// SAFETY.md S10: the harness can only call the sys-autopilot endpoints on the allowlist. These rows are the contract;
// add a row for every new kind of request.

import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { createServer, type AddressInfo } from 'node:net';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { describe, expect, it, vi } from 'vitest';

import { answerCodes, Autopilot, checkAllowed, ConsoleUnreachableError, loadAutopilotConfig, NotAllowedError, parseResponse } from './autopilot.ts';

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

    it('skips "100 Continue" and lists the answers so far', () => {
        const go = Buffer.from('HTTP/1.1 100 Continue\r\n\r\n', 'latin1');
        const ok = answer('HTTP/1.1 200 OK\r\nContent-Length: 11', '{"ok":true}');
        expect(answerCodes(go.subarray(0, 10))).toEqual([]);
        expect(answerCodes(go)).toEqual([100]);
        expect(parseResponse(go)).toBeNull();
        expect(answerCodes(Buffer.concat([go, ok]))).toEqual([100, 200]);
        expect(parseResponse(Buffer.concat([go, ok]))).toEqual({ status: 200, body: Buffer.from('{"ok":true}') });
        expect(answerCodes(answer('HTTP/1.1 401 Unauthorized\r\nContent-Length: 0', ''))).toEqual([401]);
    });
});

/**
 * A stand-in for sys-autopilot on this PC. Connection n (1, 2, ...) does `steps[n - 1]` (the last one repeats):
 * "reset-at-head" resets as soon as the request's head is there, "reset-after-body" says "100 Continue" when asked, takes the
 * body and resets, "answer" says "100 Continue" when asked, takes the body and answers 200 like the real one.
 */
async function fakeConsole(steps: ('reset-at-head' | 'reset-after-body' | 'answer')[], port = 0) {
    const heads: string[] = [];
    const bodies: string[] = []; // the bodies that reached the "console": an input there may have been played
    const early: string[] = []; // body bytes that came with the head although it asked first
    const server = createServer((socket) => {
        const step = steps[Math.min(heads.length, steps.length - 1)];
        let data = Buffer.alloc(0);
        let head = '';
        socket.on('error', () => undefined);
        socket.on('data', (chunk: Buffer) => {
            data = Buffer.concat([data, chunk]);
            const end = data.indexOf('\r\n\r\n');
            if (end < 0) return;
            const body = data.subarray(end + 4);
            if (!head) {
                head = data.subarray(0, end).toString('latin1');
                heads.push(head);
                const asks = /\r\nexpect: 100-continue/i.test(head);
                if (asks && body.length) early.push(body.toString('utf8'));
                if (step === 'reset-at-head') return void socket.resetAndDestroy();
                if (asks) socket.write('HTTP/1.1 100 Continue\r\n\r\n');
            }
            if (body.length < Number(/\r\ncontent-length: (\d+)/i.exec(head)?.[1] ?? 0)) return;
            bodies.push(body.toString('utf8'));
            if (step === 'reset-after-body') return void socket.resetAndDestroy();
            socket.write('HTTP/1.1 200 OK\r\nContent-Length: 11\r\n\r\n{"ok":true}');
        });
    });
    await new Promise<void>((done) => server.listen(port, '127.0.0.1', done));
    const api = new Autopilot({ host: '127.0.0.1', port: (server.address() as AddressInfo).port, token: 'test-token' });
    const close = () => new Promise<void>((done) => server.close(() => done()));
    return { api, heads, bodies, early, close };
}

describe('resets (the console sometimes resets a new connection)', () => {
    it('sends an input again when the console was reset before it had the body', async () => {
        const warn = vi.spyOn(console, 'warn').mockImplementation(() => undefined);
        const c = await fakeConsole(['reset-at-head', 'answer']);
        try {
            expect((await c.api.touch(640, 360)).status).toBe(200);
            expect(c.heads).toHaveLength(2);
            expect(c.heads.every((h) => /\r\nExpect: 100-continue/.test(h))).toBe(true);
            expect(c.early).toEqual([]); // the body waits for "100 Continue"
            expect(c.bodies).toEqual(['{"x":640,"y":360}']); // played once
            expect(String(warn.mock.calls[0]?.[0])).toMatch(/ECONNRESET .*never had this input/);
        } finally {
            warn.mockRestore();
            await c.close();
        }
    });

    it('never sends an input again once the console had its body', async () => {
        const c = await fakeConsole(['reset-after-body', 'answer']);
        try {
            const error = await c.api.tap(['A']).catch((e: unknown) => e);
            expect(error).toBeInstanceOf(ConsoleUnreachableError);
            expect((error as ConsoleUnreachableError).delivered).toBe(true);
            expect(c.heads).toHaveLength(1);
            expect(c.bodies).toEqual(['{"buttons":["A"]}']);
        } finally {
            await c.close();
        }
    });

    it('sends an input again when the connection never opened (refused)', async () => {
        const warn = vi.spyOn(console, 'warn').mockImplementation(() => undefined);
        const free = createServer(); // a port with nothing on it yet
        await new Promise<void>((done) => free.listen(0, '127.0.0.1', done));
        const port = (free.address() as AddressInfo).port;
        await new Promise<void>((done) => free.close(() => done()));
        const api = new Autopilot({ host: '127.0.0.1', port, token: 'test-token' });
        const touched = api.touch(5, 6);
        await new Promise((r) => setTimeout(r, 200)); // the first try is refused by now; the next one comes after 500 ms
        const c = await fakeConsole(['answer'], port);
        try {
            expect((await touched).status).toBe(200);
            expect(c.bodies).toEqual(['{"x":5,"y":6}']);
            expect(String(warn.mock.calls[0]?.[0])).toMatch(/ECONNREFUSED .*while connecting.*never had this input/);
        } finally {
            warn.mockRestore();
            await c.close();
        }
    });

    it('tries reads and /input/clear again, without asking first', async () => {
        const warn = vi.spyOn(console, 'warn').mockImplementation(() => undefined);
        const c = await fakeConsole(['reset-at-head', 'answer', 'reset-at-head', 'answer']);
        try {
            expect((await c.api.request('GET', '/status')).status).toBe(200);
            expect((await c.api.clearInput()).status).toBe(200);
            expect(c.heads).toHaveLength(4);
            expect(c.heads.some((h) => /expect:/i.test(h))).toBe(false);
            expect(warn).toHaveBeenCalledTimes(2);
        } finally {
            warn.mockRestore();
            await c.close();
        }
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
