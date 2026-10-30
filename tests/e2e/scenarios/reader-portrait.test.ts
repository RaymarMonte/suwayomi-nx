// reader-portrait (WP-22, TESTING.md): the reader with the console held sideways, on Alpha (Reading, 3 chapters x 10 JPEG
// pages, right to left by default). Checks, on desktop and on the Switch:
// - Y (desktop: the menu's Rotate) turns chapter 1's first page to portrait, turned left (the first time): a 720 x 1280
//   reader screen, the page fitted to it (720 x 1080), golden "portrait-left-page"; a tap on the left edge, the turned
//   D-pad (UP points left) and a swipe all turn the right way; the menu (golden "portrait-left-menu"); its Rotate button
//   by touch goes back to landscape;
// - fit height, then the reader settings' Orientation entry chooses turned right: the menu and page (goldens
//   "portrait-right-menu", "portrait-right-page"); the 853 x 1280 page moves with a drag instead of turning (golden
//   "portrait-right-moved"), then the same drag turns; the turned D-pad (DOWN points left), L keeps its side (Switch), the
//   end screen (golden "portrait-right-end"); turned left again on the end screen (golden "portrait-left-end");
// - the menu's Close by touch closes the reader; a restart keeps the orientation, then 100 turns with the turned D-pad
//   (memory flat, turn times: p90 < 150 ms on the console); desktop only: --simulate-docked reads landscape and keeps
//   the saved choice.
// Touch points are given on the portrait screen and turned into landscape ones (portraitPoint). Needs the test server.

import { expect } from 'vitest';

import { mangaIds, resetChapters } from '../lib/chapters.ts';
import type { AppEvent } from '../lib/events.ts';
import { chooseSetting, openChapter, openManga, readerAfter, stats, swipe, tap, waitForLibrary } from '../lib/reader.ts';
import { driverName, type Scenario, scenario } from '../lib/scenario.ts';
import { LIBRARY_MORE, type Orientation, PORTRAIT, portraitPoint, READER } from '../lib/screens.ts';
import { appServerUrl, optionsFromEnv, SuwayomiClient } from '../lib/suwayomi.ts';

const MiB = 1024 * 1024;

type Move = { x1: number; y1: number; x2: number; y2: number };

function turnedMove(way: Orientation, m: Move): Move {
    const a = portraitPoint(way, { x: m.x1, y: m.y1 });
    const b = portraitPoint(way, { x: m.x2, y: m.y2 });
    return { x1: a.x, y1: a.y, x2: b.x, y2: b.y };
}

/**
 * Waits for config.json's save after `mark` (a setting changed: it is saved about 2 s later) and for the app to settle.
 * The save holds the main loop for a moment, and borealis reads the mouse once a frame: a desktop tap (100 ms) during it
 * would be lost.
 */
async function waitForSave(s: Scenario, mark: number): Promise<void> {
    const saved = await s.events.waitForEvent('config_saved', {}, { from: mark, timeoutMs: 10_000 });
    await s.events.waitForEvent('idle', {}, { from: s.events.events.indexOf(saved) });
}

function lastReader(s: Scenario): AppEvent | undefined {
    return s.events.events.filter((e) => e.ev === 'reader').at(-1);
}

scenario('reader-portrait', async (s) => {
    const url = appServerUrl(s.driver.name);
    const client = new SuwayomiClient(optionsFromEnv());
    const alpha = (await mangaIds(client)).get('Alpha') ?? 0;
    expect(alpha, 'the test server has Alpha: tools/test-server/test-server.sh reset').toBeTruthy();
    const chapters = await resetChapters(client, alpha);
    const ch1 = chapters[0].id;

    try {
        await s.launch({ testConfig: { server: { url } } });
        await waitForLibrary(s);
        await openManga(s, LIBRARY_MORE.reading, 0, 2);
        let shown = await openChapter(s, 2, { shown: 'page' });
        expect(shown).toMatchObject({ chapterId: ch1, page: 0, orientation: 'landscape', orientationSetting: 'landscape',
            w: 480, h: 720, screenW: 1280, screenH: 720 });

        // Y: portrait, turned left the first time. The page fills the portrait screen's width.
        let way: Orientation = 'portrait-left';
        let at = (p: { x: number; y: number }) => portraitPoint(way, p);
        let mark = s.events.mark();
        if (s.driver.name === 'switch') {
            shown = await readerAfter(s, () => s.driver.press('Y'), { shown: 'page', orientation: way });
        } else {
            // The desktop can't press Y (borealis maps only A, B, X and the D-pad there): the menu's Rotate button.
            await readerAfter(s, tap(s, READER.middle), { menu: true });
            await readerAfter(s, tap(s, READER.rotate), { shown: 'page', orientation: way, menu: true });
            shown = await readerAfter(s, tap(s, at(PORTRAIT.middle)), { shown: 'page', orientation: way, menu: false });
        }
        expect(shown).toMatchObject({ page: 0, orientationSetting: way, screenW: 720, screenH: 1280, w: 720, h: 1080,
            menu: false });
        await waitForSave(s, mark);
        await s.expectGolden('portrait-left-page');

        // Right to left: the left edge is the next page; the D-pad's UP points left now; a swipe to the left goes back.
        await readerAfter(s, tap(s, at(PORTRAIT.left)), { shown: 'page', page: 1 });
        await readerAfter(s, () => s.driver.press('UP'), { shown: 'page', page: 2 });
        await readerAfter(s, swipe(s, turnedMove(way, PORTRAIT.swipeToLeft)), { shown: 'page', page: 1 });
        await readerAfter(s, tap(s, at(PORTRAIT.right)), { shown: 'page', page: 0 });

        // The menu, upright; its Rotate button (by touch, at its portrait place) goes back to landscape.
        await readerAfter(s, tap(s, at(PORTRAIT.middle)), { menu: true });
        await s.expectGolden('portrait-left-menu');
        mark = s.events.mark();
        shown = await readerAfter(s, tap(s, at(PORTRAIT.rotate)), { shown: 'page', orientation: 'landscape', menu: true });
        expect(shown).toMatchObject({ orientationSetting: 'landscape', w: 480, h: 720 });
        await waitForSave(s, mark);

        // Fit height (settings entry 1, choice 2 of 3), then the Orientation entry (4): turned right (choice 2 of 3). The
        // 2 : 3 page is then 853 x 1280: wider than the portrait screen, so it can be moved sideways.
        mark = s.events.mark();
        await chooseSetting(s, 1, 2, 3, { fit: 'height', menu: true });
        await waitForSave(s, mark);
        way = 'portrait-right';
        at = (p) => portraitPoint(way, p);
        mark = s.events.mark();
        shown = await chooseSetting(s, 4, 2, 3, { shown: 'page', orientation: way, menu: true });
        expect(shown).toMatchObject({ orientationSetting: way, fit: 'height', w: 853, h: 1280 });
        await waitForSave(s, mark);
        await s.expectGolden('portrait-right-menu');
        await readerAfter(s, tap(s, at(PORTRAIT.middle)), { menu: false });
        await s.expectGolden('portrait-right-page');

        // A drag to the right moves the page (right to left it starts at its right edge) instead of turning; the same drag
        // again, now at the left edge, turns to the next page.
        expect(lastReader(s)).toMatchObject({ page: 0, x: -133, y: 0 });  // 720 - 853: its right edge at the screen's
        await readerAfter(s, swipe(s, turnedMove(way, PORTRAIT.swipeToRight)), { shown: 'page', page: 0, x: 0, y: 0 });
        await s.expectGolden('portrait-right-moved');
        await readerAfter(s, swipe(s, turnedMove(way, PORTRAIT.swipeToRight)), { shown: 'page', page: 1 });

        // Turned right the D-pad's DOWN points left (next, right to left); on the Switch L keeps its side (left: next).
        for (let page = 2; page <= 9; page++) {
            const key = s.driver.name === 'switch' && page > 2 ? 'L' : 'DOWN';
            await readerAfter(s, () => s.driver.press(key), { shown: 'page', page });
        }
        await readerAfter(s, tap(s, at(PORTRAIT.left)), { shown: 'end' });
        await s.expectGolden('portrait-right-end');

        // Turned left on the end screen: the menu's Rotate goes to landscape, then the settings (Rotate would go to the
        // portrait way used last, turned right).
        await readerAfter(s, tap(s, at(PORTRAIT.middle)), { menu: true });
        mark = s.events.mark();
        await readerAfter(s, tap(s, at(PORTRAIT.rotate)), { shown: 'end', orientation: 'landscape', menu: true });
        await waitForSave(s, mark);
        way = 'portrait-left';
        at = (p) => portraitPoint(way, p);
        mark = s.events.mark();
        await chooseSetting(s, 4, 1, 3, { shown: 'end', orientation: way, menu: true });
        await waitForSave(s, mark);
        await readerAfter(s, tap(s, at(PORTRAIT.middle)), { menu: false });
        await s.expectGolden('portrait-left-end');

        // The menu's Close by touch closes the reader.
        await readerAfter(s, tap(s, at(PORTRAIT.middle)), { menu: true });
        mark = s.events.mark();
        await s.driver.touch(at(PORTRAIT.close).x, at(PORTRAIT.close).y);
        await s.events.waitForEvent('manga', { state: 'ready', complete: true }, { from: mark, timeoutMs: 30_000 });
        await s.events.waitForEvent('idle', {}, { from: mark });
        s.expectNoErrorEvents();
        await s.driver.quit();

        // A restart keeps the orientation (config.json).
        await s.launch({ testConfig: { server: { url } }, keepConfig: true });
        await waitForLibrary(s, { items: 2 });
        await openManga(s, null, 0);
        shown = await openChapter(s, 2, { shown: 'page' });
        expect(shown).toMatchObject({ orientation: 'portrait-left', orientationSetting: 'portrait-left', fit: 'height',
            w: 853, h: 1280 });

        // 100 page turns back and forth through chapter 1 with the turned D-pad (turned left: UP = next, DOWN = back): memory
        // stays flat, turns to prefetched pages are quick (the console: p90 < 150 ms, as in reader-paged, D17).
        const memory: number[] = [];
        const quick: number[] = [];
        let page = Number(shown.page);
        let step = page === 9 ? -1 : 1;
        for (let i = 0; i < 100; i++) {
            if (page === 0) step = 1;
            if (page === 9) step = -1;
            page += step;
            shown = await readerAfter(s, () => s.driver.press(step > 0 ? 'UP' : 'DOWN'),
                { shown: 'page', page, chapterId: ch1, orientation: 'portrait-left' });
            memory.push(Number(shown.memUsed));
            if (shown.ready) quick.push(Number(shown.turnMs));
        }
        const growth = Math.max(...memory.slice(20)) - memory[20];
        const times = stats(quick);
        console.log(`[reader-portrait] ${quick.length} prefetched turns: median ${times.median} ms, p90 ${times.p90} ms, ` +
            `max ${times.max} ms; memory after turn 20: ${(memory[20] / MiB).toFixed(1)} MiB, growth ` +
            `${(growth / MiB).toFixed(1)} MiB`);
        expect(growth, 'memory growth over turns 20..100').toBeLessThan(16 * MiB);
        expect(quick.length, 'turns to a page that was ready').toBeGreaterThan(90);
        expect(times.p90, 'page turn time (90th percentile)').toBeLessThan(s.driver.name === 'switch' ? 150 : 1000);
        s.expectNoErrorEvents();
        await s.driver.quit();

        if (s.driver.name === 'desktop') {
            // Docked (desktop: --simulate-docked) the reader is landscape; the saved choice stays.
            await s.launch({ testConfig: { server: { url } }, keepConfig: true, args: ['--simulate-docked'] });
            await waitForLibrary(s, { items: 2 });
            await openManga(s, null, 0);
            shown = await openChapter(s, 2, { shown: 'page' });
            expect(shown).toMatchObject({ orientation: 'landscape', orientationSetting: 'portrait-left', w: 480 });
            // No Rotate button docked: a tap where it is in handheld only opens and closes the menu.
            await readerAfter(s, tap(s, READER.middle), { menu: true });
            shown = await readerAfter(s, tap(s, READER.rotate), { menu: false });
            expect(shown).toMatchObject({ orientation: 'landscape', orientationSetting: 'portrait-left' });
            s.expectNoErrorEvents();
            await s.driver.quit();
        }
    } finally {
        await resetChapters(client, alpha);
    }
}, { timeoutMs: driverName() === 'switch' ? 10 * 60_000 : 4 * 60_000 });
