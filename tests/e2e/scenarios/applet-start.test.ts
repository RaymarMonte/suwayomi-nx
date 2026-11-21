// applet-start (TESTING.md scenario 11, WP-21, D62; replaces WP-08's applet-guard): the app opened from the Album.
// - It starts with the Lite profile, and the tip comes first at every start: OK closes it and the app carries on (the main
//   screen connects); the next start shows it again; "Don't show again" saves general.appletTip false in config.json, so the
//   next start has none; Settings -> "Show the Album tip" (shown from the Album only) brings it back for the start after.
//   About says "Opened from the Album (applet mode) · Lite profile".
// - Desktop only: with too little free memory (--mem-limit far below Lite's 160 MiB) the low-memory screen (launch_refused,
//   reason low_memory, exit code 0). The null-pointer drill inside it leaves a crash record, the ending note and exit code 1,
//   and the next start from the Album runs in safe mode.
// The console too since WP-21's P2 passed (D62).

import { writeFileSync } from 'node:fs';
import { join } from 'node:path';

import { expect } from 'vitest';

import { parseCrashRecord } from '../lib/crashrecord.ts';
import type { AppEvent } from '../lib/events.ts';
import { scenario, type Scenario } from '../lib/scenario.ts';
import { CLOCK_MASK, tab } from '../lib/screens.ts';
import { appServerUrl } from '../lib/suwayomi.ts';

const TINY_MB = 100; // free at start is below Lite's liteMinFreeBytes (160 MiB): the low-memory screen

const isEv = (name: string, fields: Record<string, unknown> = {}) => (e: AppEvent) =>
    e.ev === name && Object.entries(fields).every(([k, v]) => e[k] === v);
const lastOf = (s: Scenario, ev: string) => s.events.events.filter((e) => e.ev === ev).at(-1);

/** The view id in the latest "focus" event ("" when none). */
function focusedView(s: Scenario): string {
    return String(lastOf(s, 'focus')?.view ?? '');
}

/** Presses `button` until the focused view's id contains `part` (at most `max` presses). */
async function pressUntilFocus(s: Scenario, button: 'UP' | 'DOWN' | 'RIGHT', part: string, max = 12): Promise<void> {
    for (let i = 0; i < max && !focusedView(s).includes(part); i++) {
        const mark = s.events.mark();
        await s.driver.press(button);
        const moved = await s.events.waitForEvent('focus', {}, { from: mark, timeoutMs: 3_000 }).catch(() => undefined);
        if (moved) await s.events.waitForEvent('idle', {}, { from: mark });
    }
    expect(focusedView(s), `${button}: the focus reaches ${part}`).toContain(part);
}

/** A start from the Album with config.json deciding about the tip (the test config names only the server). */
async function startFromAlbum(s: Scenario, keepConfig: boolean): Promise<AppEvent> {
    const start = await s.launch({
        mode: 'applet',
        keepConfig,
        appletTipFromConfig: true,
        testConfig: { server: { url: appServerUrl(s.driver.name) } },
    });
    expect(start.profile).toBe('lite');
    return start;
}

/** The tip is on screen: `answer` closes it; then the main screen connects (the tip comes before the server check). */
async function answerTip(s: Scenario, answer: 'ok' | 'hidden', golden?: string): Promise<void> {
    await s.events.waitForEvent('applet_tip', { what: 'shown' }, { from: 0, timeoutMs: 20_000 });
    expect(s.events.events.some((e) => e.ev === 'server_check'), 'no server check before the tip closes').toBe(false);
    await s.events.waitForEvent('idle', {}, { from: 0 });
    if (golden) await s.expectGolden(golden, { masks: [CLOCK_MASK] });
    const mark = s.events.mark();
    if (answer === 'hidden') await s.driver.press('RIGHT'); // the second button, "Don't show again"
    await s.driver.press('A');
    await s.events.waitForEvent('applet_tip', { what: answer }, { from: mark, timeoutMs: 10_000 });
    const check = await s.events.waitForEvent('server_check', { where: 'main' }, { from: mark, timeoutMs: 30_000 });
    expect(check.ok).toBe(true);
}

scenario(
    'applet-start',
    async (s) => {
        const desktop = s.driver.name === 'desktop';

        // 1. The first start from the Album: the tip, OK. 2. The next start: the tip again, Don't show again.
        await startFromAlbum(s, false);
        await answerTip(s, 'ok', 'tip');
        await s.driver.quit();
        await startFromAlbum(s, true);
        await answerTip(s, 'hidden');
        await s.driver.quit();
        const saved = JSON.parse((await s.driver.readFile('config.json')).toString('utf8'));
        expect(saved.general?.appletTip, 'config.json after "Don\'t show again"').toBe(false);

        // 3. No tip; Settings -> About from the Album, and the switch brings the tip back.
        await startFromAlbum(s, true);
        await s.events.waitForEvent('server_check', { where: 'main' }, { timeoutMs: 30_000 });
        let mark = s.events.mark();
        await s.events.waitForEvent('library', {}, { from: 0, timeoutMs: 30_000 }).catch(() => undefined);
        let settings: AppEvent | undefined;
        for (let i = 0; i < 3 && !settings; i++) {
            await new Promise((r) => setTimeout(r, 1_000)); // the main screen settles after the library loads
            mark = s.events.mark();
            await s.driver.touch(tab('Settings').x, tab('Settings').y);
            settings = await s.events.waitForEvent('screen', { name: 'Settings' }, { from: mark, timeoutMs: 5_000 }).catch(() => undefined);
        }
        if (!settings) throw new Error('the Settings tab did not open after 3 taps');
        expect(settings.params).toMatchObject({ mode: 'applet', profile: 'lite' });
        await s.events.waitForEvent('idle', {}, { from: mark });
        await pressUntilFocus(s, 'RIGHT', 'settings/', 3);
        await pressUntilFocus(s, 'DOWN', 'settings/applet_tip', 30);
        mark = s.events.mark();
        await s.driver.press('A');
        await s.events.waitForEvent('setting', { name: 'appletTip', value: true }, { from: mark });
        await s.events.waitForEvent('idle', {}, { from: mark });
        await s.expectGolden('settings-about', { masks: ABOUT_MASKS });
        expect(s.events.events.filter((e) => e.ev === 'applet_tip'), 'no tip at this start').toEqual([]);
        s.expectNoErrorEvents();
        await s.driver.quit();

        // 4. Back on: the tip again at the next start.
        await startFromAlbum(s, true);
        await answerTip(s, 'ok');
        s.expectNoErrorEvents();
        await s.driver.quit();
        if (!desktop) return; // the console's memory can't be made small: the low-memory screen is tested on the PC

        // 5. The low-memory screen: only launch_refused, exit code 0.
        await s.launch({ mode: 'applet', memLimitMb: TINY_MB, refused: true });
        expect(await s.driver.waitForExit(20_000)).toBe('exited with code 0');
        expect(s.events.events.map((e) => e.ev)).toEqual(['launch_refused']);

        // 6. The null-pointer drill inside it: a crash record, the ending note, exit code 1; the next start in safe mode.
        const before = await s.driver.listFiles('logs');
        await s.launch({ mode: 'applet', memLimitMb: TINY_MB, refused: true, testConfig: { drill: 'null-write' } });
        expect(await s.driver.waitForExit(30_000)).toBe('exited with code 1');
        expect(s.events.events.map((e) => e.ev)).toEqual(['launch_refused', 'drill']);
        const records = (await s.driver.listFiles('logs')).filter((n) => /^crash-.*\.txt$/.test(n) && !before.includes(n));
        expect(records.length, 'one new crash record').toBe(1);
        const record = parseCrashRecord((await s.driver.readFile(`logs/${records[0]}`)).toString('utf8'));
        expect(record).toMatchObject({ type: 'cpu_exception', thread: 'main', mode: 'applet' });
        const ending = (await s.driver.readFile('logs/crash-ending.tmp')).toString('utf8');
        writeFileSync(join(s.out, 'crash-ending.txt'), ending);
        expect(ending).toMatch(/ms: desktop: no system to tell; _exit with code: 0x00000001 \(1\)\n$/);

        const next = await s.launch({ mode: 'applet' });
        expect(next.safeMode, 'the next start from the Album runs in safe mode').toBe(true);
        await s.events.waitForEvent('idle');
        await s.driver.quit();
    },
    { timeoutMs: 300_000 },
);

// Settings -> About's values that change from run to run (desktop 1280x720, the About section scrolled into view).
const ABOUT_MASKS = [
    CLOCK_MASK,
    { x: 900, y: 410, w: 320, h: 60, why: 'memory in use' },
    { x: 1080, y: 555, w: 140, h: 55, why: 'the number of crash logs' },
];
