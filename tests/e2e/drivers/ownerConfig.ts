// The owner's own app settings on the test Switch (D11, D18). The console is also the owner's reading console, so its
// sdmc:/switch/suwayomi-nx/config.json (and the app's config.json.bak) hold the owner's real server, and progress-queue.json
// (+ .bak, WP-15b) the owner's progress saves still waiting for that server. Scenarios need a first-run config and no waiting
// saves: the SwitchDriver removes all four before a launch, and the app then saves the test server there. This backup gives
// the owner's files back exactly (so a test run never loses the owner's waiting saves, nor sends them to the test server):
// - before the first change, ensure() copies the files into tests/e2e/out/owner-config/ (git-ignored), checks each copy
//   against the console's SHA-256, and writes pending.json last. pending.json there = the console still has the tests' files.
// - at the end of every scenario (SwitchDriver.dispose) and once more after the run (tools/e2e.sh, restore-config.ts), restore()
//   uploads the copies again with a hash check, deletes what the tests left where there was no file, removes the harness's own
//   test/config.json, and renames pending.json to restored.json.
// - a pending.json that is already there (an earlier run stopped before it could put the files back) is never overwritten:
//   it still holds the owner's files, while the console may hold the tests'. The next restore() puts it back.

import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, readFileSync, renameSync, rmSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { APP_DIR, putVerified, type Autopilot } from './autopilot.ts';

export const OWNER_CONFIG_DIR = fileURLToPath(new URL('../out/owner-config/', import.meta.url));

/**
 * The app's settings (src/app/ConfigStore.cpp), its waiting progress saves (src/ui/reader/WaitingSaves.cpp, WP-15b), and the
 * copy SafeFs keeps of the last good one of each.
 */
export const CONFIG_FILES = ['config.json', 'config.json.bak', 'progress-queue.json', 'progress-queue.json.bak'] as const;
export type ConfigFile = (typeof CONFIG_FILES)[number];

/** The harness's own file. A test build that finds it starts with the test server, so the restore removes it too. */
export const TEST_CONFIG = 'test/config.json';

/** The console calls the backup needs (Autopilot has them; the unit test passes a fake). */
export type ConfigFileApi = Pick<Autopilot, 'getFile' | 'sha256' | 'putFile' | 'deleteFile'>;

export interface OwnerConfigManifest {
    takenAt: string;
    /**
     * SHA-256 of each file as it was on the console; null = it wasn't there. A file with no key: the backup was made before
     * the harness knew it (a pending.json from before WP-15b has no queue files), so the restore leaves it as it is.
     */
    files: Partial<Record<ConfigFile, string | null>>;
    restoredAt?: string;
}

const sha256 = (data: Buffer) => createHash('sha256').update(data).digest('hex');

/** The backup the console still owes back (pending.json), or null. Throws if a saved copy doesn't match its hash. */
export function readPendingOwnerConfig(dir = OWNER_CONFIG_DIR): OwnerConfigManifest | null {
    const file = join(dir, 'pending.json');
    if (!existsSync(file)) return null;
    const manifest = JSON.parse(readFileSync(file, 'utf8')) as OwnerConfigManifest;
    for (const name of CONFIG_FILES) {
        const want = manifest.files[name];
        if (want === null || want === undefined) continue;
        const copy = join(dir, name);
        if (!existsSync(copy) || sha256(readFileSync(copy)) !== want) {
            throw new Error(`the copy ${copy} doesn't match ${file}: the console's own ${name} can't be put back from it. Please tell Claude.`);
        }
    }
    return manifest;
}

export class OwnerConfigBackup {
    private readonly api: ConfigFileApi;
    readonly dir: string;

    constructor(api: ConfigFileApi, dir = OWNER_CONFIG_DIR) {
        this.api = api;
        this.dir = dir;
    }

    /**
     * Before the tests change config.json: copies the console's CONFIG_FILES here, unless a backup is still
     * pending. `earlier` = that pending one was there already (from a run that stopped early). Only while the app is closed.
     */
    async ensure(): Promise<{ manifest: OwnerConfigManifest; earlier: boolean }> {
        const pending = readPendingOwnerConfig(this.dir);
        if (pending) return { manifest: pending, earlier: true };

        mkdirSync(this.dir, { recursive: true });
        rmSync(join(this.dir, 'restored.json'), { force: true });
        const files = {} as Record<ConfigFile, string | null>;
        for (const name of CONFIG_FILES) {
            const sdPath = `${APP_DIR}/${name}`;
            const copy = join(this.dir, name);
            rmSync(copy, { force: true });
            const data = await this.api.getFile(sdPath);
            if (data === null) {
                files[name] = null;
                continue;
            }
            const hash = sha256(data);
            const onConsole = await this.api.sha256(sdPath);
            if (onConsole !== hash) throw new Error(`the copy of ${sdPath} doesn't match the console's SHA-256 (${hash} vs ${onConsole})`);
            writeFileSync(copy, data);
            files[name] = hash;
        }
        const manifest: OwnerConfigManifest = { takenAt: new Date().toISOString(), files };
        const file = join(this.dir, 'pending.json');
        writeFileSync(`${file}.tmp`, JSON.stringify(manifest, null, 2) + '\n');
        renameSync(`${file}.tmp`, file); // last, and whole: a half-made backup never counts as pending
        return { manifest, earlier: false };
    }

    /**
     * Puts the pending backup back on the console exactly and marks it done (restored.json). False when nothing was pending.
     * Only while the app is closed: the app would save its own settings over the files again.
     */
    async restore(): Promise<boolean> {
        const manifest = readPendingOwnerConfig(this.dir);
        if (!manifest) return false;
        for (const name of CONFIG_FILES) {
            const sdPath = `${APP_DIR}/${name}`;
            const want = manifest.files[name];
            if (want === undefined) continue; // not in this backup (an older one): left as it is
            if (want === null) {
                await this.api.deleteFile(sdPath);
                if ((await this.api.sha256(sdPath)) !== null) throw new Error(`${sdPath} is still there after deleting it`);
            } else if ((await this.api.sha256(sdPath)) !== want) {
                await putVerified(this.api, sdPath, readFileSync(join(this.dir, name)));
            }
        }
        await this.api.deleteFile(`${APP_DIR}/${TEST_CONFIG}`);
        const file = join(this.dir, 'pending.json');
        writeFileSync(join(this.dir, 'restored.json'), JSON.stringify({ ...manifest, restoredAt: new Date().toISOString() }, null, 2) + '\n');
        rmSync(file);
        return true;
    }
}
