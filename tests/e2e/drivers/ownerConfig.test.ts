// ownerConfig.ts: the owner's config.json on the test Switch comes back exactly, whatever the tests left there.
// The console is a fake: a map of SD paths to bytes, with the four file calls the backup uses.

import { createHash } from 'node:crypto';
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { afterEach, beforeEach, describe, expect, it } from 'vitest';

import { APP_DIR } from './autopilot.ts';
import { OwnerConfigBackup, readPendingOwnerConfig, type ConfigFileApi } from './ownerConfig.ts';

const CONFIG = `${APP_DIR}/config.json`;
const BAK = `${APP_DIR}/config.json.bak`;
const TEST_CONFIG = `${APP_DIR}/test/config.json`;

class FakeConsole implements ConfigFileApi {
    readonly files = new Map<string, Buffer>();
    failPuts = 0; // the next N uploads arrive broken (the console deletes a broken upload)

    async getFile(sdPath: string): Promise<Buffer | null> {
        return this.files.get(sdPath) ?? null;
    }
    async sha256(sdPath: string): Promise<string | null> {
        const data = this.files.get(sdPath);
        return data ? createHash('sha256').update(data).digest('hex') : null;
    }
    async putFile(sdPath: string, data: Buffer): Promise<void> {
        if (this.failPuts > 0) {
            this.failPuts--;
            this.files.delete(sdPath);
            return;
        }
        this.files.set(sdPath, Buffer.from(data));
    }
    async deleteFile(sdPath: string): Promise<void> {
        this.files.delete(sdPath);
    }
}

const owner = Buffer.from('{"server":{"url":"http://owner-server:4568"}}\n');
const ownerBak = Buffer.from('{"server":{"url":"http://owner-server:4568"},"older":true}\n');
const tests = Buffer.from('{"server":{"url":"http://test-server:4568"}}\n');

let dir: string;
let fake: FakeConsole;

beforeEach(() => {
    dir = mkdtempSync(join(tmpdir(), 'snx-owner-config-'));
    fake = new FakeConsole();
});
afterEach(() => rmSync(dir, { recursive: true, force: true }));

/** What a scenario does to the console: removes both files, the app saves the test server, the harness leaves its test config. */
function runTests(): void {
    fake.files.delete(BAK);
    fake.files.set(CONFIG, tests);
    fake.files.set(TEST_CONFIG, Buffer.from('{"server":{"url":"http://test-server:4568"}}'));
}

describe('OwnerConfigBackup', () => {
    it('puts both files back byte for byte and removes the test config', async () => {
        fake.files.set(CONFIG, owner);
        fake.files.set(BAK, ownerBak);
        const backup = new OwnerConfigBackup(fake, dir);
        const { manifest, earlier } = await backup.ensure();
        expect(earlier).toBe(false);
        expect(manifest.files['config.json']).toMatch(/^[0-9a-f]{64}$/);
        expect(readFileSync(join(dir, 'config.json'))).toEqual(owner);

        runTests();
        expect(await backup.restore()).toBe(true);
        expect(fake.files.get(CONFIG)).toEqual(owner);
        expect(fake.files.get(BAK)).toEqual(ownerBak);
        expect(fake.files.has(TEST_CONFIG)).toBe(false);
        expect(readPendingOwnerConfig(dir)).toBeNull();
        expect(existsSync(join(dir, 'restored.json'))).toBe(true);
        expect(await backup.restore()).toBe(false); // nothing pending any more
    });

    it('deletes what the tests left where the console had no file', async () => {
        const backup = new OwnerConfigBackup(fake, dir);
        expect((await backup.ensure()).manifest.files).toEqual({ 'config.json': null, 'config.json.bak': null });
        runTests();
        fake.files.set(BAK, tests);
        await backup.restore();
        expect(fake.files.has(CONFIG)).toBe(false);
        expect(fake.files.has(BAK)).toBe(false);
    });

    it('keeps a backup a stopped run never put back, instead of copying the tests\' files', async () => {
        fake.files.set(CONFIG, owner);
        await new OwnerConfigBackup(fake, dir).ensure();
        runTests(); // ... and the run stops here: no restore

        const next = new OwnerConfigBackup(fake, dir);
        const { earlier } = await next.ensure();
        expect(earlier).toBe(true);
        await next.restore();
        expect(fake.files.get(CONFIG)).toEqual(owner);
    });

    it('uploads once more when an upload arrives broken', async () => {
        fake.files.set(CONFIG, owner);
        const backup = new OwnerConfigBackup(fake, dir);
        await backup.ensure();
        runTests();
        fake.failPuts = 1;
        await backup.restore();
        expect(fake.files.get(CONFIG)).toEqual(owner);
    });

    it('stays pending when the files can\'t be put back, so the next restore tries again', async () => {
        fake.files.set(CONFIG, owner);
        const backup = new OwnerConfigBackup(fake, dir);
        await backup.ensure();
        runTests();
        fake.failPuts = 2;
        await expect(backup.restore()).rejects.toThrow(/failed twice/);
        expect(readPendingOwnerConfig(dir)).not.toBeNull();
        await backup.restore();
        expect(fake.files.get(CONFIG)).toEqual(owner);
    });

    it('refuses a copy that no longer matches its hash', async () => {
        fake.files.set(CONFIG, owner);
        await new OwnerConfigBackup(fake, dir).ensure();
        writeFileSync(join(dir, 'config.json'), 'changed');
        expect(() => readPendingOwnerConfig(dir)).toThrow(/doesn't match/);
        await expect(new OwnerConfigBackup(fake, dir).restore()).rejects.toThrow(/doesn't match/);
    });
});
