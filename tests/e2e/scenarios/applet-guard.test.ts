// applet-guard (TESTING.md scenario 11, D21, D41, SAFETY.md S12, WP-08): started in applet mode (from the Album), the app writes
// only `launch_refused`, shows the "can't run from the Album yet" text and ends with a normal exit (back to hbmenu).
// Desktop (--simulate-applet-mode) also runs the null-pointer drill inside that screen: a crash record, exit code 1, the
// ending note (WP-21, D62: logs/crash-ending.tmp), and the next launch in application mode runs in safe mode.
// The console (opt-in: SNX_APPLET_CONSOLE=1, owner watching): the refusal screen, with a golden. The drill there only with
// SNX_APPLET_DRILL=1 too: that is WP-21's proof P1 (D62), at most 3 tries in all, each with the owner watching, because until it
// passes a crash from the Album can be a system fatal (D41: the HOME menu stopped the system when the Album's process ended
// without telling it). P1 passes when the crash ends at the HOME menu with no new fatal or crash report, our record parses,
// the ending note says command 10 went through, and the Album opens again.

import { writeFileSync } from 'node:fs';
import { join } from 'node:path';

import { expect } from 'vitest';

import { ENDED_AT_HOME, ENDED_IN_HBMENU, ENDED_SOFTWARE_CLOSED } from '../drivers/SwitchDriver.ts';
import { parseCrashRecord } from '../lib/crashrecord.ts';
import { scenario, type Scenario } from '../lib/scenario.ts';

// The guard writes nothing once its screen shows (S12), so there is no event to wait for: give the console a moment to draw.
const SCREEN_DRAW_MS = 2_000;
const CONSOLE_DRILL = process.env.SNX_APPLET_DRILL === '1'; // WP-21 P1 (D62), owner watching
const ENDING_WATCH_MS = 15_000; // the crash ending waits at most 5 s for the system (crashExitWaitMs): watch three times that
const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

/**
 * P1's record of what the console showed after the crash: a screenshot about every second (ending-<ms>.jpg, ms since the drill
 * started) and ending-timeline.txt. Only looks: never presses anything.
 */
async function watchTheEnding(s: Scenario, startedAt: number): Promise<void> {
    const lines: string[] = [];
    while (Date.now() - startedAt < ENDING_WATCH_MS) {
        const ms = Date.now() - startedAt;
        try {
            const shot = await s.driver.screenshot();
            const name = `ending-${String(ms).padStart(5, '0')}ms.jpg`;
            writeFileSync(join(s.out, name), shot);
            lines.push(`${ms} ms: ${name}`);
        } catch (e) {
            lines.push(`${ms} ms: no screenshot (${(e as Error).message})`);
        }
        await sleep(1_000);
    }
    writeFileSync(join(s.out, 'ending-timeline.txt'), lines.join('\n') + '\n');
}

/**
 * After the Album closed by itself the HOME menu keeps its cursor on the Album icon, where the driver's "HOME menu" check
 * (the first tile selected) doesn't match. HOME brings it to the first tile when nothing runs (the driver's recover() does the
 * same). P1's first try (2026-11-21) used UP then 12 × LEFT: the cursor ended on the third tile, so the check never matched.
 * Never A here: on the HOME menu A starts a game.
 */
async function backToTheFirstTile(s: Scenario): Promise<string> {
    try {
        return await s.driver.waitForExit(4_000);
    } catch {
        await s.driver.press('HOME');
        await sleep(1_500);
        return s.driver.waitForExit(20_000);
    }
}

scenario(
    'applet-guard',
    async (s) => {
        const desktop = s.driver.name === 'desktop';
        await s.launch({ mode: 'applet' });
        if (!desktop) {
            await sleep(SCREEN_DRAW_MS);
            await s.expectGolden('refused');
            await s.driver.press('A'); // any button closes it
        }
        expect(await s.driver.waitForExit(20_000)).toBe(desktop ? 'exited with code 0' : ENDED_IN_HBMENU);
        expect(s.events.events.map((e) => e.ev), 'applet mode writes only launch_refused').toEqual(['launch_refused']);
        if (!desktop && !CONSOLE_DRILL) return; // the console's drill is P1 only (D41, D62)

        // The null-pointer drill inside the guard's screen. Desktop: always. Console: P1 (WP-21, D62), owner watching.
        const before = await s.driver.listFiles('logs');
        const drillAt = Date.now();
        await s.launch({ mode: 'applet', testConfig: { drill: 'null-write' } });
        if (desktop) {
            expect(await s.driver.waitForExit(30_000)).toBe('exited with code 1');
        } else {
            await watchTheEnding(s, drillAt);
            // D62: the Album's process told the system it was leaving, so the HOME menu carries on (no "press POWER"). A note
            // from the HOME menu is allowed (P1 records which). waitForExit also compares the crash folders: a new fatal file
            // stops everything (STOP-console-fatal.txt), a new crash report fails the test.
            const ended = await backToTheFirstTile(s);
            writeFileSync(join(s.out, 'ended.txt'), `${ended}\n`);
            expect([ENDED_AT_HOME, ENDED_SOFTWARE_CLOSED]).toContain(ended);
        }
        expect(s.events.events.map((e) => e.ev)).toEqual(['launch_refused', 'drill']);
        const records = (await s.driver.listFiles('logs')).filter((n) => /^crash-.*\.txt$/.test(n) && !before.includes(n));
        expect(records.length, 'one new crash record').toBe(1);
        const record = parseCrashRecord((await s.driver.readFile(`logs/${records[0]}`)).toString('utf8'));
        expect(record).toMatchObject({ type: 'cpu_exception', thread: 'main', mode: 'applet' });

        // The ending note (D62): which steps the crash ending reached.
        const ending = (await s.driver.readFile('logs/crash-ending.tmp')).toString('utf8');
        writeFileSync(join(s.out, 'crash-ending.txt'), ending);
        if (desktop) {
            expect(ending).toMatch(/ms: desktop: no system to tell; _exit with code: 0x00000001 \(1\)\n$/);
        } else {
            // The new ending ran. Its other lines (the call's result, a wait that ran out) are P1's findings for D62, not
            // pass/fail: the pass is the HOME menu carrying on (above).
            expect(ending).toMatch(/ms: sending ExitProcessAndReturn \(command 10\), applet type: 0x00000002 \(2\)\n/);
            console.log(`[applet-guard] the crash ending's steps:\n${ending}`);

            // P1's last check: the Album opens again (the refusal screen, A, back to hbmenu), with the console still answering.
            await s.launch({ mode: 'applet' });
            await sleep(SCREEN_DRAW_MS);
            await s.driver.press('A');
            expect(await s.driver.waitForExit(20_000)).toBe(ENDED_IN_HBMENU);
            expect(s.events.events.map((e) => e.ev)).toEqual(['launch_refused']);
            return;
        }

        const start = await s.launch();
        expect(start.safeMode, 'the next launch runs in safe mode').toBe(true);
        await s.events.waitForEvent('idle');
        await s.driver.quit();
    },
    { drivers: process.env.SNX_APPLET_CONSOLE === '1' ? ['desktop', 'switch'] : ['desktop'], timeoutMs: 300_000 },
);
