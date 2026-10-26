// auth-matrix (TESTING.md scenario 3, WP-08): the app against the test server in its three login modes (D38).
//   none      connects.
//   basic     connects with the right password; a wrong one is refused and the setup screen opens with the error.
//   ui_login  logs in with the saved password (no tokens yet), renews the access token after it expires (the test server's
//             JWT lives 20 s), logs out from Settings (the login screen opens), logs in again there, and a restart uses the
//             saved tokens.
// It restarts the test server per mode (tools/test-server/test-server.sh start --auth <mode>) and leaves it running with
// auth none. The login screen's password comes from the test config (the Switch can't type in a test).

import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

import { expect } from 'vitest';

import type { AppEvent } from '../lib/events.ts';
import type { Scenario } from '../lib/scenario.ts';
import { scenario } from '../lib/scenario.ts';
import { LOGIN, settingsRow, tab } from '../lib/screens.ts';
import { appServerUrl } from '../lib/suwayomi.ts';

const SCRIPT = fileURLToPath(new URL('../../../tools/test-server/test-server.sh', import.meta.url));
const CONFIG = JSON.parse(readFileSync(fileURLToPath(new URL('../../../tools/test-server/server.json', import.meta.url)), 'utf8')) as {
    username: string;
    password: string;
};
const JWT_EXPIRY_MS = 20_000; // server.json: the test server's access token lives 20 s

// The test server's stop is a hard stop (tools/test-server/README.md). Now and then the next start then dies with a broken
// server config (seen in WP-08: "null cannot be cast to ... ServerConfig"); `reset` repairs that (fresh data, same fixtures).
function serverMode(auth: 'none' | 'basic' | 'ui_login'): void {
    execFileSync(SCRIPT, ['stop'], { stdio: 'ignore' });
    try {
        execFileSync(SCRIPT, ['start', '--auth', auth], { stdio: 'ignore', timeout: 120_000 });
    } catch {
        console.warn(`auth-matrix: the test server didn't start with auth ${auth}; resetting it and trying once more`);
        execFileSync(SCRIPT, ['reset'], { stdio: 'ignore', timeout: 300_000 });
        execFileSync(SCRIPT, ['start', '--auth', auth], { stdio: 'ignore', timeout: 120_000 });
    }
}

async function connected(s: Scenario, from = 0): Promise<AppEvent> {
    const check = await s.events.waitForEvent('server_check', { where: 'main' }, { from, timeoutMs: 30_000 });
    expect(check.ok, JSON.stringify(check)).toBe(true);
    return check;
}

async function touchAt(s: Scenario, at: { x: number; y: number }): Promise<number> {
    const mark = s.events.mark();
    await s.driver.touch(at.x, at.y);
    return mark;
}

scenario(
    'auth-matrix',
    async (s) => {
        const url = appServerUrl(s.driver.name);
        const login = { username: CONFIG.username, password: CONFIG.password };
        try {
            // none
            serverMode('none');
            await s.launch({ testConfig: { server: { url } } });
            await connected(s);
            s.expectNoErrorEvents();
            await s.driver.quit();

            // basic: the right password, then a wrong one
            serverMode('basic');
            await s.launch({ testConfig: { server: { url, auth: 'basic', ...login } } });
            await connected(s);
            s.expectNoErrorEvents();
            await s.driver.quit();

            await s.launch({ testConfig: { server: { url, auth: 'basic', username: login.username, password: 'wrong' } } });
            const refused = await s.events.waitForEvent('server_check', { where: 'main' }, { timeoutMs: 30_000 });
            expect(refused).toMatchObject({ ok: false, kind: 'auth' });
            await s.events.waitForEvent('screen', { name: 'Setup' }, { from: s.events.events.indexOf(refused) });
            await s.driver.quit();

            // ui_login: no tokens yet, so the client logs in with the saved password, then renews the expired token
            serverMode('ui_login');
            await s.launch({ testConfig: { server: { url, auth: 'ui_login', ...login } } });
            const first = await connected(s);
            expect(s.events.events.some((e) => e.ev === 'login' && e.hasRefreshToken === true), 'logged in').toBe(true);
            await s.events.waitForEvent('idle', {}, { from: s.events.events.indexOf(first) });
            // The one wait that is about time, not an event: the access token has to expire on the server.
            await new Promise((resolve) => setTimeout(resolve, JWT_EXPIRY_MS + 2_000));
            let mark = await touchAt(s, tab('Settings'));
            await s.events.waitForEvent('screen', { name: 'Settings' }, { from: mark });
            mark = await touchAt(s, settingsRow('serverVersion', true)); // a tap checks the server again
            await s.events.waitForEvent('login', { hasAccessToken: true }, { from: mark, timeoutMs: 30_000 }); // renewed
            await connected(s, mark);

            // Log out -> the login screen (password from the test config) -> Log in -> back on the main screen
            mark = await touchAt(s, settingsRow('logout', true));
            await s.events.waitForEvent('screen', { name: 'Login' }, { from: mark });
            await s.events.waitForEvent('idle', {}, { from: mark });
            mark = await touchAt(s, LOGIN.login);
            const loggedIn = await s.events.waitForEvent('login_screen', {}, { from: mark, timeoutMs: 30_000 });
            expect(loggedIn.ok, JSON.stringify(loggedIn)).toBe(true);
            await connected(s, mark);
            s.expectNoErrorEvents();
            await s.driver.quit();

            // A restart uses the saved tokens: no password needed (the test config is gone).
            await s.launch({ keepConfig: true });
            await connected(s);
            s.expectNoErrorEvents();
            await s.driver.quit();
        } finally {
            serverMode('none'); // what the other scenarios expect
        }
    },
    { timeoutMs: 600_000 },
);
