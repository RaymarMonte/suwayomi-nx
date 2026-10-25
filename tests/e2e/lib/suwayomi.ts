// A small GraphQL client for the Suwayomi test server, used by the test-server tools (seed, dump-schema, record-fixtures)
// and later by the E2E harness to check what the app did (docs/TESTING.md). Handles the three auth modes we test.

import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

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

/** Windows' address as seen from WSL: the default gateway (WSL 2 on Windows 10 is behind NAT). */
function windowsHost(): string {
    try {
        for (const line of readFileSync('/proc/net/route', 'utf8').split('\n').slice(1)) {
            const f = line.trim().split(/\s+/);
            if (f[1] === '00000000' && f[2]) {
                const hex = f[2];
                return [3, 2, 1, 0].map((i) => parseInt(hex.slice(i * 2, i * 2 + 2), 16)).reverse().join('.');
            }
        }
    } catch {
        // not Linux: fall through
    }
    return '127.0.0.1';
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
