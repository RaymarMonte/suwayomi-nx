// low-memory (TESTING.md scenario 12, WP-08, desktop only): started with --mem-limit, the app's memory in use is over the
// memory-pressure line (85 %) from the start, so the guard shrinks the caches (`mem_pressure`) and the app carries on: the
// main screen connects, Settings -> About shows the mode and the limited memory, and it quits cleanly. Needs the test server.

import { expect } from 'vitest';

import { scenario } from '../lib/scenario.ts';
import { tab } from '../lib/screens.ts';
import { appServerUrl } from '../lib/suwayomi.ts';

const LIMIT_MB = 48; // the desktop app uses about 50 MiB with Mesa's software renderer

scenario(
    'low-memory',
    async (s) => {
        const start = await s.launch({ memLimitMb: LIMIT_MB, testConfig: { server: { url: appServerUrl(s.driver.name) } } });
        expect(start.memTotal).toBe(LIMIT_MB * 1024 * 1024);
        const pressure = await s.events.waitForEvent('mem_pressure', { action: 'shrink_caches' }, { timeoutMs: 20_000 });
        expect(pressure.usedPct).toBeGreaterThanOrEqual(85);
        const check = await s.events.waitForEvent('server_check', { where: 'main' }, { timeoutMs: 30_000 });
        expect(check.ok).toBe(true);

        const mark = s.events.mark();
        const settings = tab('Settings');
        await s.driver.touch(settings.x, settings.y);
        const screen = await s.events.waitForEvent('screen', { name: 'Settings' }, { from: mark });
        expect(screen.params).toMatchObject({ mode: 'application', profile: 'full', memTotal: LIMIT_MB * 1024 * 1024 });
        await s.events.waitForEvent('idle', {}, { from: mark });
        s.expectNoErrorEvents();
        await s.driver.quit();
    },
    { drivers: ['desktop'] },
);
