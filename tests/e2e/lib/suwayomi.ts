// A small GraphQL client for the Suwayomi test server, used by the test-server tools (seed, dump-schema, record-fixtures)
// and later by the E2E harness to check what the app did (docs/TESTING.md). Handles the three auth modes we test.

import { execFileSync } from 'node:child_process';
import { copyFileSync, cpSync, existsSync, mkdirSync, readFileSync, rmSync, utimesSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { chapterFolder, chapterTime, EPSILON, EPSILON_HELD_BACK_CHAPTER, seriesTime } from '../../../tools/gen-fixtures/fixtures.ts';

export type AuthMode = 'none' | 'basic' | 'ui_login';

export interface ServerOptions {
    url: string; // base URL, e.g. http://172.27.64.1:4568
    auth: AuthMode;
    username: string;
    password: string;
}

export interface GraphQLResponse<T = unknown> {
    status: number; // HTTP status
    body: { data?: T; errors?: { message: string; path?: (string | number)[] }[] } | null;
    text: string; // raw response text
}

export class GraphQLError extends Error {
    readonly response: GraphQLResponse;
    constructor(message: string, response: GraphQLResponse) {
        super(message);
        this.response = response;
    }
}

const CONFIG_PATH = fileURLToPath(new URL('../../../tools/test-server/server.json', import.meta.url));

/**
 * The default gateway from /proc/net/route text, or null. The kernel writes it as little-endian hex: "01401BAC" is 172.27.64.1
 * (bytes read last to first; WP-05 fixed a second reversal that turned it into 1.64.27.172).
 */
export function gatewayFromRoute(routeText: string): string | null {
    for (const line of routeText.split('\n').slice(1)) {
        const f = line.trim().split(/\s+/);
        if (f[1] === '00000000' && f[2] && /^[0-9A-Fa-f]{8}$/.test(f[2])) {
            const hex = f[2];
            return [3, 2, 1, 0].map((i) => parseInt(hex.slice(i * 2, i * 2 + 2), 16)).join('.');
        }
    }
    return null;
}

/** Windows' address as seen from WSL: the default gateway (WSL 2 on Windows 10 is behind NAT). */
function windowsHost(): string {
    try {
        return gatewayFromRoute(readFileSync('/proc/net/route', 'utf8')) ?? '127.0.0.1';
    } catch {
        return '127.0.0.1'; // not Linux
    }
}

/** Server settings from the environment (SNX_SERVER_URL, SNX_AUTH, SNX_USER, SNX_PASS), else from server.json. */
export function optionsFromEnv(env: NodeJS.ProcessEnv = process.env): ServerOptions {
    const cfg = JSON.parse(readFileSync(CONFIG_PATH, 'utf8')) as { port: number; username: string; password: string };
    const auth = (env.SNX_AUTH ?? 'none') as AuthMode;
    if (!['none', 'basic', 'ui_login'].includes(auth)) throw new Error(`SNX_AUTH must be none, basic or ui_login, not ${auth}`);
    return {
        url: (env.SNX_SERVER_URL ?? `http://${windowsHost()}:${cfg.port}`).replace(/\/+$/, ''),
        auth,
        username: env.SNX_USER ?? cfg.username,
        password: env.SNX_PASS ?? cfg.password,
    };
}

export class SuwayomiClient {
    readonly options: ServerOptions;
    private accessToken: string | undefined;

    constructor(options: ServerOptions) {
        this.options = options;
    }

    /**
     * Sends one GraphQL request and returns the HTTP answer as it is, errors included (record-fixtures keeps those).
     * anonymous: send no credentials at all (to see what a client that isn't logged in gets).
     */
    async request<T = unknown>(query: string, variables?: Record<string, unknown>, anonymous = false): Promise<GraphQLResponse<T>> {
        if (anonymous) return this.send<T>(query, variables, true);
        let res = await this.send<T>(query, variables);
        if (this.options.auth === 'ui_login' && isUnauthorized(res)) {
            // No token yet, or it expired (the test server's tokens live 20 s): log in again once.
            await this.login();
            res = await this.send<T>(query, variables);
        }
        return res;
    }

    /** Sends one GraphQL request and returns `data`. Throws GraphQLError on HTTP or GraphQL errors. */
    async query<T = unknown>(query: string, variables?: Record<string, unknown>): Promise<T> {
        const res = await this.request<T>(query, variables);
        if (res.status !== 200 || !res.body || res.body.errors?.length || res.body.data === undefined) {
            const why = res.body?.errors?.map((e) => e.message).join('; ') || res.text.slice(0, 300);
            throw new GraphQLError(`GraphQL request failed (HTTP ${res.status}): ${why}`, res);
        }
        return res.body.data;
    }

    /** ui_login: logs in (without a token: the server refuses `login` from a client that is already logged in). */
    async login(): Promise<{ accessToken: string; refreshToken: string }> {
        this.accessToken = undefined;
        const res = await this.send<{ login: { accessToken: string; refreshToken: string } }>(
            'mutation Login($u: String!, $p: String!) { login(input: { username: $u, password: $p }) { accessToken refreshToken } }',
            { u: this.options.username, p: this.options.password }, true,
        );
        const tokens = res.body?.data?.login;
        if (!tokens?.accessToken) throw new GraphQLError(`login failed (HTTP ${res.status}): ${res.text.slice(0, 300)}`, res);
        this.accessToken = tokens.accessToken;
        return tokens;
    }

    private async send<T>(query: string, variables?: Record<string, unknown>, anonymous = false): Promise<GraphQLResponse<T>> {
        const headers: Record<string, string> = { 'content-type': 'application/json', accept: 'application/json' };
        if (anonymous) {
            // no credentials
        } else if (this.options.auth === 'basic') {
            headers.authorization = 'Basic ' + Buffer.from(`${this.options.username}:${this.options.password}`).toString('base64');
        } else if (this.options.auth === 'ui_login' && this.accessToken) {
            headers.authorization = `Bearer ${this.accessToken}`;
        }
        const response = await fetch(`${this.options.url}/api/graphql`, {
            method: 'POST',
            headers,
            body: JSON.stringify(variables ? { query, variables } : { query }),
            signal: AbortSignal.timeout(60_000),
        });
        const text = await response.text();
        let body: GraphQLResponse<T>['body'] = null;
        try {
            body = JSON.parse(text);
        } catch {
            // not JSON (e.g. a 401 page): keep the text
        }
        return { status: response.status, body, text };
    }
}

function isUnauthorized(res: GraphQLResponse): boolean {
    if (res.status === 401) return true;
    return (res.body?.errors ?? []).some((e) => /unauthori[sz]ed|forbidden|not logged in/i.test(e.message));
}

/** Waits until the server answers aboutServer (max timeoutMs). */
export async function waitForServer(client: SuwayomiClient, timeoutMs = 90_000): Promise<{ name: string; version: string }> {
    const until = Date.now() + timeoutMs;
    let last: unknown;
    while (Date.now() < until) {
        try {
            const data = await client.query<{ aboutServer: { name: string; version: string } }>('{ aboutServer { name version } }');
            return data.aboutServer;
        } catch (e) {
            last = e;
            await new Promise((r) => setTimeout(r, 500));
        }
    }
    throw new Error(`server at ${client.options.url} didn't answer within ${timeoutMs / 1000} s: ${String(last)}`);
}

/**
 * The server URL the app under test should use (WP-05). SNX_APP_SERVER_URL wins. Otherwise the desktop app uses the same URL as
 * the harness (Windows host as seen from WSL), and the Switch the PC's first LAN address from `test-server.sh status`, so a
 * changed DHCP address needs no edit.
 */
export function appServerUrl(driver: 'desktop' | 'switch', env: NodeJS.ProcessEnv = process.env): string {
    if (env.SNX_APP_SERVER_URL) return env.SNX_APP_SERVER_URL.replace(/\/+$/, '');
    if (driver === 'desktop') return optionsFromEnv(env).url;
    const script = fileURLToPath(new URL('../../../tools/test-server/test-server.sh', import.meta.url));
    const status = JSON.parse(execFileSync(script, ['status'], { encoding: 'utf8' })) as { lan?: string[] | string; port?: number };
    const lan = Array.isArray(status.lan) ? status.lan : status.lan ? [status.lan] : [];
    const cfg = JSON.parse(readFileSync(CONFIG_PATH, 'utf8')) as { port: number };
    if (lan.length === 0) throw new Error('test-server.sh status lists no LAN address: set SNX_APP_SERVER_URL=http://<pc-ip>:4568');
    return `http://${lan[0]}:${status.port ?? cfg.port}`;
}

const SERVER_SCRIPT = fileURLToPath(new URL('../../../tools/test-server/test-server.sh', import.meta.url));

/** Stops the test server (a hard stop: the app sees connections refused). auth-matrix and offline-errors (WP-12). */
export function stopServer(): void {
    execFileSync(SERVER_SCRIPT, ['stop'], { stdio: 'ignore' });
}

/**
 * Starts the test server with login mode `auth` (restarts it when it runs with another one) and waits until it answers.
 * After a hard stop the next start now and then dies with a broken server config (seen in WP-08: "null cannot be cast to
 * ... ServerConfig"); `reset` repairs that (fresh data, same fixtures, so reading progress is gone), then it starts once more.
 */
export function startServer(auth: AuthMode = 'none'): void {
    try {
        execFileSync(SERVER_SCRIPT, ['start', '--auth', auth], { stdio: 'ignore', timeout: 120_000 });
    } catch {
        console.warn(`the test server didn't start with auth ${auth}; resetting it and trying once more`);
        execFileSync(SERVER_SCRIPT, ['reset'], { stdio: 'ignore', timeout: 300_000 });
        execFileSync(SERVER_SCRIPT, ['start', '--auth', auth], { stdio: 'ignore', timeout: 120_000 });
    }
}

// ---- Epsilon Weekly: the manga for "a library update finds a new chapter" (WP-15) ----

/** The test server's files (<repo>/.test-server/, git-ignored). WSL and Windows see the same folder (D29). */
const TEST_SERVER_DIR = fileURLToPath(new URL('../../../.test-server/', import.meta.url));

/** Epsilon Weekly's manga id after seeding: the seed adds it after the 156 older ones. Its chapters are 460 and 461. */
export const EPSILON_MANGA_ID = 157;

/** The server's Local Source folder (`<server root>/local`): one folder per manga. */
export function serverLocalFolder(): string {
    return join(TEST_SERVER_DIR, 'root', 'local');
}

/**
 * Copies one chapter of Epsilon Weekly from the pristine copy (<repo>/.test-server/fixtures-extra, written by gen-fixtures)
 * into the server's local/ folder, with the fixed folder time: the server uses a chapter folder's time as its upload date.
 * Also makes the manga folder (with details.json) when it isn't there yet. Does nothing for a chapter that is already there.
 * The seed uses this for chapters 1 and 2.
 */
export function copyEpsilonChapter(chapter: number): void {
    const from = join(TEST_SERVER_DIR, 'fixtures-extra', EPSILON.title);
    const to = join(serverLocalFolder(), EPSILON.title);
    if (!existsSync(from)) throw new Error(`${from} is missing: run tools/test-server/test-server.sh reset (it generates the fixtures)`);
    if (!existsSync(join(to, 'details.json'))) {
        mkdirSync(to, { recursive: true });
        copyFileSync(join(from, 'details.json'), join(to, 'details.json'));
        utimesSync(join(to, 'details.json'), seriesTime(EPSILON), seriesTime(EPSILON));
        utimesSync(to, seriesTime(EPSILON), seriesTime(EPSILON));
    }
    const target = join(to, chapterFolder(chapter));
    if (existsSync(target)) return;
    cpSync(join(from, chapterFolder(chapter)), target, { recursive: true });
    utimesSync(target, chapterTime(EPSILON, chapter), chapterTime(EPSILON, chapter)); // after the copy: copying changes the time
}

/**
 * Puts the held-back chapter 3 of Epsilon Weekly into the server's local/ folder. The server only notices it at the next
 * library update (`updateLibrary(input: {})`) or chapter fetch. Safe to call when it is already there.
 */
export function addNewChapter(): void {
    copyEpsilonChapter(EPSILON_HELD_BACK_CHAPTER);
}

/**
 * Undoes addNewChapter(): deletes chapter 3's folder and asks the server to re-read Epsilon's chapter list, so the server forgets
 * the chapter (chapter 462 is gone again). Safe to call when the chapter isn't there.
 */
export async function removeNewChapter(client: SuwayomiClient = new SuwayomiClient(optionsFromEnv())): Promise<void> {
    rmSync(join(serverLocalFolder(), EPSILON.title, chapterFolder(EPSILON_HELD_BACK_CHAPTER)), { recursive: true, force: true });
    await client.query(
        'mutation F($id: Int!) { fetchMangaAndChapters(input: { id: $id, fetchManga: true, fetchChapters: true }) { manga { id } } }',
        { id: EPSILON_MANGA_ID });
}
