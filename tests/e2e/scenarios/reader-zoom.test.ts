// reader-zoom (WP-13, TESTING.md 6c): zoom and pan in the paged reader, on Alpha (Reading, 3 chapters x 10 JPEG pages of
// 1000 x 1500, right to left). Pinches and double taps are played through the test build's touch channel (lib/touches.ts);
// drags and taps go through the driver like a finger. Checks, on desktop and on the Switch:
// - chapter 1 in landscape: a double tap in the middle zooms to 2 x around the tapped point (x/y), then the page decodes
//   again sharper (decW 1000, sharp; golden "zoom2"); a drag down stops at the page's top (y 0); a played pinch to 3 x; a
//   drag to the right stops at the page's left edge (x 0), the same drag again turns (page 2, back at the fit);
// - a pinch in (3 x) and out snaps back to the fit; a double tap in and one out; a single middle tap opens the menu after
//   the double tap's wait; an edge tap turns at once;
// - "Zoom on page turn: Keep the zoom": a turn keeps 2 x at the reading start (the top); the setting survives a restart;
// - turned right (portrait): the same double tap, pinch and drag on the portrait screen (golden "portrait-zoom3").
// The Switch adds: ZR / ZL steps; a stick pan to the edge and a fresh push there turns; D-pad glides and the edge press;
// not zoomed, a stick held 2 s turns one page; in portrait the D-pad held turns one page; the frame times while gestures
// run (p90 <= 17 ms, none over 34 ms) and sharpMs (p90 <= 350 ms); 50 zoom cycles and 100 turns from zoomed pages with
// memory flat. Needs the test server.

import { expect } from 'vitest';

import { mangaIds, resetChapters } from '../lib/chapters.ts';
import type { AppEvent } from '../lib/events.ts';
import { chooseSetting, openChapter, openManga, readerAfter, stats, swipe, tap, waitForLibrary } from '../lib/reader.ts';
import { driverName, type Scenario, scenario } from '../lib/scenario.ts';
import { LIBRARY_MORE, PORTRAIT, portraitPoint, READER } from '../lib/screens.ts';
import { appServerUrl, optionsFromEnv, SuwayomiClient } from '../lib/suwayomi.ts';
import { doubleTap, pinch, type Point } from '../lib/touches.ts';

const MiB = 1024 * 1024;

type Move = { x1: number; y1: number; x2: number; y2: number };

/** Waits for config.json's save after `mark` and for the app to settle (see reader-portrait). */
async function waitForSave(s: Scenario, mark: number): Promise<void> {
    const saved = await s.events.waitForEvent('config_saved', {}, { from: mark, timeoutMs: 10_000 });
    await s.events.waitForEvent('idle', {}, { from: s.events.events.indexOf(saved) });
}

/** Does `action`, then waits for the first "reader" event after it that `match` accepts, and for the app to settle. */
async function readerWhere(s: Scenario, action: () => Promise<void>, what: string,
    match: (e: AppEvent) => boolean): Promise<AppEvent> {
    const mark = s.events.mark();
    await action();
    const shown = await s.events.waitFor(what, (e) => e.ev === 'reader' && e.shown === 'page' && match(e),
        { from: mark, timeoutMs: 30_000 });
    await s.events.waitForEvent('idle', {}, { from: mark });
    return shown;
}

const zoomNear = (want: number) => (e: AppEvent) => Math.abs(Number(e.zoom) - want) <= 0.1;

/** After a zoom: the sharper decode of the page on screen, drawn (the zoom's settle wait and the decode included). */
async function sharpAfter(s: Scenario, mark: number, fields: Record<string, unknown>): Promise<AppEvent> {
    return s.events.waitForEvent('reader', { shown: 'page', sharp: true, ...fields }, { from: mark, timeoutMs: 30_000 });
}

function lastReader(s: Scenario): AppEvent | undefined {
    return s.events.events.filter((e) => e.ev === 'reader').at(-1);
}

scenario('reader-zoom', async (s) => {
    const url = appServerUrl(s.driver.name);
    const client = new SuwayomiClient(optionsFromEnv());
    const alpha = (await mangaIds(client)).get('Alpha') ?? 0;
    expect(alpha, 'the test server has Alpha: tools/test-server/test-server.sh reset').toBeTruthy();
    const chapters = await resetChapters(client, alpha);
    const ch1 = chapters[0].id;
    const onSwitch = s.driver.name === 'switch';

    try {
        await s.launch({ testConfig: { server: { url } } });
        await waitForLibrary(s);
        await openManga(s, LIBRARY_MORE.reading, 0, 2);
        let shown = await openChapter(s, 2, { shown: 'page' });
        // At the fit: 480 x 720 in the middle of the screen.
        expect(shown).toMatchObject({ chapterId: ch1, page: 0, orientation: 'landscape', w: 480, h: 720, x: 400, y: 0,
            zoom: 1, zoomOnTurn: 'reset', srcW: 1000, srcH: 1500 });
        const fitTextures = Number(shown.texUsed);

        // A double tap in the middle: 2 x around the tapped point. The page (960 x 1440) is still narrower than the screen,
        // so it stays centred sideways (x 160); the tapped height stays put: 200 - 2 x 200 = -200.
        let mark = s.events.mark();
        shown = await readerAfter(s, doubleTap(s, READER.middle), { shown: 'page', page: 0, zoom: 2 });
        expect(shown).toMatchObject({ w: 960, h: 1440, x: 160, y: -200 });
        // Then the page on screen decodes again for 960 x 1440: the whole 1000 x 1500 source (the shrink slack keeps it).
        shown = await sharpAfter(s, mark, { zoom: 2 });
        expect(shown).toMatchObject({ decW: 1000, decH: 1500, x: 160, y: -200 });
        expect(Number(shown.sharpMs), 'sharpMs (the 150 ms settle wait included)').toBeGreaterThanOrEqual(150);
        await s.expectGolden('zoom2');

        // A drag down 400 units moves the page down, but it stops at its top edge (y 0).
        shown = await readerAfter(s, swipe(s, { x1: 640, y1: 250, x2: 640, y2: 650 }), { shown: 'page', page: 0, y: 0 });
        expect(shown).toMatchObject({ zoom: 2, x: 160 });

        // A played pinch (two fingers from 200 to 300 apart around the screen's middle): 3 x. Now 1440 wide: x -80.
        shown = await readerWhere(s, pinch(s, { x: 640, y: 360 }, 200, 300), 'zoom 3 after the pinch', zoomNear(3));
        expect(shown).toMatchObject({ page: 0, w: 1440, h: 2160, x: -80, y: -180 });

        // A drag to the right: the page moves 600 units, but stops at its left edge (x 0). The same drag again at the edge
        // turns (right to left: the next page), back at the fit.
        shown = await readerAfter(s, swipe(s, READER.swipeToRight), { shown: 'page', page: 0, x: 0 });
        expect(zoomNear(3)(shown), `zoom ${shown.zoom}`).toBe(true);
        shown = await readerAfter(s, swipe(s, READER.swipeToRight), { shown: 'page', page: 1 });
        expect(shown).toMatchObject({ zoom: 1, w: 480, x: 400, y: 0 });

        // A pinch in (100 -> 300 apart: 3 x) and back out (300 -> 100): it snaps to the fit.
        shown = await readerWhere(s, pinch(s, { x: 640, y: 360 }, 100, 300), 'zoom 3 after the pinch in', zoomNear(3));
        expect(shown).toMatchObject({ page: 1, x: -80, y: -720 });
        shown = await readerAfter(s, pinch(s, { x: 640, y: 360 }, 300, 100), { shown: 'page', page: 1, zoom: 1 });
        expect(shown).toMatchObject({ w: 480, x: 400, y: 0 });

        // A double tap zooms in, the next one back to the fit.
        await readerAfter(s, doubleTap(s, READER.middle), { shown: 'page', page: 1, zoom: 2 });
        shown = await readerAfter(s, doubleTap(s, READER.middle), { shown: 'page', page: 1, zoom: 1 });
        expect(shown).toMatchObject({ x: 400, y: 0, menu: false });

        // A single tap in the middle opens the menu once the double tap's wait (250 ms) is over; with the menu open a tap
        // closes it at once.
        let started = Date.now();
        await readerAfter(s, tap(s, READER.middle), { menu: true });
        expect(Date.now() - started, 'the menu opens after the double tap wait').toBeGreaterThanOrEqual(250);
        await readerAfter(s, tap(s, READER.middle), { menu: false });
        // An edge tap turns without waiting.
        shown = await readerAfter(s, tap(s, READER.left), { shown: 'page', page: 2 });
        expect(Number(shown.turnMs), 'turnMs of an edge tap').toBeLessThan(onSwitch ? 150 : 100);
        const texturesNow = Number(shown.texUsed);
        expect(texturesNow, 'texUsed at the fit again').toBe(fitTextures);

        // "Zoom on page turn": Keep the zoom (settings entry 5, choice 2 of 2). A turn keeps 2 x, at the reading start: the
        // top (y 0), centred sideways.
        mark = s.events.mark();
        await chooseSetting(s, 5, 1, 2, { zoomOnTurn: 'keep', menu: true });
        await waitForSave(s, mark);
        await readerAfter(s, tap(s, READER.middle), { menu: false });
        await readerAfter(s, doubleTap(s, READER.middle), { shown: 'page', page: 2, zoom: 2 });
        shown = await readerAfter(s, tap(s, READER.left), { shown: 'page', page: 3 });
        expect(shown).toMatchObject({ zoom: 2, w: 960, x: 160, y: 0, zoomOnTurn: 'keep' });
        s.expectNoErrorEvents();
        await s.driver.quit();

        // The setting survives a restart; a chapter opens at the fit.
        await s.launch({ testConfig: { server: { url } }, keepConfig: true });
        await waitForLibrary(s, { items: 2 });
        await openManga(s, null, 0);
        shown = await openChapter(s, 2, { shown: 'page' });
        expect(shown).toMatchObject({ chapterId: ch1, zoomOnTurn: 'keep', zoom: 1 });
        const page = Number(shown.page);

        // Turned right (portrait, settings entry 4, choice 3 of 3): 720 x 1080 at y 100 on the 720 x 1280 reader screen.
        const at = (p: Point) => portraitPoint('portrait-right', p);
        mark = s.events.mark();
        shown = await chooseSetting(s, 4, 2, 3, { shown: 'page', orientation: 'portrait-right', menu: true });
        await waitForSave(s, mark);
        shown = await readerAfter(s, tap(s, at(PORTRAIT.middle)), { shown: 'page', menu: false });
        expect(shown).toMatchObject({ page, w: 720, h: 1080, x: 0, y: 100, zoom: 1 });
        // The double tap at the portrait middle (360, 400): 2 x around it, x 360 - 720 = -360, y 400 - 600 = -200.
        mark = s.events.mark();
        shown = await readerAfter(s, doubleTap(s, PORTRAIT.middle, at), { shown: 'page', zoom: 2 });
        expect(shown).toMatchObject({ page, w: 1440, h: 2160, x: -360, y: -200 });
        // Portrait 2 x wants 1440 x 2160 of a 1000 x 1500 source: sharp at the whole source (the cap is 2560 each way).
        shown = await sharpAfter(s, mark, { zoom: 2 });
        expect(shown).toMatchObject({ decW: 1000, decH: 1500 });
        // A pinch around the portrait screen's middle (360, 640) to 3 x: x 360 - 1080 = -720, y 640 - 1260 = -620.
        shown = await readerWhere(s, pinch(s, { x: 360, y: 640 }, 200, 300, at), 'zoom 3 in portrait', zoomNear(3));
        expect(shown).toMatchObject({ page, w: 2160, h: 3240, x: -720, y: -620 });
        // A drag to the right on the portrait screen (450 units): the page follows, not at an edge yet.
        const right = PORTRAIT.swipeToRight;
        const a = at({ x: right.x1, y: right.y1 });
        const b = at({ x: right.x2, y: right.y2 });
        const turned: Move = { x1: a.x, y1: a.y, x2: b.x, y2: b.y };
        shown = await readerWhere(s, swipe(s, turned), 'the portrait drag', (e) => Number(e.x) > -720);
        // borealis starts a pan only after the finger's first step (12 steps of 37.5 units): the page follows 412..450.
        expect(Number(shown.x), 'x after the drag').toBeGreaterThanOrEqual(-720 + 410);
        expect(Number(shown.x), 'x after the drag').toBeLessThanOrEqual(-720 + 451);
        expect(shown).toMatchObject({ page, y: -620 });
        await s.expectGolden('portrait-zoom3');
        s.expectNoErrorEvents();

        if (onSwitch) {
            await switchControls(s, ch1);
        }
        s.expectNoErrorEvents();
        await s.driver.quit();
    } finally {
        await resetChapters(client, alpha);
    }
}, { timeoutMs: driverName() === 'switch' ? 15 * 60_000 : 4 * 60_000 });

/**
 * The Switch's controller part (the desktop can't press ZL / ZR / L / R / Y or push a stick). Starts in portrait turned
 * right at zoom 3 with "Keep the zoom"; turns back to landscape and "Back to whole page" first.
 */
async function switchControls(s: Scenario, ch1: number): Promise<void> {
    const press = (btn: Parameters<Scenario['driver']['press']>[0], ms?: number) => () => s.driver.press(btn, ms);
    const stick = (x: number, ms: number) => () => s.driver.stick('left', x, 0, ms);

    // Landscape (Y from turned right goes to landscape), "Back to whole page" (entry 5, choice 1 of 2).
    let mark = s.events.mark();
    await readerAfter(s, press('Y'), { shown: 'page', orientation: 'landscape' });
    await chooseSetting(s, 5, 0, 2, { zoomOnTurn: 'reset', menu: true });
    await waitForSave(s, mark);
    let shown = await readerAfter(s, tap(s, READER.middle), { shown: 'page', menu: false });
    expect(shown.zoom).toBe(1);
    let page = Number(shown.page);

    // ZR twice: 1.5 x, then 2 x; ZL back to the fit.
    await readerAfter(s, press('ZR'), { shown: 'page', zoom: 1.5 });
    await readerAfter(s, press('ZR'), { shown: 'page', zoom: 2 });
    await readerAfter(s, press('ZL'), { shown: 'page', zoom: 1.5 });
    shown = await readerAfter(s, press('ZL'), { shown: 'page', zoom: 1 });
    expect(shown).toMatchObject({ x: 400, y: 0 });

    // ZR three times (3 x, 1440 wide around the middle: x -80). The stick pushed left pans to the left edge (x 0) without
    // turning; a fresh push there turns (right to left: the next page), back at the fit.
    for (const zoom of [1.5, 2, 3]) await readerAfter(s, press('ZR'), { shown: 'page', zoom });
    shown = await readerAfter(s, stick(-1, 400), { shown: 'page', page, x: 0 });
    expect(shown.zoom).toBe(3);
    shown = await readerAfter(s, stick(-1, 300), { shown: 'page', page: page + 1 });
    expect(shown.zoom).toBe(1);
    page += 1;

    // The D-pad zoomed: LEFT glides the page right (0.75 of the screen, to the edge here), the next fresh LEFT turns.
    for (const zoom of [1.5, 2, 3]) await readerAfter(s, press('ZR'), { shown: 'page', zoom });
    await readerAfter(s, press('RIGHT'), { shown: 'page', page, x: -160 });
    await readerAfter(s, press('LEFT'), { shown: 'page', page, x: 0 });
    shown = await readerAfter(s, press('LEFT'), { shown: 'page', page: page + 1 });
    expect(shown.zoom).toBe(1);
    page += 1;

    // Not zoomed, the stick held 2 s turns one page, not more.
    mark = s.events.mark();
    await readerAfter(s, stick(-1, 2000), { shown: 'page', page: page + 1 });
    await s.events.waitForEvent('idle', {}, { from: s.events.mark() - 1 });
    expect(s.events.events.slice(mark).some((e) => e.ev === 'reader' && Number(e.page) > page + 1),
        'a held stick turned more than one page').toBe(false);
    page += 1;

    // In portrait (Y: turned right, the way used last) the D-pad held 1.5 s turns one page: turned right DOWN points left.
    await readerAfter(s, press('Y'), { shown: 'page', orientation: 'portrait-right' });
    mark = s.events.mark();
    await readerAfter(s, press('DOWN', 1500), { shown: 'page', page: page + 1 });
    await s.events.waitForEvent('idle', {}, { from: s.events.mark() - 1 });
    expect(s.events.events.slice(mark).some((e) => e.ev === 'reader' && Number(e.page) > page + 1),
        'a held D-pad turned more than one page').toBe(false);
    page += 1;
    await readerAfter(s, press('Y'), { shown: 'page', orientation: 'landscape' });

    // 50 zoom cycles (ZR up to 4 x, ZL back to the fit): memory flat after the fifth, the textures back to the fit's.
    const fit = Number(lastReader(s)?.texUsed);
    const memory: number[] = [];
    for (let i = 0; i < 50; i++) {
        for (const zoom of [1.5, 2, 3, 4]) await readerAfter(s, press('ZR'), { shown: 'page', zoom });
        for (const zoom of [3, 2, 1.5, 1]) shown = await readerAfter(s, press('ZL'), { shown: 'page', zoom });
        expect(Number(shown.texUsed), `texUsed after zoom cycle ${i + 1}`).toBe(fit);
        memory.push(Number(shown.memUsed));
    }
    const cycleGrowth = Math.max(...memory.slice(5)) - memory[5];
    console.log(`[reader-zoom] 50 zoom cycles: memory after the fifth ${(memory[5] / MiB).toFixed(1)} MiB, growth ` +
        `${(cycleGrowth / MiB).toFixed(1)} MiB`);
    expect(cycleGrowth, 'memory growth over zoom cycles 5..50').toBeLessThan(16 * MiB);

    // 100 turns from zoomed pages ("Keep the zoom", 2 x; L and R turn whatever the zoom): memory flat, quick turns.
    mark = s.events.mark();
    await chooseSetting(s, 5, 1, 2, { zoomOnTurn: 'keep', menu: true });
    await waitForSave(s, mark);
    await readerAfter(s, tap(s, READER.middle), { menu: false });
    for (const zoom of [1.5, 2]) await readerAfter(s, press('ZR'), { shown: 'page', zoom });
    const turnMemory: number[] = [];
    const quick: number[] = [];
    let step = page === 9 ? -1 : 1;
    for (let i = 0; i < 100; i++) {
        if (page === 0) step = 1;
        if (page === 9) step = -1;
        page += step;
        // Right to left: L (left) is the next page, R the one before.
        shown = await readerAfter(s, press(step > 0 ? 'L' : 'R'), { shown: 'page', page, chapterId: ch1, zoom: 2 });
        turnMemory.push(Number(shown.memUsed));
        if (shown.ready) quick.push(Number(shown.turnMs));
    }
    const growth = Math.max(...turnMemory.slice(20)) - turnMemory[20];
    const times = stats(quick);
    console.log(`[reader-zoom] ${quick.length} prefetched zoomed turns: median ${times.median} ms, p90 ${times.p90} ms, ` +
        `max ${times.max} ms; memory growth after turn 20 ${(growth / MiB).toFixed(1)} MiB`);
    expect(growth, 'memory growth over zoomed turns 20..100').toBeLessThan(16 * MiB);
    expect(times.p90, 'zoomed page turn time (90th percentile)').toBeLessThan(150);

    // The frame times while gestures ran, and how soon pages got sharp (D17, WP-13 acceptance).
    const gestures = s.events.events.filter((e) => e.ev === 'gesture' && Number(e.frames) >= 3);
    const p90s = stats(gestures.map((e) => Number(e.p90Ms)));
    const worst = Math.max(...gestures.map((e) => Number(e.maxMs)));
    const sharp = stats(s.events.events.filter((e) => e.ev === 'reader' && e.sharpMs !== undefined)
        .map((e) => Number(e.sharpMs)));
    console.log(`[reader-zoom] ${gestures.length} gestures: p90 of their p90s ${p90s.p90} ms, worst frame ${worst} ms; ` +
        `sharpMs median ${sharp.median}, p90 ${sharp.p90}, max ${sharp.max}`);
    expect(p90s.p90, 'frame interval p90 while a gesture runs').toBeLessThanOrEqual(17);
    expect(worst, 'the longest frame while a gesture runs').toBeLessThanOrEqual(34);
    expect(sharp.p90, 'sharpMs p90').toBeLessThanOrEqual(350);
    const pressure = s.events.events.filter((e) => e.ev === 'mem_pressure');
    expect(pressure, 'mem_pressure events').toEqual([]);
}
