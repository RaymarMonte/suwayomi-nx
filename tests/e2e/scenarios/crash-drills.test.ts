// crash-drills (TESTING.md scenario 13, SAFETY.md "Crash drills", WP-03b): every deliberate crash ends in a crash record and a
// clean exit, and the next launch runs in safe mode. What each drill should end in: src/app/crash/README.md "Drills".
// Pass rules per drill (application mode, D21): the app exits, logs/crash-*.txt exists and parses, the next launch says
// safeMode with that record (now filed in logs/crashes/). On the Switch the driver also checks that no new fatal file and no
// Atmosphère crash report appeared, not even a moment later (a crash report would mean creport saw a crash, ours or hbmenu's
// after us), and the HOME menu shows "The software was closed because an error occurred." (D35, WP-03c). One scenario per
// drill, so a failure names it.

import { expect } from 'vitest';

import type { DriverName } from '../drivers/Driver.ts';
import { ENDED_SOFTWARE_CLOSED } from '../drivers/SwitchDriver.ts';
import { isCrashFileName, parseCrashRecord, type CrashRecord } from '../lib/crashrecord.ts';
import { scenario, type Scenario } from '../lib/scenario.ts';

const CLOCK = { x: 0, y: 650, w: 420, h: 70, why: 'clock, battery, Wi-Fi' };
const RECORD_NAME = { x: 400, y: 374, w: 480, h: 30, why: 'the crash record name has the time in it' };

interface Drill {
    name: string;
    ends: 'by-itself' | 'crash-screen'; // the app closes on its own, or shows the crash screen and closes on A
    type: string; // the record's type
    thread: 'main' | 'other' | 'watchdog';
    what?: Partial<Record<DriverName, RegExp>>;
    drivers?: DriverName[];
    exitMs?: number; // how long the app may take to end (default 15 s)
}

const DRILLS: Drill[] = [
    { name: 'null-write', ends: 'by-itself', type: 'cpu_exception', thread: 'main', what: { switch: /null pointer/, desktop: /SIGSEGV.*null pointer/ } },
    { name: 'misaligned', ends: 'by-itself', type: 'cpu_exception', thread: 'main', what: { switch: /alignment fault/, desktop: /SIGBUS/ } },
    { name: 'terminate', ends: 'by-itself', type: 'terminate', thread: 'main' },
    { name: 'gpu-abort', ends: 'by-itself', type: 'gpu_abort', thread: 'main', what: { switch: /GPU error/, desktop: /GPU error/ } },
    { name: 'watchdog', ends: 'by-itself', type: 'watchdog', thread: 'watchdog', exitMs: 30_000 },
    { name: 'worker-null-write', ends: 'crash-screen', type: 'cpu_exception', thread: 'other' },
    { name: 'worker-stack-overflow', ends: 'crash-screen', type: 'cpu_exception', thread: 'other' },
    { name: 'main-loop-exception', ends: 'crash-screen', type: 'main_loop_exception', thread: 'main' },
    // Desktop Linux hands out 2 GB blocks of address space almost without limit, so running out is only real on the Switch.
    { name: 'oom', ends: 'crash-screen', type: 'main_loop_exception', thread: 'main', what: { switch: /bad_alloc/ }, drivers: ['switch'] },
];

async function crashFiles(s: Scenario, dir = 'logs'): Promise<string[]> {
    return (await s.driver.listFiles(dir)).filter(isCrashFileName);
}

/** An earlier run's records would put the drill's first launch into safe mode: one plain launch files them away. */
async function startClean(s: Scenario): Promise<void> {
    if ((await crashFiles(s)).length === 0) return;
    await s.launch();
    await s.driver.quit();
}

/** The one new record in logs/, read and checked. */
async function newRecord(s: Scenario, drill: Drill): Promise<{ file: string; record: CrashRecord }> {
    const files = await crashFiles(s);
    expect(files, 'new crash records in logs/').toHaveLength(1);
    const text = (await s.driver.readFile(`logs/${files[0]}`)).toString('utf8');
    const record = parseCrashRecord(text);
    expect(record, `logs/${files[0]} is a crash record`).not.toBeNull();
    const r = record as CrashRecord;
    expect(r.complete, 'the record has its footer').toBe(true);
    expect(r.type).toBe(drill.type);
    expect(r.thread).toBe(drill.thread);
    expect(r.platform).toBe(s.driver.name);
    expect(r.mode).toBe('application');
    expect(r.buildId).toMatch(/^[0-9a-f]{40}$/);
    const what = drill.what?.[s.driver.name];
    if (what) expect(r.what).toMatch(what);
    expect(r.log.some((l) => l.includes(`drill: ${drill.name}`)), 'the log ring has the drill line').toBe(true);
    return { file: files[0], record: r };
}

/** The launch after a crash: safe mode, the record filed in logs/crashes/, logs/ clean again. */
async function expectSafeModeNext(s: Scenario, file: string): Promise<void> {
    const start = await s.launch();
    expect(start.safeMode, 'app_start.safeMode after a crash').toBe(true);
    expect(start.lastCrash).toBe(`logs/crashes/${file}`);
    expect(await crashFiles(s)).toEqual([]);
    expect(await crashFiles(s, 'logs/crashes')).toContain(file);
    await s.events.waitForEvent('idle');
    await s.driver.quit();
}

for (const drill of DRILLS) {
    scenario(
        `crash-drill-${drill.name}`,
        async (s) => {
            await startClean(s);
            const start = await s.launch({ testConfig: { drill: drill.name } });
            expect(start.safeMode).toBe(false);
            if (drill.ends === 'crash-screen') {
                const shown = await s.events.waitForEvent('screen', { name: 'Crash' }, { timeoutMs: 30_000 });
                if (drill.name === 'main-loop-exception') {
                    // One golden of the crash screen.
                    await s.events.waitForEvent('idle', {}, { from: s.events.events.indexOf(shown) });
                    await s.expectGolden('crash-screen', { masks: [CLOCK, RECORD_NAME] });
                }
                await s.driver.press('A');
            }
            const ended = await s.driver.waitForExit(drill.exitMs ?? 15_000);
            if (s.driver.name === 'desktop') expect(ended, 'how the app ended').toBe('exited with code 1');
            // Switch: every crash ends the process at once (D13 as amended in WP-03c), never back to hbmenu.
            else expect(ended, 'how the app ended').toBe(ENDED_SOFTWARE_CLOSED);
            const { file } = await newRecord(s, drill);
            await expectSafeModeNext(s, file);
        },
        { drivers: drill.drivers, timeoutMs: 4 * 60_000 },
    );
}

// S5: an exit request in the middle of a cache write. The half-written file is dropped, the app quits normally and quickly,
// and leaves no crash record. (A hard kill mid-write leaves the .tmp; the next start deletes it: the desktop part below.)
scenario(
    'crash-drill-cache-write-exit',
    async (s) => {
        await startClean(s);
        await s.launch({ testConfig: { drill: 'cache-write-exit' } });
        await s.events.waitForEvent('drill', { name: 'cache-write-exit', phase: 'writing' }, { timeoutMs: 30_000 });
        expect(await s.driver.listFiles('cache')).toEqual(['drill.bin.tmp']);
        const asked = Date.now();
        await s.driver.requestExit();
        const ended = await s.driver.waitForExit(15_000);
        if (s.driver.name === 'desktop') {
            expect(ended).toBe('exited with code 0');
            expect(Date.now() - asked, 'quit within 1.5 s of the exit request (S5)').toBeLessThan(1_500);
        }
        expect(await s.driver.listFiles('cache')).toEqual([]);
        expect(await crashFiles(s)).toEqual([]);
    },
    { timeoutMs: 4 * 60_000 },
);

scenario(
    'crash-drill-cache-write-killed',
    async (s) => {
        await startClean(s);
        await s.launch({ testConfig: { drill: 'cache-write-exit' } });
        await s.events.waitForEvent('drill', { name: 'cache-write-exit', phase: 'writing' }, { timeoutMs: 30_000 });
        const { DesktopDriver } = await import('../drivers/DesktopDriver.ts');
        (s.driver as InstanceType<typeof DesktopDriver>).killApp('SIGKILL');
        expect(await s.driver.waitForExit(5_000)).toBe('killed by SIGKILL');
        expect(await s.driver.listFiles('cache')).toEqual(['drill.bin.tmp']);
        const start = await s.launch();
        expect(start.safeMode, 'a kill is not a crash').toBe(false);
        expect(await s.driver.listFiles('cache')).toEqual([]);
        await s.driver.quit();
    },
    { drivers: ['desktop'] },
);
