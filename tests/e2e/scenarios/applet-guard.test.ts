// applet-guard (TESTING.md scenario 11, D21, D41, SAFETY.md S12, WP-08): started in applet mode (from the Album), the app writes
// only `launch_refused`, shows the "can't run from the Album yet" text and ends with a normal exit (back to hbmenu).
// Desktop (--simulate-applet-mode) also runs the null-pointer drill inside that screen: a crash record, exit code 1, and the
// next launch in application mode runs in safe mode.
// The console (opt-in: SNX_APPLET_CONSOLE=1, owner watching): the refusal screen only, with a golden. Never the drill there
// (D41): a crash in applet mode is always a system fatal, because the HOME menu stops the system when the Album's process
// ends abnormally, however our crash handler ends it. Verified once on the console in WP-08 (2026-10-26).

import { expect } from 'vitest';

import { ENDED_IN_HBMENU } from '../drivers/SwitchDriver.ts';
import { parseCrashRecord } from '../lib/crashrecord.ts';
import { scenario } from '../lib/scenario.ts';

// The guard writes nothing once its screen shows (S12), so there is no event to wait for: give the console a moment to draw.
const SCREEN_DRAW_MS = 2_000;

scenario(
    'applet-guard',
    async (s) => {
        const desktop = s.driver.name === 'desktop';
        await s.launch({ mode: 'applet' });
        if (!desktop) {
            await new Promise((resolve) => setTimeout(resolve, SCREEN_DRAW_MS));
            await s.expectGolden('refused');
            await s.driver.press('A'); // any button closes it
        }
        expect(await s.driver.waitForExit(20_000)).toBe(desktop ? 'exited with code 0' : ENDED_IN_HBMENU);
        expect(s.events.events.map((e) => e.ev), 'applet mode writes only launch_refused').toEqual(['launch_refused']);
        if (!desktop) return; // no drill on the console (D41)

        // The null-pointer drill inside the guard's screen (desktop only).
        const before = await s.driver.listFiles('logs');
        await s.launch({ mode: 'applet', testConfig: { drill: 'null-write' } });
        expect(await s.driver.waitForExit(30_000)).toBe('exited with code 1');
        expect(s.events.events.map((e) => e.ev)).toEqual(['launch_refused', 'drill']);
        const records = (await s.driver.listFiles('logs')).filter((n) => /^crash-.*\.txt$/.test(n) && !before.includes(n));
        expect(records.length, 'one new crash record').toBe(1);
        const record = parseCrashRecord((await s.driver.readFile(`logs/${records[0]}`)).toString('utf8'));
        expect(record).toMatchObject({ type: 'cpu_exception', thread: 'main', mode: 'applet' });

        const start = await s.launch();
        expect(start.safeMode, 'the next launch runs in safe mode').toBe(true);
        await s.events.waitForEvent('idle');
        await s.driver.quit();
    },
    { drivers: process.env.SNX_APPLET_CONSOLE === '1' ? ['desktop', 'switch'] : ['desktop'], timeoutMs: 300_000 },
);
