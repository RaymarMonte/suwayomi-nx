// REST client for sys-autopilot-nx on the test Switch (D19, D22, tools/sys-autopilot-nx/README.md).
// Every request goes through checkAllowed() first: the SAFETY.md S10 allowlist. Anything else throws before a byte is sent.
// Host, port and bearer token come from tests/e2e/device/.env (git-ignored). The token never appears in messages or logs.

import { createHash } from 'node:crypto';
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
 * The status codes of the answers at the start of `data`, in order, each one complete (status line and headers). An input
 * request gets "100 Continue" first (see Autopilot.send()), then the real answer.
 */
export function answerCodes(data: Buffer): number[] {
    const codes: number[] = [];
    let start = 0;
    for (;;) {
        const end = data.indexOf('\r\n\r\n', start);
        if (end < 0) return codes;
        const firstLine = data.subarray(start, end).toString('latin1').split('\r\n')[0];
        const status = /^HTTP\/1\.[01] (\d{3})/.exec(firstLine);
        if (!status) throw new Error(`not an HTTP answer: ${firstLine.slice(0, 80)}`);
        codes.push(Number(status[1]));
        if (!status[1].startsWith('1')) return codes; // a real answer: a body follows, not another head
        start = end + 4;
    }
}

/**
 * A complete HTTP answer (status line, headers, then exactly Content-Length bytes), or null if more bytes are needed.
 * An interim "100 Continue" before it is skipped. sys-autopilot always sends Content-Length (http.c), so an answer without
 * it is never complete.
 */
export function parseResponse(data: Buffer): Response | null {
    let start = 0;
    for (;;) {
        const end = data.indexOf('\r\n\r\n', start);
        if (end < 0) return null;
        const lines = data.subarray(start, end).toString('latin1').split('\r\n');
        const status = /^HTTP\/1\.[01] (\d{3})/.exec(lines[0] ?? '');
        if (!status) throw new Error(`not an HTTP answer: ${(lines[0] ?? '').slice(0, 80)}`);
        if (status[1].startsWith('1')) {
            start = end + 4; // "100 Continue": the real answer comes after it
            continue;
        }
        const lengthLine = lines.find((l) => /^content-length:/i.test(l));
        if (!lengthLine) return null;
        const length = Number(lengthLine.slice(lengthLine.indexOf(':') + 1).trim());
        const bodyStart = end + 4;
        if (!Number.isInteger(length) || data.length < bodyStart + length) return null;
        return { status: Number(status[1]), body: data.subarray(bodyStart, bodyStart + length) };
    }
}

/**
 * The console didn't answer at all (asleep, Wi-Fi off, rebooting, or stuck on a fatal error screen), or it reset the
 * connection. `delivered` is false only when sys-autopilot provably never had the request: an input whose body never left
 * the PC (see Autopilot.send()). Then nothing was played, and sending it again can't press anything twice.
 */
export class ConsoleUnreachableError extends Error {
    readonly delivered: boolean;
    constructor(message: string, delivered = true) {
        super(message);
        this.delivered = delivered;
    }
}

/**
 * The inputs whose sys-autopilot handler reads the whole JSON body before it plays anything (routes.c: read_json_body
 * first; a missing or cut-off body is a 400 and plays nothing). /input/clear, /controller/attach and /controller/detach
 * don't read theirs.
 */
const INPUT_WITH_BODY = /^\/input\/(tap|hold|release|stick|touch|swipe)$/;

export const UNREACHABLE_HELP =
    'Please look at the console. If it shows "A fatal error occurred" (press POWER to reboot): press POWER, boot back into ' +
    'CFW (Hekate → Launch → CFW (EMUMMC)), and tell Claude what the screen said. If it is asleep or in airplane mode: wake it ' +
    'or turn Wi-Fi back on.';

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

/** Shortest time between two connections to the console (see request()). */
const MIN_GAP_MS = 150;

/** The waits between tries of a request that may be tried again (request()), -1 = no more tries: about a minute in all. */
const BACKOFF_MS = [500, 1_000, 2_000, 4_000, 8_000, 15_000, 30_000, -1];

/**
 * Failures worth another try: the console reset the connection or closed it before answering, or the connection never
 * opened (refused, or the console briefly gone from the network: seen 2026-11-17 after a ~20 s spell of resets, it answered
 * again a few seconds later).
 */
const TRANSIENT = /ECONNRESET|before the answer|ECONNREFUSED|EHOSTUNREACH|ENETUNREACH|EHOSTDOWN/;

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
     * become ConsoleUnreachableError. After a reset, or a connection that never opened (TRANSIENT), the request is tried
     * again, with a warning on stderr, when that can't do anything twice: a request that changes nothing or can safely happen
     * twice (everything except /input and /controller; /input/clear only lets go of everything), or an input the console
     * provably never had (send(): its body never left). An input the console may have played is never sent again: a
     * repeated A could start a game. Once a request has failed after a whole minute of tries, the next ones try only once
     * until one gets through, so a console that is really gone (a fatal error screen) is reported without more minutes.
     */
    request(method: string, url: string, body?: Buffer | string | object, timeoutMs = 30_000): Promise<Response> {
        checkAllowed(method, url);
        const repeatable = !/^\/(input|controller)\//.test(url) || url === '/input/clear';
        const askFirst = method.toUpperCase() === 'POST' && INPUT_WITH_BODY.test(url) && body !== undefined;
        const attempt = async (): Promise<Response> => {
            // WP-03: after bursts of connections (~50 within a second) the console reset every new one for ~10 s, curl's too.
            // So connections start at least MIN_GAP_MS apart, and requests that are safe to repeat back off and try again.
            // WP-03c: during long device runs a spell sometimes lasts over 30 s, so the tries cover about a minute.
            for (const backoffMs of this.gaveUp ? [-1] : BACKOFF_MS) {
                await sleep(Math.max(0, this.lastStart + MIN_GAP_MS - Date.now()));
                this.lastStart = Date.now();
                try {
                    const response = await this.send(method, url, body, timeoutMs, askFirst);
                    this.gaveUp = false;
                    return response;
                } catch (e) {
                    const transient = e instanceof ConsoleUnreachableError && TRANSIENT.test(e.message);
                    const safe = repeatable || (e instanceof ConsoleUnreachableError && !e.delivered);
                    if (transient && safe && backoffMs < 0) this.gaveUp = true;
                    if (!transient || !safe || backoffMs < 0) throw e;
                    const why = repeatable ? '' : ' (the console never had this input)';
                    console.warn(`[autopilot] ${e.message}${why}; again in ${backoffMs} ms`);
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
    private gaveUp = false; // a request failed after all of BACKOFF_MS: the next ones try once until one gets through

    /**
     * Plain HTTP/1.1 over a fresh TCP connection per request. Found in WP-03:
     * - Node's fetch and http modules both reported "connection reset" for GET /files answers that curl and a plain socket
     *   receive whole, so this reads the answer itself and checks that all Content-Length bytes arrived.
     * - With "Connection: close" the console closes every connection itself and has to remember each closed one for a while.
     *   After a few hundred quick requests it started resetting new connections, and recovered by itself after some seconds.
     *   So the request keeps the connection open (HTTP/1.1 default) and this side closes it once the answer is complete.
     *
     * `askFirst` (inputs with a body, 2026-11-17): the console also resets a single new connection now and then, mostly
     * during long runs with the app busy on the network (updates-history: ~20 in 3 minutes). So an input asks first: its
     * head says "Expect: 100-continue" and goes out alone. sys-autopilot answers "100 Continue" only once it has accepted
     * the connection, checked the token and reached the input's handler (http.c http_read_body), and the handler plays
     * nothing before it has the whole body. Only then does the body go out. A reset before that is an error with
     * `delivered: false`: nothing was played, so request() may send it again.
     */
    private send(method: string, url: string, body: Buffer | string | object | undefined, timeoutMs: number,
        askFirst = false): Promise<Response> {
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
            (askFirst ? 'Expect: 100-continue\r\n' : '') +
            '\r\n';
        const where = `sys-autopilot at ${this.config.host} didn't answer ${method} ${url.split('?')[0]}`;

        return new Promise<Response>((resolve, reject) => {
            const chunks: Buffer[] = [];
            let settled = false;
            let bodySent = !askFirst; // without askFirst the body goes out with the head, so the console may have it
            let connectedAt = 0;
            const finish = (error?: Error) => {
                if (settled) return;
                const data = Buffer.concat(chunks);
                if (!bodySent && !error && answerCodes(data)[0] === 100) {
                    bodySent = true; // from here on the console may play the input
                    if (payload.length) socket.write(payload);
                }
                const parsed = parseResponse(data);
                if (parsed) {
                    settled = true;
                    socket.end(); // we close first, so the "recently closed" bookkeeping stays on the PC
                    setTimeout(() => socket.destroy(), 2_000).unref(); // in case the console never closes its side
                    resolve(parsed);
                } else if (error) {
                    settled = true;
                    socket.destroy();
                    const when = connectedAt ? `${Date.now() - connectedAt} ms after connecting` : 'while connecting';
                    const got = data.length ? `, ${data.length} bytes back` : '';
                    // A connection that never opened carried nothing, whatever the request.
                    reject(new ConsoleUnreachableError(`${where}: ${error.message} (${when}${got})`, bodySent && connectedAt > 0));
                }
            };
            const socket = connect({ host: this.config.host, port: this.config.port }, () => {
                connectedAt = Date.now();
                socket.write(head);
                if (bodySent && payload.length) socket.write(payload);
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

/** PUT writes in place and deletes the file if the upload breaks off, so the hash is checked; one retry on a mismatch. */
export async function putVerified(api: Pick<Autopilot, 'putFile' | 'sha256'>, sdPath: string, data: Buffer): Promise<void> {
    const want = createHash('sha256').update(data).digest('hex');
    for (let attempt = 1; attempt <= 2; attempt++) {
        await api.putFile(sdPath, data);
        if ((await api.sha256(sdPath)) === want) return;
    }
    throw new Error(`upload of ${sdPath} failed twice (SHA-256 mismatch)`);
}
