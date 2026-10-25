// SAFETY.md S10: the harness can only call the sys-autopilot endpoints on the allowlist. These rows are the contract;
// add a row for every new kind of request.

import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { describe, expect, it } from 'vitest';

import { checkAllowed, loadAutopilotConfig, NotAllowedError, parseResponse } from './autopilot.ts';

const q = (p: string) => encodeURIComponent(p);

const allowed: [string, string][] = [
    ['GET', '/status'],
    ['GET', '/screenshot'],
    ['POST', '/input/tap'],
    ['POST', '/input/hold'],
    ['POST', '/input/release'],
    ['POST', '/input/stick'],
    ['POST', '/input/clear'],
    ['POST', '/input/touch'],
    ['POST', '/input/swipe'],
    ['POST', '/controller/attach'],
    ['POST', '/controller/detach'],
    ['GET', `/files?path=${q('/switch/suwayomi-nx')}`],
    ['GET', `/files?path=${q('/switch/suwayomi-nx/test/events.jsonl')}&offset=120`],
    ['GET', `/files?path=${q('/switch/suwayomi-nx/logs/app.log')}&offset=-4096`],
    ['GET', `/files?path=${q('/switch/suwayomi-nx/logs/app.log')}&offset=0&length=10`],
    ['GET', `/files?path=${q('/atmosphere/crash_reports')}`],
    ['GET', `/files?path=${q('/atmosphere/fatal_errors')}`],
    ['GET', `/files?path=${q('/atmosphere/fatal_reports')}`],
    ['GET', `/files?path=${q('/atmosphere/fatal_reports/dumps')}`],
    ['GET', `/files?path=${q('/atmosphere/crash_reports/01234_0100000000001000.log')}`],
    ['GET', `/files/hash?path=${q('/switch/suwayomi-nx/suwayomi-nx.nro')}`],
    ['PUT', `/files?path=${q('/switch/suwayomi-nx/suwayomi-nx.nro')}`],
    ['PUT', `/files?path=${q('/switch/suwayomi-nx/test/config.json')}`],
    ['DELETE', `/files?path=${q('/switch/suwayomi-nx/test/events.jsonl')}`],
    ['PUT', `/files?path=${q('/SWITCH/Suwayomi-NX/test/config.json')}`], // FAT32 ignores case: still our folder
];

const refused: [string, string][] = [
    // Removed or dangerous routes (they 404 on our fork anyway, but the harness must never even try).
    ['POST', '/install'],
    ['POST', '/settings/theme'],
    ['POST', '/settings/airplane'],
    ['POST', '/network/dns'],
    ['POST', '/power/restart'],
    ['GET', '/titles'],
    ['POST', '/mcp'],
    // Wrong method for an allowed route.
    ['POST', '/status'],
    ['DELETE', '/screenshot'],
    ['GET', '/input/tap'],
    ['POST', '/input/tap?x=1'],
    // Writes outside our folder.
    ['PUT', `/files?path=${q('/atmosphere/contents/4200000000004150/exefs.nsp')}`],
    ['PUT', `/files?path=${q('/hbmenu.nro')}`],
    ['PUT', `/files?path=${q('/bootloader/hekate_ipl.ini')}`],
    ['DELETE', `/files?path=${q('/atmosphere/crash_reports/x.log')}`],
    ['DELETE', `/files?path=${q('/atmosphere/fatal_reports/x.log')}`],
    ['PUT', `/files?path=${q('/atmosphere/fatal_errors/x.bin')}`],
    ['DELETE', `/files?path=${q('/switch/suwayomi-nx')}`], // the folder itself
    ['DELETE', `/files?path=${q('/switch/suwayomi-nx/')}`],
    ['PUT', `/files?path=${q('/switch/suwayomi-nx-evil/x')}`], // a look-alike folder
    ['PUT', `/files?path=${q('/switch/suwayomi-nx/../../atmosphere/x')}`], // traversal
    ['PUT', `/files?path=%2Fswitch%2Fsuwayomi-nx%2F%2E%2E%2Fx`], // traversal, percent-encoded
    ['PUT', `/files?path=${q('/switch/suwayomi-nx/./x')}`],
    ['PUT', `/files?path=${q('/switch//suwayomi-nx/x')}`],
    ['PUT', `/files?path=${q('/switch/suwayomi-nx\\..\\x')}`],
    ['PUT', `/files?path=${q('switch/suwayomi-nx/x')}`], // not absolute
    ['PUT', `/files?path=${q('/switch/suwayomi-nx/x')}&path=${q('/hbmenu.nro')}`], // two paths
    ['PUT', `/files?path=${q('/switch/suwayomi-nx/x\u0000.txt')}`],
    ['PUT', `/files/hash?path=${q('/switch/suwayomi-nx/x')}`],
    ['PUT', '/files'], // no path
    // Reads outside the allowed folders.
    ['GET', `/files?path=${q('/')}`],
    ['GET', `/files?path=${q('/atmosphere')}`],
    ['GET', `/files?path=${q('/config/sys-autopilot/config.ini')}`],
    ['GET', `/files?path=${q('/atmosphere/crash_reports/../../config/sys-autopilot/config.ini')}`],
    ['GET', `/files?path=${q('/switch/suwayomi-nx/x')}&extra=1`],
    ['GET', `/files/hash?path=${q('/switch/suwayomi-nx/x')}&offset=1`],
];

describe('sys-autopilot allowlist (SAFETY.md S10)', () => {
    it.each(allowed)('allows %s %s', (method, url) => {
        expect(() => checkAllowed(method, url)).not.toThrow();
    });

    it.each(refused)('refuses %s %s', (method, url) => {
        expect(() => checkAllowed(method, url)).toThrow(NotAllowedError);
    });
});

describe('HTTP answers', () => {
    const answer = (head: string, body: string) => Buffer.from(`${head}\r\n\r\n${body}`, 'latin1');

    it('is complete only once all Content-Length bytes are there', () => {
        const full = answer('HTTP/1.1 200 OK\r\nServer: sys-autopilot\r\nContent-Length: 11', '{"ok":true}');
        expect(parseResponse(full)).toEqual({ status: 200, body: Buffer.from('{"ok":true}') });
        expect(parseResponse(full.subarray(0, full.length - 1))).toBeNull();
        expect(parseResponse(full.subarray(0, 20))).toBeNull(); // headers not finished
    });

    it('reads the status, ignores bytes after the body, and needs Content-Length', () => {
        expect(parseResponse(answer('HTTP/1.1 404 Not Found\r\ncontent-length: 2', 'nojunk'))).toEqual({ status: 404, body: Buffer.from('no') });
        expect(parseResponse(answer('HTTP/1.1 200 OK', 'x'))).toBeNull();
        expect(() => parseResponse(answer('SSH-2.0-nope', ''))).toThrow(/not an HTTP answer/);
    });
});

describe('device .env', () => {
    it('reads host, port and token, and never puts the token in an error', () => {
        const dir = mkdtempSync(join(tmpdir(), 'snx-env-'));
        try {
            const env = join(dir, '.env');
            writeFileSync(env, 'SWITCH_HOST=192.0.2.7\r\nSWITCH_PORT=4150\r\nSWITCH_TOKEN=secret-token-value\r\n');
            expect(loadAutopilotConfig(env)).toEqual({ host: '192.0.2.7', port: 4150, token: 'secret-token-value' });
            writeFileSync(env, 'SWITCH_TOKEN=secret-token-value\n');
            expect(() => loadAutopilotConfig(env)).toThrow(/SWITCH_HOST/);
            try {
                loadAutopilotConfig(env);
            } catch (e) {
                expect(String(e)).not.toContain('secret-token-value');
            }
            expect(() => loadAutopilotConfig(join(dir, 'missing.env'))).toThrow(/address and token/);
        } finally {
            rmSync(dir, { recursive: true, force: true });
        }
    });
});
