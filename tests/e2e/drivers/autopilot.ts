// REST client for sys-autopilot-nx on the test Switch (D19, D22, tools/sys-autopilot-nx/README.md).
// Every request goes through checkAllowed() first: the SAFETY.md S10 allowlist. Anything else throws before a byte is sent.
// Host, port and bearer token come from tests/e2e/device/.env (git-ignored). The token never appears in messages or logs.

import { readFileSync } from 'node:fs';
import { connect } from 'node:net';
import { fileURLToPath } from 'node:url';

export const DEVICE_ENV = fileURLToPath(new URL('../device/.env', import.meta.url));

/** Our folder on the SD card (D10). The only place the harness may write or delete. */
export const APP_DIR = '/switch/suwayomi-nx';
/** creport's crash reports: a new one means some program crashed (in application mode: our app, without a system fatal). */
export const CRASH_REPORTS_DIR = '/atmosphere/crash_reports';
/**
 * A new file in either is a system fatal: fatal_reports/ is written by Atmosphère's "A fatal error occurred" screen (except
 * when creport caused it), fatal_errors/ at the next boot after a secure-monitor panic. Owner OK for fatal_reports: 2026-10-25.
 */
export const FATAL_DIRS = ['/atmosphere/fatal_errors', '/atmosphere/fatal_reports'] as const;
/** Folders outside ours the harness may read (list and download), never write. */
export const CRASH_DIRS = [CRASH_REPORTS_DIR, ...FATAL_DIRS] as const;

export interface AutopilotConfig {
    host: string;
    port: number;
    token: string;
}

export function loadAutopilotConfig(envPath = DEVICE_ENV): AutopilotConfig {
    let text: string;
    try {
        text = readFileSync(envPath, 'utf8');
    } catch {
        throw new Error(`no ${envPath}: it holds the console's address and token (docs/SETUP.md, WP-00b)`);
    }
    const values = new Map<string, string>();
    for (const line of text.split(/\r?\n/)) {
        const m = /^\s*([A-Z_]+)\s*=\s*(.*?)\s*$/.exec(line);
        if (m) values.set(m[1], m[2]);
    }
    const host = values.get('SWITCH_HOST');
    const port = Number(values.get('SWITCH_PORT') ?? '4150');
    const token = values.get('SWITCH_TOKEN');
    if (!host || !token || !Number.isInteger(port)) throw new Error(`${envPath} needs SWITCH_HOST, SWITCH_PORT and SWITCH_TOKEN`);
    return { host, port, token };
}

/** A request the S10 allowlist refuses. */
export class NotAllowedError extends Error {}

/** True when `path` (an SD path from a query) is `dir` itself or inside it. No "..", ".", "//", backslashes or control characters. */
function isUnder(path: string, dir: string, allowDirItself: boolean): boolean {
    if (!path.startsWith('/') || /[\\\x00-\x1f]/.test(path) || path.includes('//')) return false;
    const segments = path.split('/').slice(1);
    if (segments.some((s) => s === '.' || s === '..')) return false;
    // FAT32 ignores case, so /SWITCH/Suwayomi-NX is the same folder: compare without case.
    const p = path.toLowerCase().replace(/\/$/, '');
    const d = dir.toLowerCase();
    return p.startsWith(d + '/') || (allowDirItself && p === d);
}

/**
 * SAFETY.md S10. Allowed: GET /status, GET /screenshot, POST /input/<x>, POST /controller/<x>; GET /files and /files/hash only
 * under our folder or the two crash folders; PUT/DELETE /files only inside our folder. Nothing else, ever.
 */
export function checkAllowed(method: string, url: string): void {
    const refuse = (why: string): never => {
        throw new NotAllowedError(`sys-autopilot request refused by the S10 allowlist: ${method} ${url} (${why})`);
    };
    const parsed = new URL(url, 'http://switch');
    const route = parsed.pathname;
    const m = method.toUpperCase();
    const params = [...parsed.searchParams.keys()];

    if (m === 'GET' && (route === '/status' || route === '/screenshot')) {
        if (params.length) refuse('no query expected');
        return;
    }
    if (m === 'POST' && /^\/(input|controller)\/[a-z]+$/.test(route)) {
        if (params.length) refuse('no query expected');
        return;
    }
    if (route === '/files' || route === '/files/hash') {
        const allowedParams = route === '/files' && m === 'GET' ? ['path', 'offset', 'length'] : ['path'];
        if (params.some((p) => !allowedParams.includes(p))) refuse(`only ${allowedParams.join(', ')} allowed in the query`);
        if (parsed.searchParams.getAll('path').length !== 1) refuse('exactly one path');
        const path = parsed.searchParams.get('path')!;
        if (m === 'GET') {
            if (isUnder(path, APP_DIR, true) || CRASH_DIRS.some((d) => isUnder(path, d, true))) return;
            refuse(`reads only under ${APP_DIR} and ${CRASH_DIRS.join(', ')}`);
        }
        if ((m === 'PUT' || m === 'DELETE') && route === '/files') {
            if (isUnder(path, APP_DIR, false)) return;
            refuse(`writes and deletes only inside ${APP_DIR}/`);
        }
    }
    refuse('not on the list');
}

export interface Response {
    status: number;
    body: Buffer;
}

export interface DirEntry {
    name: string;
    type: 'file' | 'dir';
    size?: number;
}

/**
 * A complete HTTP answer (status line, headers, then exactly Content-Length bytes), or null if more bytes are needed.
 * sys-autopilot always sends Content-Length (http.c), so an answer without it is never complete.
 */
export function parseResponse(data: Buffer): Response | null {
    const end = data.indexOf('\r\n\r\n');
    if (end < 0) return null;
    const lines = data.subarray(0, end).toString('latin1').split('\r\n');
    const status = /^HTTP\/1\.[01] (\d{3})/.exec(lines[0] ?? '');
    if (!status) throw new Error(`not an HTTP answer: ${(lines[0] ?? '').slice(0, 80)}`);
    const lengthLine = lines.find((l) => /^content-length:/i.test(l));
    if (!lengthLine) return null;
    const length = Number(lengthLine.slice(lengthLine.indexOf(':') + 1).trim());
    const bodyStart = end + 4;
    if (!Number.isInteger(length) || data.length < bodyStart + length) return null;
    return { status: Number(status[1]), body: data.subarray(bodyStart, bodyStart + length) };
}

/** The console didn't answer at all (asleep, Wi-Fi off, rebooting, or stuck on a fatal error screen). */
export class ConsoleUnreachableError extends Error {}

export const UNREACHABLE_HELP =
    'Please look at the console. If it shows "A fatal error occurred" (press POWER to reboot): press POWER, boot back into ' +
    'CFW (Hekate → Launch → CFW (EMUMMC)), and tell Claude what the screen said. If it is asleep or in airplane mode: wake it ' +
    'or turn Wi-Fi back on.';

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

/** Shortest time between two connections to the console (see request()). */
const MIN_GAP_MS = 150;

export class Autopilot {
    private readonly config: AutopilotConfig;

    constructor(config: AutopilotConfig = loadAutopilotConfig()) {
        this.config = config;
    }

    get host(): string {
        return this.config.host;
    }

    /**
     * One allowed request, and never two at once (the sysmodule's server handles one connection at a time). Network failures
     * become ConsoleUnreachableError. A request that changes nothing or can safely happen twice (everything except /input and
     * /controller) is tried once more after a reset, with a warning on stderr.
     */
    request(method: string, url: string, body?: Buffer | string | object, timeoutMs = 30_000): Promise<Response> {
        checkAllowed(method, url);
        const repeatable = !/^\/(input|controller)\//.test(url);
        const attempt = async (): Promise<Response> => {
            // WP-03: after bursts of connections (~50 within a second) the console reset every new one for ~10 s, curl's too.
            // So connections start at least MIN_GAP_MS apart, and repeatable requests back off and try again.
            for (const backoffMs of [500, 1_000, 2_000, 4_000, 8_000, -1]) {
                await sleep(Math.max(0, this.lastStart + MIN_GAP_MS - Date.now()));
                this.lastStart = Date.now();
                try {
                    return await this.send(method, url, body, timeoutMs);
                } catch (e) {
                    const reset = e instanceof ConsoleUnreachableError && /ECONNRESET|before the answer/.test(e.message);
                    if (!repeatable || !reset || backoffMs < 0) throw e;
                    console.warn(`[autopilot] ${e.message}; again in ${backoffMs} ms`);
                    await sleep(backoffMs);
                }
            }
            throw new Error('unreachable');
        };
        const result = this.queue.then(attempt, attempt);
        this.queue = result.catch(() => undefined);
        return result;
    }

    private queue: Promise<unknown> = Promise.resolve();
    private lastStart = 0;

    /**
     * Plain HTTP/1.1 over a fresh TCP connection per request. Found in WP-03:
     * - Node's fetch and http modules both reported "connection reset" for GET /files answers that curl and a plain socket
     *   receive whole, so this reads the answer itself and checks that all Content-Length bytes arrived.
     * - With "Connection: close" the console closes every connection itself and has to remember each closed one for a while.
     *   After a few hundred quick requests it started resetting new connections, and recovered by itself after some seconds.
     *   So the request keeps the connection open (HTTP/1.1 default) and this side closes it once the answer is complete.
     */
    private send(method: string, url: string, body: Buffer | string | object | undefined, timeoutMs: number): Promise<Response> {
        let payload: Buffer = Buffer.alloc(0);
        let contentType = '';
        if (Buffer.isBuffer(body) || typeof body === 'string') {
            payload = Buffer.isBuffer(body) ? body : Buffer.from(body, 'utf8');
            contentType = 'application/octet-stream';
        } else if (body !== undefined) {
            payload = Buffer.from(JSON.stringify(body), 'utf8');
            contentType = 'application/json';
        }
        const head =
            `${method} ${url} HTTP/1.1\r\nHost: ${this.config.host}:${this.config.port}\r\n` +
            `Authorization: Bearer ${this.config.token}\r\n` +
            (body !== undefined ? `Content-Type: ${contentType}\r\nContent-Length: ${payload.length}\r\n` : '') +
            '\r\n';
        const where = `sys-autopilot at ${this.config.host} didn't answer ${method} ${url.split('?')[0]}`;

        return new Promise<Response>((resolve, reject) => {
            const chunks: Buffer[] = [];
            let settled = false;
            const finish = (error?: Error) => {
                if (settled) return;
                const parsed = parseResponse(Buffer.concat(chunks));
                if (parsed) {
                    settled = true;
                    socket.end(); // we close first, so the "recently closed" bookkeeping stays on the PC
                    setTimeout(() => socket.destroy(), 2_000).unref(); // in case the console never closes its side
                    resolve(parsed);
                } else if (error) {
                    settled = true;
                    socket.destroy();
                    reject(new ConsoleUnreachableError(`${where}: ${error.message}`));
                }
            };
            const socket = connect({ host: this.config.host, port: this.config.port }, () => {
                socket.write(head);
                if (payload.length) socket.write(payload);
            });
            socket.setTimeout(timeoutMs, () => finish(new Error(`no complete answer within ${timeoutMs / 1000} s`)));
            socket.on('data', (c: Buffer) => {
                chunks.push(c);
                finish();
            });
            socket.on('end', () => finish(new Error('the connection closed before the answer was complete')));
            socket.on('error', (e) => finish(e));
        });
    }

    /** Like request(), but anything other than the expected status is an error with the console's message. */
    async expect(status: number | number[], method: string, url: string, body?: Buffer | string | object, timeoutMs?: number): Promise<Response> {
        const res = await this.request(method, url, body, timeoutMs);
        const ok = Array.isArray(status) ? status.includes(res.status) : res.status === status;
        if (!ok) throw new Error(`${method} ${url} answered HTTP ${res.status}: ${res.body.toString('utf8').slice(0, 300)}`);
        return res;
    }

    async status(): Promise<Record<string, unknown>> {
        return JSON.parse((await this.expect(200, 'GET', '/status', undefined, 10_000)).body.toString('utf8'));
    }

    /** JPEG 1280×720. Captures fail during app switches (HTTP 500, rc 0xe0ce), so it retries and accepts only real JPEG data. */
    async screenshot(tries = 10): Promise<Buffer> {
        let last = '';
        for (let i = 0; i < tries; i++) {
            const res = await this.request('GET', '/screenshot', undefined, 15_000);
            if (res.status === 200 && res.body[0] === 0xff && res.body[1] === 0xd8) return res.body;
            last = `HTTP ${res.status} ${res.body.toString('utf8').slice(0, 100)}`;
            await sleep(500);
        }
        throw new Error(`no screenshot after ${tries} tries (last: ${last})`);
    }

    tap(buttons: string[], durationMs?: number): Promise<Response> {
        return this.expect(200, 'POST', '/input/tap', durationMs ? { buttons, durationMs } : { buttons });
    }
    hold(buttons: string[]): Promise<Response> {
        return this.expect(200, 'POST', '/input/hold', { buttons });
    }
    release(buttons: string[]): Promise<Response> {
        return this.expect(200, 'POST', '/input/release', { buttons });
    }
    clearInput(): Promise<Response> {
        return this.expect(200, 'POST', '/input/clear', {});
    }
    stick(side: 'left' | 'right', x: number, y: number, durationMs: number): Promise<Response> {
        return this.expect(200, 'POST', '/input/stick', { side, x, y, durationMs }, durationMs + 15_000);
    }
    touch(x: number, y: number, durationMs?: number): Promise<Response> {
        const body = durationMs ? { x: Math.round(x), y: Math.round(y), durationMs } : { x: Math.round(x), y: Math.round(y) };
        return this.expect(200, 'POST', '/input/touch', body, (durationMs ?? 100) + 15_000);
    }
    swipe(x1: number, y1: number, x2: number, y2: number, durationMs: number, steps?: number): Promise<Response> {
        const body: Record<string, number> = { x1, y1, x2, y2, durationMs };
        for (const k of Object.keys(body)) body[k] = Math.round(body[k]);
        if (steps) body.steps = steps;
        return this.expect(200, 'POST', '/input/swipe', body, durationMs + 15_000);
    }

    /** Names in an SD folder, sorted. A missing folder is an empty list. */
    async listDir(sdPath: string): Promise<DirEntry[]> {
        const res = await this.request('GET', `/files?path=${encodeURIComponent(sdPath)}`);
        if (res.status === 404) return [];
        if (res.status !== 200) throw new Error(`listing ${sdPath} answered HTTP ${res.status}`);
        const entries = (JSON.parse(res.body.toString('utf8')) as { entries: DirEntry[] }).entries;
        return entries.sort((a, b) => a.name.localeCompare(b.name));
    }

    /** A file's bytes from `offset` on (negative = the last N bytes). null if it doesn't exist. */
    async getFile(sdPath: string, offset = 0): Promise<Buffer | null> {
        const res = await this.request('GET', `/files?path=${encodeURIComponent(sdPath)}&offset=${offset}`);
        if (res.status === 404) return null;
        if (res.status !== 200) throw new Error(`reading ${sdPath} answered HTTP ${res.status}`);
        return res.body;
    }

    async sha256(sdPath: string): Promise<string | null> {
        const res = await this.request('GET', `/files/hash?path=${encodeURIComponent(sdPath)}`, undefined, 60_000);
        if (res.status === 404) return null;
        if (res.status !== 200) throw new Error(`hashing ${sdPath} answered HTTP ${res.status}`);
        return (JSON.parse(res.body.toString('utf8')) as { hash: string }).hash;
    }

    /** Uploads in place (not atomic: a broken upload deletes the file), so callers check the hash afterwards. */
    async putFile(sdPath: string, data: Buffer): Promise<void> {
        await this.expect([200, 201], 'PUT', `/files?path=${encodeURIComponent(sdPath)}`, data, 120_000);
    }

    /** Deletes a file. Missing is fine. */
    async deleteFile(sdPath: string): Promise<void> {
        await this.expect([200, 204, 404], 'DELETE', `/files?path=${encodeURIComponent(sdPath)}`);
    }
}
