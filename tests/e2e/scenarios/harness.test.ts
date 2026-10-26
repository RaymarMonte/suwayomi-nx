// The harness checking itself (WP-03 acceptance), desktop only:
// - launch({ mode: 'applet' }) is checked by WP-08's applet-guard scenario now (the app refuses applet mode).
// - launch({ memLimitMb }) passes --mem-limit.
// - an app that dies while a scenario waits gives a clear error at once, not a hang until the timeout.

import { expect } from 'vitest';

import { DesktopDriver } from '../drivers/DesktopDriver.ts';
import { AppGoneError } from '../lib/events.ts';
import { scenario } from '../lib/scenario.ts';

scenario(
    'harness-mem-limit',
    async (s) => {
        const start = await s.launch({ memLimitMb: 768 });
        expect(start.memTotal).toBe(768 * 1024 * 1024);
        await s.driver.quit();
    },
    { drivers: ['desktop'] },
);

scenario(
    'harness-app-killed',
    async (s) => {
        await s.launch();
        const driver = s.driver as DesktopDriver;
        const started = Date.now();
        setTimeout(() => driver.killApp('SIGKILL'), 300);
        const err = await s.events.waitForEvent('screen', { name: 'NeverShown' }, { timeoutMs: 60_000 }).catch((e: unknown) => e);
        expect(err).toBeInstanceOf(AppGoneError);
        expect(String(err)).toContain('killed by SIGKILL');
        expect(Date.now() - started).toBeLessThan(5_000); // fails fast, long before the 60 s timeout
        await expect(s.driver.quit()).rejects.toThrow(/did not end cleanly: it killed by SIGKILL/);
    },
    { drivers: ['desktop'] },
);
