// applet-guard (TESTING.md scenario 11, D21, SAFETY.md S12, WP-08): started in applet mode (from the Album), the app writes
// only `launch_refused`, shows the "can't run from the Album yet" text and ends by itself with a normal exit. Then the
// null-pointer drill inside that screen: it must end as a crash record (exit code 1 on desktop), and the next launch in
// application mode runs in safe mode.
// Desktop only for now (--simulate-applet-mode). The console run via the Album, with the owner watching the first drill
// there, is WP-08b (SwitchDriver APPLET_MODE_READY).

import { expect } from 'vitest';

import { parseCrashRecord } from '../lib/crashrecord.ts';
import { scenario } from '../lib/scenario.ts';

scenario(
    'applet-guard',
    async (s) => {
        await s.launch({ mode: 'applet' });
        expect(await s.driver.waitForExit(15_000)).toBe('exited with code 0');
        expect(s.events.events.map((e) => e.ev), 'applet mode writes only launch_refused').toEqual(['launch_refused']);

        const before = await s.driver.listFiles('logs');
        await s.launch({ mode: 'applet', testConfig: { drill: 'null-write' } });
        expect(await s.driver.waitForExit(15_000)).toBe('exited with code 1');
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
    { drivers: ['desktop'] },
);
