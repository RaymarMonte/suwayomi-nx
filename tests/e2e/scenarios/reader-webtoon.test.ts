// reader-webtoon (WP-13b, TESTING.md 6f): the webtoon strip, on Gamma Webtoon (Long series: 2 chapters x 3 WebP pages of
// 800 x 12000, each 8 segments of 1500 px with their own page code; no genres, so the shape rule picks the strip) and Alpha
// (Reading: 1000 x 1500 JPEG pages, switched to the strip by hand). Zoom in the strip: WP-13d. Checks:
// - Gamma ch1 from the details screen opens as a strip by itself (auto "shape"), turned left on a fresh config (the strip's
//   own orientation), 720 wide, the first page's top at the screen's top; config.json then has Gamma as "vertical";
// - a played drag moves the strip by the drag; a played flick carries on past the finger and stops; a touch during a flick
//   stops it (and is no tap); a bottom tap glides on 960 units (3/4 of the screen), a top tap back (two quick taps: twice as
//   far, WP-13d), a middle tap opens the menu (after the double tap's 250 ms wait);
// - zoom (WP-13d): a double tap in the middle zooms to 2 x around the tapped point (Gamma's 800-wide decode is sharp
//   enough: nothing decodes again); a zoomed drag pans sideways; a pinch goes to 3 x and back, the point between the
//   fingers staying put; a zoomed drag scrolls half as far (the zoom stays); a double tap goes back to 1 x;
// - after the moves, the page codes on screen are where the event's page and spot (the middle line) say;
// - golden "webtoon-seam": the seam of pages 1 and 2 at the middle line (no gap); in page 2 the server gets lastPageRead 1
//   and the spot note; at the end of ch1 the band (golden "webtoon-band"), ch1 read on the server, then ch2 is the chapter
//   read (chapterId); scrolling back up shows ch1's last segment;
// - closed half way down ch2's page 2, Continue opens it with the same segment at the same place;
// - Alpha ch1 meanwhile opens paged in landscape (each reader keeps its own orientation); in the strip, Rotate -> landscape,
//   640 wide, centred (golden "webtoon-landscape"); "Strip width: Full" -> 1280, and Gamma opens like that next time;
// - Alpha switched to "Vertical strip" by hand is a strip at the same page; back to "Left to right" shows that page;
// - texUsed stays <= 64 MiB, no mem_pressure or error event.
// Needs the test server; Gamma's and Alpha's chapters (and Gamma's spot notes) are reset before and after.

import { expect } from 'vitest';

import { chaptersOf, clearStripSpots, mangaIds, resetChapters, stripSpots } from '../lib/chapters.ts';
import type { AppEvent } from '../lib/events.ts';
import type { FoundPageCode } from '../lib/pagecode.ts';
import { chooseSetting, codesOnScreen, expectPage, middleCode, openChapter, openManga, readerAfter, stats, tap,
    waitForLibrary } from '../lib/reader.ts';
import { driverName, type Scenario, scenario } from '../lib/scenario.ts';
import { LIBRARY_MORE, MANGA, type Orientation, PORTRAIT, portraitPoint, READER } from '../lib/screens.ts';
import { appServerUrl, optionsFromEnv, SuwayomiClient } from '../lib/suwayomi.ts';
import { doubleTap, dragFrames, flickFrames, FRAME_MS, pinch, playTouches, type Point,
    type TouchFrame } from '../lib/touches.ts';

const MiB = 1024 * 1024;

// Gamma's pages (tools/gen-fixtures): 800 x 12000 px, 8 segments of 1500 px. A segment's page code (pagecode.ts, u = 5 px) is
// a 240 px block 15 px below the segment's top, its red marker's middle 60 px below it.
const GAMMA = { series: 3, width: 800, height: 12000, segment: 1500, segments: 8, blockTop: 15, block: 240, marker: 60 };
const GAMMA_PAGES = 3;

// The strip held turned left, in reader units: 720 x 1280, bands a third of the screen, pages 12000 * 720 / 800 tall.
// Opened at ch1 (nothing above it): band, ch1's three pages, band, ch2's three pages, band.
const BAND = 1280 / 3;
const PAGE = 10800;
const ch1Page = (i: number) => BAND + i * PAGE;  // page i's top on the strip
const CH1_END = ch1Page(3);
const ch2Page = (i: number) => CH1_END + BAND + i * PAGE;

/** The reader's screen: how it is held, its size in reader units, and a reader point -> the screen point to touch. */
type View = { way: Orientation; w: number; h: number; at: (p: Point) => Point };
const PORTRAIT_LEFT: View = { way: 'portrait-left', w: 720, h: 1280, at: (p) => portraitPoint('portrait-left', p) };
const LANDSCAPE: View = { way: 'landscape', w: 1280, h: 720, at: (p) => p };
// The middle of the landscape screen: the strip's tap zones go across (the top and bottom 30 % glide), so READER.middle (high
// up, y 200) would glide back there. The page reader's middle zone is the middle 40 % across: this point is in it too.
const MIDDLE = { x: 640, y: 360 };

function lastReader(s: Scenario): AppEvent | undefined {
    return s.events.events.filter((e) => e.ev === 'reader').at(-1);
}

// borealis begins a drag once the finger has moved more than 6 px, and counts it from there (so the strip doesn't jump when
// a drag begins). A held drag first moves the finger this far, then the drag itself.
const DRAG_SLOP = 20;

/** A drag that rests 100 ms before the finger lifts (no flick): the strip moves `by` units on (+) or back (-). */
function heldDrag(s: Scenario, view: View, by: number): () => Promise<void> {
    const down = { x: view.w / 2, y: by > 0 ? view.h - 80 : 80 };
    const from = { x: down.x, y: down.y - Math.sign(by) * DRAG_SLOP };
    const frames: TouchFrame[] = [
        { ms: 0, fingers: [{ id: 0, ...view.at(down) }] },
        ...dragFrames(from, { x: from.x, y: from.y - by }, 250, view.at)
            .map((f) => ({ ms: f.ms + FRAME_MS, fingers: f.fingers })),
    ];
    const end = frames[frames.length - 1];
    frames.push({ ms: end.ms + 100, fingers: end.fingers }, { ms: end.ms + 100 + FRAME_MS, fingers: [] });
    return async () => { await playTouches(s, frames); };
}

/** WP-13d: a sideways drag (held at the end, no flick) of `by` units to the right (+) or left (-) across the middle. */
function sideDrag(s: Scenario, view: View, by: number): () => Promise<void> {
    const down = { x: by > 0 ? 120 : view.w - 120, y: view.h / 2 };
    const from = { x: down.x + Math.sign(by) * DRAG_SLOP, y: down.y };
    const frames: TouchFrame[] = [
        { ms: 0, fingers: [{ id: 0, ...view.at(down) }] },
        ...dragFrames(from, { x: from.x + by, y: from.y }, 250, view.at)
            .map((f) => ({ ms: f.ms + FRAME_MS, fingers: f.fingers })),
    ];
    const end = frames[frames.length - 1];
    frames.push({ ms: end.ms + 100, fingers: end.fingers }, { ms: end.ms + 100 + FRAME_MS, fingers: [] });
    return async () => { await playTouches(s, frames); };
}

/** A flick: the finger moves `by` units in 120 ms and lifts while moving; then, `touchAfterMs` later, a touch if given. */
function flickBy(s: Scenario, view: View, by: number, touchAfterMs?: number): () => Promise<void> {
    const from = { x: view.w / 2, y: by > 0 ? view.h - 80 : 80 };
    const frames: TouchFrame[] = flickFrames(from, { x: from.x, y: from.y - by }, 120, view.at);
    if (touchAfterMs !== undefined) {
        const lift = frames[frames.length - 1].ms;
        const p = view.at({ x: view.w / 2, y: view.h - 120 });  // the bottom tap zone: a tap there would glide on
        frames.push({ ms: lift + touchAfterMs, fingers: [{ id: 0, x: p.x, y: p.y }] },
            { ms: lift + touchAfterMs + 60, fingers: [] });
    }
    return async () => { await playTouches(s, frames); };
}

/**
 * Moves the strip until its top (the event's whole units) is `target` rounded: flicks while far, then held drags. A drag
 * moves exactly its distance, so the last one lands on it (goldens need the same place every run).
 */
async function scrollTo(s: Scenario, view: View, target: number): Promise<AppEvent> {
    let ev = lastReader(s) as AppEvent;
    for (let i = 0; i < 60; i++) {
        const d = Math.round(target) - Number(ev.top);
        if (d === 0) return ev;
        const most = view.h - 160 - DRAG_SLOP;
        let action: () => Promise<void>;
        if (Math.abs(d) > 4 * view.h) {
            action = flickBy(s, view, Math.sign(d) * most * 0.8);  // goes about 3 screens: never past the target
        } else if (Math.abs(d) < 40) {
            action = heldDrag(s, view, d - Math.sign(d) * 100);  // too short for a drag: back 100 past it first
        } else {
            action = heldDrag(s, view, Math.sign(d) * Math.min(Math.abs(d), most));
        }
        ev = await readerAfter(s, action, { mode: 'strip' });
    }
    throw new Error(`the strip didn't get to top ${target} (it is at ${ev.top})`);
}

/**
 * Expects Gamma's page codes on screen where the "reader" event `ev` says: the page under the middle line has its top spot x
 * its height above that line. Every code found must be at its segment's place (within 3 px), and every code wholly on
 * screen must be found. Returns the codes found, top to bottom.
 */
async function expectCodes(s: Scenario, ev: AppEvent, view: View): Promise<FoundPageCode[]> {
    const { codes, height } = await codesOnScreen(s, view.way);
    const scale = Number(ev.stripW) / GAMMA.width;
    const pageH = GAMMA.height * scale;
    const pageTop = height / 2 - Number(ev.spot) * pageH;
    const places: { page: number; segment: number; y: number; whole: boolean }[] = [];
    for (let page = 0; page < GAMMA_PAGES; page++) {
        for (let segment = 0; segment < GAMMA.segments; segment++) {
            const top = pageTop + (page - Number(ev.page)) * pageH + segment * GAMMA.segment * scale;
            const blockTop = top + GAMMA.blockTop * scale;
            places.push({ page: page + 1, segment, y: top + GAMMA.marker * scale,
                whole: blockTop >= 4 && blockTop + GAMMA.block * scale <= height - 4 });
        }
    }
    const found = codes.map((c) => `p${c.page}s${c.segment}@${Math.round(c.y)}`).join(' ');
    const where = `codes on screen (${found || 'none'}) for page ${ev.page}, spot ${ev.spot}, top ${ev.top}`;
    for (const code of codes) {
        expect(code, where).toMatchObject({ series: GAMMA.series, chapter: Number(ev.chapter) });
        const place = places.find((p) => p.page === code.page && p.segment === code.segment);
        expect(place ? Math.abs(place.y - code.y) : Infinity, where).toBeLessThanOrEqual(3);
    }
    for (const place of places.filter((p) => p.whole)) {
        expect(codes.some((c) => c.page === place.page && c.segment === place.segment),
            `${where}: p${place.page}s${place.segment} should show`).toBe(true);
    }
    return codes;
}

/** Waits for config.json's save after `mark` (about 2 s after a change) and for the app to settle (reader-portrait). */
async function waitForSave(s: Scenario, mark: number): Promise<void> {
    const saved = await s.events.waitForEvent('config_saved', {}, { from: mark, timeoutMs: 10_000 });
    await s.events.waitForEvent('idle', {}, { from: s.events.events.indexOf(saved) });
}

/**
 * Reads config.json after each save from `mark` on (up to 3) until `check` passes: an earlier change (the library tab) may
 * be saved first, about 2 s after it, and the change looked for in the next save.
 */
async function waitForConfig(s: Scenario, mark: number, check: (config: { reader: { manga: unknown[] } }) => boolean): Promise<void> {
    let from = mark;
    for (let i = 0; i < 3; i++) {
        const saved = await s.events.waitForEvent('config_saved', {}, { from, timeoutMs: 10_000 });
        const config = JSON.parse((await s.driver.readFile('config.json')).toString('utf8'));
        if (check(config)) {
            await s.events.waitForEvent('idle', {}, { from: s.events.events.indexOf(saved) });
            return;
        }
        from = s.events.events.indexOf(saved) + 1;
    }
    throw new Error('config.json never had what the step saved');
}

/** The strip's settings (5 entries, the reader in landscape): opens the menu unless open, entry `entry`, choice `choice`. */
async function stripSetting(s: Scenario, entry: number, choice: number, count: number,
    fields: Record<string, unknown>): Promise<AppEvent> {
    if (!lastReader(s)?.menu) {
        await readerAfter(s, tap(s, MIDDLE), { menu: true });
    }
    let mark = s.events.mark();
    await s.driver.touch(READER.settings.x, READER.settings.y);
    await s.events.waitForEvent('idle', {}, { from: mark });
    const first = MANGA.menuEntry(entry, 5);
    mark = s.events.mark();
    await s.driver.touch(first.x, first.y);
    await s.events.waitForEvent('idle', {}, { from: mark });
    const second = MANGA.menuEntry(choice, count);
    return readerAfter(s, () => s.driver.touch(second.x, second.y), fields);
}

/** The menu's Close (opening the menu first): back on the details screen once it has settled. */
async function closeReader(s: Scenario, view: View): Promise<void> {
    const middle = view.way === 'landscape' ? MIDDLE : view.at(PORTRAIT.middle);
    const close = view.way === 'landscape' ? READER.close : view.at(PORTRAIT.close);
    if (!lastReader(s)?.menu) {
        await readerAfter(s, tap(s, middle), { menu: true });
    }
    const mark = s.events.mark();
    await s.driver.touch(close.x, close.y);
    await s.events.waitForEvent('manga', { state: 'ready', complete: true }, { from: mark, timeoutMs: 30_000 });
    await s.events.waitForEvent('idle', {}, { from: mark });
}

/** From the details screen back to the library, then the manga at `cover` of a category tab. */
async function switchManga(s: Scenario, category: Point, cover: number): Promise<void> {
    const mark = s.events.mark();
    await s.driver.press('B');
    await s.events.waitForEvent('idle', {}, { from: mark });
    await openManga(s, category, cover, category === LIBRARY_MORE.reading ? 3 : 2); // Reading: Alpha, Beta RTL, Epsilon Weekly
}

const near = (a: unknown, b: number, within = 2) => Math.abs(Number(a) - b) <= within;

// The whole strip opened at ch1 with ch2 joined below, turned left: its top goes from 0 to this.
const STRIP_END = ch2Page(3) + BAND - 1280;

/**
 * The Switch's part (the desktop can't push a stick or press Y, L or R). From the details screen: Gamma ch1 (read, so it
 * opens at its start) in landscape full width (remembered), Y turns it left. A half stick push, the held D-pad (LEFT points
 * down the strip turned left) and L back; ZR / ZL zoom steps with a sideways
 * D-pad glide (WP-13d); then three passes down and up the whole strip at a full push: memory grows by less
 * than 16 MiB after the first pass, and from the second pass on nothing waits (`waitFrames` 0). Frame intervals of every
 * gesture with 3 frames or more (drags, flicks, glides, scrolls): p90 <= 17 ms, none over 34 ms.
 */
async function switchControls(s: Scenario, g1: number): Promise<void> {
    const push = (x: number, ms: number) => () => s.driver.stick('left', x, 0, ms);
    await switchManga(s, LIBRARY_MORE.longSeries, 1);
    let ev = await openChapter(s, 1, { mode: 'strip', shown: 'strip' });
    expect(ev).toMatchObject({ chapterId: g1, page: 0, orientation: 'landscape', stripW: 1280 });
    ev = await readerAfter(s, () => s.driver.press('Y'), { mode: 'strip', orientation: 'portrait-left' });
    expect(ev).toMatchObject({ stripW: 720, page: 0 });

    // Turned left the stick's real left points down the strip. A half push moves less than a full one would (2 s: 4267).
    let top = Number(ev.top);
    ev = await readerAfter(s, push(-0.5, 2000), { mode: 'strip' });
    console.log(`[reader-webtoon] half push 2 s: ${Number(ev.top) - top}`);
    expect(Number(ev.top) - top, 'a half push').toBeGreaterThan(200);
    expect(Number(ev.top) - top, 'a half push').toBeLessThan(2000);
    // The D-pad's LEFT held 1.5 s: a glide, then it scrolls on (a screen a second); L held 1.5 s goes back about as far.
    top = Number(ev.top);
    ev = await readerAfter(s, () => s.driver.press('LEFT', 1500), { mode: 'strip' });
    const held = Number(ev.top) - top;
    console.log(`[reader-webtoon] D-pad held 1.5 s: ${held}`);
    expect(held, 'the held D-pad scrolls on past one glide').toBeGreaterThan(1300);
    top = Number(ev.top);
    ev = await readerAfter(s, () => s.driver.press('L', 1500), { mode: 'strip' });
    console.log(`[reader-webtoon] L held 1.5 s: ${Number(ev.top) - top}`);
    expect(top - Number(ev.top), 'L held goes back').toBeGreaterThan(1300);

    // WP-13d: ZR / ZL step the zoom around the screen's middle: 1.5, then 2 (the strip 1440 wide, its left edge at
    // 360 - 2 x 360 = -360). Zoomed, the D-pad's UP (turned left, a sideways arrow) glides 3/4 of a screen sideways and
    // stops at the strip's edge, 360 away either way; the top stays. ZL twice goes back to 1.
    ev = await readerAfter(s, () => s.driver.press('ZR'), { mode: 'strip', zoom: 1.5 });
    ev = await readerAfter(s, () => s.driver.press('ZR'), { mode: 'strip', zoom: 2 });
    expect(ev).toMatchObject({ x: -360, sharp: true, sharpPages: 0 });
    top = Number(ev.top);
    ev = await readerAfter(s, () => s.driver.press('UP'), { mode: 'strip', zoom: 2 });
    expect(Math.abs(Number(ev.x) + 360), `a zoomed sideways glide: x -360 -> ${ev.x}`).toBe(360);
    expect(near(ev.top, top), 'and the top stays').toBe(true);
    ev = await readerAfter(s, () => s.driver.press('ZL'), { mode: 'strip', zoom: 1.5 });
    ev = await readerAfter(s, () => s.driver.press('ZL'), { mode: 'strip', zoom: 1 });
    expect(ev).toMatchObject({ x: 0 });

    // Three passes down and up at a full push (8 s pushes until the end: ~17,000 units each).
    const memory: number[] = [];
    let secondPass = 0;
    for (let pass = 1; pass <= 3; pass++) {
        if (pass === 2) secondPass = s.events.mark();
        for (const down of [true, false]) {
            for (let i = 0; i < 8; i++) {
                const before = Number((lastReader(s) as AppEvent).top);
                if (down ? before >= STRIP_END - 2 : before <= 2) break;
                const mark = s.events.mark();
                await push(down ? -1 : 1, 8000)();
                await s.events.waitForEvent('idle', {}, { from: mark, timeoutMs: 30_000 });
                const moved = Math.abs(Number((lastReader(s) as AppEvent).top) - before);
                if (moved < 1000) break;  // it stopped at the end during that push
            }
        }
        memory.push(Number((lastReader(s) as AppEvent).memUsed));
    }
    const waits = s.events.events.slice(secondPass).filter((e) => e.ev === 'reader')
        .reduce((sum, e) => sum + Number(e.waitFrames ?? 0), 0);
    expect(near(lastReader(s)?.top, 0), 'back at the top after the passes').toBe(true);
    const growth = Math.max(...memory.slice(1)) - memory[0];
    console.log(`[reader-webtoon] memory after the passes: ${memory.map((m) => (m / MiB).toFixed(1)).join(', ')} MiB; ` +
        `growth after the first ${(growth / MiB).toFixed(1)} MiB; waitFrames from the second pass ${waits}`);
    expect(growth, 'memory growth after the first pass').toBeLessThan(16 * MiB);
    expect(waits, 'frames that showed a page still loading, from the second pass on').toBe(0);

    // The frame times while the strip moved (the console's numbers, D17).
    const gestures = s.events.events.filter((e) => e.ev === 'gesture' && Number(e.frames) >= 3);
    const p90s = stats(gestures.map((e) => Number(e.p90Ms)));
    const worst = Math.max(...gestures.map((e) => Number(e.maxMs)));
    const kinds = [...new Set(gestures.map((e) => String(e.kind)))].join(', ');
    console.log(`[reader-webtoon] ${gestures.length} gestures (${kinds}): p90 of their p90s ${p90s.p90} ms, worst ${worst} ms`);
    expect(p90s.p90, 'frame interval p90 while the strip moves').toBeLessThanOrEqual(17);
    expect(worst, 'the longest frame while the strip moves').toBeLessThanOrEqual(34);
    await closeReader(s, PORTRAIT_LEFT);
}

scenario('reader-webtoon', async (s) => {
    const url = appServerUrl(s.driver.name);
    const client = new SuwayomiClient(optionsFromEnv());
    const ids = await mangaIds(client);
    const gamma = ids.get('Gamma Webtoon') ?? 0;
    const alpha = ids.get('Alpha') ?? 0;
    expect(gamma && alpha, 'the test server has Gamma Webtoon and Alpha: tools/test-server/test-server.sh reset').toBeTruthy();
    const reset = async () => {
        await resetChapters(client, alpha);
        await clearStripSpots(client, gamma);
        return resetChapters(client, gamma);
    };
    const [g1, g2] = (await reset()).map((c) => c.id);
    const a1 = (await chaptersOf(client, alpha))[0].id;
    const view = PORTRAIT_LEFT;

    try {
        await s.launch({ testConfig: { server: { url } } });
        await waitForLibrary(s);

        // Gamma ch1: a strip by itself (the shape of its first page), turned left (the strip's own orientation, fresh).
        await openManga(s, LIBRARY_MORE.longSeries, 1, 2);  // "Delta Big" < "Gamma Webtoon"
        let mark = s.events.mark();
        let ev = await openChapter(s, 1, { mode: 'strip', shown: 'strip' });
        expect(ev).toMatchObject({ chapterId: g1, chapter: 1, page: 0, of: GAMMA_PAGES, auto: 'shape', direction: 'vertical',
            orientation: 'portrait-left', orientationSetting: 'portrait-left', screenW: 720, screenH: 1280, stripW: 720,
            stripWidth: 'half', menu: false });
        expect(near(ev.top, ch1Page(0)), `the first page's top at the screen's top: top ${ev.top}`).toBe(true);
        console.log(`[reader-webtoon] opened by shape in ${ev.openMs} ms`);
        if (s.driver.name === 'switch') expect(Number(ev.openMs), 'openMs, the first time (by shape)').toBeLessThanOrEqual(800);
        await expectCodes(s, ev, view);
        // config.json: Gamma saved as a strip.
        await waitForConfig(s, mark, (config) => config.reader.manga.some((m) =>
            (m as { id?: number; direction?: string }).id === gamma && (m as { direction?: string }).direction === 'vertical'));

        // A drag moves the strip by the drag: no flick after a finger that rested.
        let top = Number(ev.top);
        mark = s.events.mark();
        ev = await readerAfter(s, heldDrag(s, view, 400), { mode: 'strip' });
        expect(near(ev.top, top + 400), `a 400 drag: top ${top} -> ${ev.top}`).toBe(true);
        expect(s.events.events.slice(mark).filter((e) => e.ev === 'gesture' && e.kind === 'fling')).toEqual([]);
        await expectCodes(s, ev, view);

        // A flick carries on past the finger, then stops.
        top = Number(ev.top);
        mark = s.events.mark();
        ev = await readerAfter(s, flickBy(s, view, 400), { mode: 'strip' });
        const fling = s.events.events.slice(mark).find((e) => e.ev === 'gesture' && e.kind === 'fling');
        console.log(`[reader-webtoon] a 400 flick went ${Number(ev.top) - top}: ${JSON.stringify(fling)}`);
        expect(fling, 'a "fling" gesture').toBeTruthy();
        expect(Number(ev.top) - top, 'a flick carries on past the finger').toBeGreaterThan(400 + 300);
        expect(Number(ev.top) - top, 'and stops').toBeLessThan(400 + 5 * 1280);
        const flicked = Number(ev.top) - top;
        await expectCodes(s, ev, view);

        // A touch 60 ms into a flick stops it, and is no tap (a tap down there would glide on).
        top = Number(ev.top);
        mark = s.events.mark();
        ev = await readerAfter(s, flickBy(s, view, 400, 60), { mode: 'strip' });
        expect(Number(ev.top) - top, 'a flick stopped by a touch').toBeGreaterThan(400);
        expect(Number(ev.top) - top, 'a flick stopped by a touch').toBeLessThan(flicked - 200);
        expect(s.events.events.slice(mark).filter((e) => e.ev === 'gesture' && e.kind === 'glide')).toEqual([]);
        expect(ev.menu).toBe(false);

        // Taps: the bottom glides on 3/4 of a screen, the top back, the middle opens the menu after the double tap's wait
        // (and closes it at once).
        top = Number(ev.top);
        ev = await readerAfter(s, tap(s, view.at({ x: 360, y: 1100 })), { mode: 'strip' });
        expect(near(ev.top, top + 960), `a bottom tap: top ${top} -> ${ev.top}`).toBe(true);
        await expectCodes(s, ev, view);
        ev = await readerAfter(s, tap(s, view.at({ x: 360, y: 150 })), { mode: 'strip' });
        expect(near(ev.top, top), `a top tap: back to ${top}: ${ev.top}`).toBe(true);
        // Quick taps count every one (the owner's try, WP-13d): a second bottom tap while the first one's glide runs adds
        // another glide (as a second button press does), and two quick top taps go back as far.
        ev = await readerAfter(s, doubleTap(s, { x: 360, y: 1100 }, view.at), { mode: 'strip' });
        expect(near(ev.top, top + 1920), `two quick bottom taps: top ${top} -> ${ev.top}`).toBe(true);
        ev = await readerAfter(s, doubleTap(s, { x: 360, y: 150 }, view.at), { mode: 'strip' });
        expect(near(ev.top, top), `two quick top taps: back to ${top}: ${ev.top}`).toBe(true);
        ev = await readerAfter(s, tap(s, view.at({ x: 360, y: 640 })), { mode: 'strip', menu: true });
        expect(near(ev.top, top)).toBe(true);
        ev = await readerAfter(s, tap(s, view.at({ x: 360, y: 640 })), { mode: 'strip', menu: false });

        // Zoom in the strip (WP-13d). A double tap in the middle band: 2 x around the tapped point (200, 640). The strip is
        // 1440 wide then, wider than the screen: its left edge at 200 - 2 x 200 = -200; the tapped height stays (the top
        // moves down 640 / 2). Gamma's 800-wide decode has all the detail 2 x wants: sharp, nothing decodes again.
        top = Number(ev.top);
        ev = await readerAfter(s, doubleTap(s, { x: 200, y: 640 }, view.at), { mode: 'strip', zoom: 2 });
        expect(ev).toMatchObject({ x: -200, sharp: true, sharpPages: 0, decW: GAMMA.width, menu: false });
        expect(near(ev.top, top + 320), `a double tap at 2 x: top ${top} -> ${ev.top}`).toBe(true);
        // A zoomed drag pans sideways (150 to the right): the left edge follows the finger, the top stays.
        top = Number(ev.top);
        ev = await readerAfter(s, sideDrag(s, view, 150), { mode: 'strip', zoom: 2 });
        expect(near(ev.x, -50), `a sideways drag: x -200 -> ${ev.x}`).toBe(true);
        expect(near(ev.top, top), 'and the top stays').toBe(true);
        // A pinch from 200 to 300 apart around the middle: 3 x, the point between the fingers stays (strip x 205 stays at
        // 360: the left edge at 360 - 3 x 205); then back from 300 to 200: 2 x again.
        ev = await readerAfter(s, pinch(s, { x: 360, y: 640 }, 200, 300, view.at), { mode: 'strip', zoom: 3 });
        expect(near(ev.x, -255, 3), `a pinch to 3 x: x ${ev.x}`).toBe(true);
        ev = await readerAfter(s, pinch(s, { x: 360, y: 640 }, 300, 200, view.at), { mode: 'strip', zoom: 2 });
        expect(near(ev.x, -50, 3), `and back to 2 x: x ${ev.x}`).toBe(true);
        // Scrolling keeps the zoom: a 400 drag moves the strip 200 (a screen unit is half a strip unit at 2 x).
        top = Number(ev.top);
        ev = await readerAfter(s, heldDrag(s, view, 400), { mode: 'strip', zoom: 2 });
        expect(near(ev.top, top + 200), `a zoomed 400 drag: top ${top} -> ${ev.top}`).toBe(true);
        expect(near(ev.x, -50, 3), 'sideways it stays').toBe(true);
        // A double tap again: back to the strip's width around the tapped point.
        top = Number(ev.top);
        ev = await readerAfter(s, doubleTap(s, { x: 360, y: 640 }, view.at), { mode: 'strip', zoom: 1 });
        expect(ev).toMatchObject({ x: 0, menu: false });
        expect(near(ev.top, top - 320), `a double tap back to 1: top ${top} -> ${ev.top}`).toBe(true);
        await expectCodes(s, ev, view);

        // The seam of pages 1 and 2 just above the middle line (page 2 is under it): no gap (golden).
        ev = await scrollTo(s, view, ch1Page(1) + 40 - 640);
        expect(ev).toMatchObject({ chapterId: g1, page: 1 });
        await expectCodes(s, ev, view);
        await s.expectGolden('webtoon-seam');

        // Further into page 2: the server gets lastPageRead 1 and the spot note within progressSyncMs (5 s).
        mark = s.events.mark();
        ev = await scrollTo(s, view, ch1Page(1) + 2000 - 640);
        expect(ev).toMatchObject({ chapterId: g1, page: 1, read: false });
        await expectCodes(s, ev, view);
        await s.events.waitForEvent('progress', { chapterId: g1, page: 1, spot: ev.spot, ok: true },
            { from: mark, timeoutMs: 15_000 });
        expect((await chaptersOf(client, gamma))[0]).toMatchObject({ id: g1, isRead: false, lastPageRead: 1 });
        expect((await stripSpots(client, gamma)).get(g1)).toBe(`1:${Number(ev.spot).toFixed(4)}`);

        // The end of ch1: the band between the chapters (golden), ch1 read on the server; then ch2 is the chapter read.
        mark = s.events.mark();
        ev = await scrollTo(s, view, CH1_END + BAND / 2 - 640);
        expect(ev).toMatchObject({ read: true });
        await s.expectGolden('webtoon-band');
        await s.events.waitForEvent('progress', { chapterId: g1, read: true, ok: true }, { from: mark, timeoutMs: 15_000 });
        expect((await chaptersOf(client, gamma))[0]).toMatchObject({ id: g1, isRead: true });
        ev = await scrollTo(s, view, ch2Page(0) + 1000 - 640);
        expect(ev).toMatchObject({ chapterId: g2, chapter: 2, page: 0, chapters: [g1, g2] });
        await expectCodes(s, ev, view);

        // Back up from ch2's start: ch1's last segment.
        ev = await scrollTo(s, view, ch1Page(2) + 7 * 1350 - 80);
        expect(ev).toMatchObject({ chapterId: g1, page: 2 });
        const codes = await expectCodes(s, ev, view);
        expect(codes.at(-1), "ch1's last segment").toMatchObject({ chapter: 1, page: 3, segment: 7 });

        // Closed half way down ch2's page 2; Continue opens it again with the same segment at the same place.
        ev = await scrollTo(s, view, ch2Page(1) + PAGE / 2 - 640);
        expect(ev).toMatchObject({ chapterId: g2, page: 1 });
        const closed = ev;
        const closedCode = middleCode(await expectCodes(s, ev, view), 1280);
        expect(closedCode).toBeTruthy();
        // Saved by the 5 s sync or by the close (whichever came first after the strip got there).
        await closeReader(s, view);
        await s.events.waitForEvent('progress', { chapterId: g2, page: 1, spot: closed.spot, ok: true },
            { from: s.events.events.indexOf(closed), timeoutMs: 15_000 });
        ev = await readerAfter(s, tap(s, MANGA.continue), { mode: 'strip', shown: 'strip' });
        expect(ev).toMatchObject({ chapterId: g2, page: 1, orientation: 'portrait-left' });
        expect(ev.auto, 'Gamma is saved as a strip now: no rule needed').toBeUndefined();
        console.log(`[reader-webtoon] opened again (saved as a strip) in ${ev.openMs} ms`);
        if (s.driver.name === 'switch') expect(Number(ev.openMs), 'openMs, saved as a strip').toBeLessThanOrEqual(500);
        expect(Math.abs(Number(ev.spot) - Number(closed.spot)), `spot ${closed.spot} -> ${ev.spot}`).toBeLessThanOrEqual(0.0001);
        const again = middleCode(await expectCodes(s, ev, view), 1280);
        expect(again).toMatchObject({ page: closedCode?.page, segment: closedCode?.segment });
        expect(Math.abs((again?.y ?? 0) - (closedCode?.y ?? 0)), 'the same place on screen').toBeLessThanOrEqual(3);
        await closeReader(s, view);

        // Alpha meanwhile opens paged in landscape: each reader keeps its own orientation.
        await switchManga(s, LIBRARY_MORE.reading, 0);
        ev = await openChapter(s, 2, { shown: 'page' });
        expect(ev).toMatchObject({ mode: 'paged', chapterId: a1, page: 0, orientation: 'landscape' });
        await closeReader(s, LANDSCAPE);

        // Gamma again (ch2, the first row): the strip turned left. Rotate -> landscape, 640 wide, centred (golden).
        await switchManga(s, LIBRARY_MORE.longSeries, 1);
        ev = await openChapter(s, 0, { mode: 'strip', shown: 'strip' });
        expect(ev).toMatchObject({ chapterId: g2, page: 1, orientation: 'portrait-left', stripW: 720 });
        await readerAfter(s, tap(s, view.at(PORTRAIT.middle)), { menu: true });
        mark = s.events.mark();
        ev = await readerAfter(s, tap(s, view.at(PORTRAIT.rotate)), { mode: 'strip', orientation: 'landscape', menu: true });
        expect(ev).toMatchObject({ orientationSetting: 'landscape', screenW: 1280, screenH: 720, stripW: 640 });
        await waitForSave(s, mark);
        ev = await readerAfter(s, tap(s, MIDDLE), { mode: 'strip', menu: false });
        await expectCodes(s, ev, LANDSCAPE);
        await s.expectGolden('webtoon-landscape');

        // "Strip width: Full" (entry 1, choice 3 of 3): 1280 wide.
        mark = s.events.mark();
        ev = await stripSetting(s, 1, 2, 3, { mode: 'strip', stripWidth: 'full' });
        expect(ev).toMatchObject({ stripW: 1280, orientation: 'landscape' });
        await waitForSave(s, mark);
        ev = await readerAfter(s, tap(s, MIDDLE), { mode: 'strip', menu: false });
        await expectCodes(s, ev, LANDSCAPE);
        await closeReader(s, LANDSCAPE);

        // Remembered: Gamma opens in landscape, full width.
        ev = await openChapter(s, 0, { mode: 'strip', shown: 'strip' });
        expect(ev).toMatchObject({ chapterId: g2, orientation: 'landscape', stripW: 1280, stripWidth: 'full' });
        await closeReader(s, LANDSCAPE);

        // Alpha at page 3, switched to "Vertical strip" by hand (Reading direction, choice 3 of 3): a strip of its pages, page
        // 3's top at the screen's top (landscape, full width now: the strip's settings); back to "Left to right": page 3.
        await switchManga(s, LIBRARY_MORE.reading, 0);
        ev = await openChapter(s, 2, { mode: 'paged', shown: 'page' });
        for (const page of [1, 2]) {
            ev = await readerAfter(s, () => s.driver.press(ev.direction === 'rtl' ? 'LEFT' : 'RIGHT'),
                { shown: 'page', page });
        }
        ev = await chooseSetting(s, 0, 2, 3, { mode: 'strip', shown: 'strip' });
        expect(ev).toMatchObject({ chapterId: a1, page: 2, orientation: 'landscape', stripW: 1280 });
        expect(ev.auto, 'by hand: no rule').toBeUndefined();
        ev = await readerAfter(s, tap(s, MIDDLE), { mode: 'strip', menu: false });
        const { codes: alphaCodes } = await codesOnScreen(s, 'landscape');
        expect(alphaCodes[0], "Alpha's page 3 at the top").toMatchObject({ series: 1, chapter: 1, page: 3 });
        ev = await stripSetting(s, 0, 0, 3, { mode: 'paged', shown: 'page' });
        expect(ev).toMatchObject({ chapterId: a1, page: 2, direction: 'ltr' });
        await readerAfter(s, tap(s, MIDDLE), { mode: 'paged', menu: false });
        await expectPage(s, 1, 1, 3);

        if (s.driver.name === 'switch') {
            await closeReader(s, LANDSCAPE);
            await switchControls(s, g1);
        }

        // Memory: the tiles kept stay within 64 MiB; no memory pressure, no error.
        const strips = s.events.events.filter((e) => e.ev === 'reader' && e.mode === 'strip');
        const most = Math.max(...strips.map((e) => Number(e.texUsed)));
        console.log(`[reader-webtoon] ${strips.length} strip events, texUsed at most ${(most / MiB).toFixed(1)} MiB`);
        expect(most, 'texUsed').toBeLessThanOrEqual(64 * MiB);
        expect(s.events.events.filter((e) => e.ev === 'mem_pressure')).toEqual([]);
        s.expectNoErrorEvents();
        await s.driver.quit();
    } finally {
        await reset();
    }
}, { timeoutMs: driverName() === 'switch' ? 15 * 60_000 : 5 * 60_000 });
