// reader-spread-split (WP-13c, TESTING.md 6e): double-page spreads shown as two halves, on Beta RTL (Reading, 2 chapters x 8
// PNG pages, right to left; chapter 1's page 4 is a 2000 x 1500 spread whose page code sits in its top-right corner, so only
// the right half shows it). Checks, on desktop and on the Switch:
// - held sideways ("Split spreads: In portrait", the default) page 4 shows its right half first: split, half 0, 720 x 1080,
//   sharp (decoded 1440 wide, so the half drawn 720 wide has 720 pixels, not the 360 of a plain decode; golden
//   "split-right-half"); a double tap zooms that half and back; the next step shows its left half (no page code; golden
//   "split-left-half"), then page 5; back lands on page 4's left half, then its right half;
// - the server's lastPageRead is page 4 (index 3) on both halves (saved when the reader closes); Continue opens page 4 at its
//   first half;
// - turned back to landscape page 4 shows whole (960 x 720, as in reader-rtl-spread); "Always" splits it in landscape too
//   (480 x 720, the right half first); left to right shows the left half first; "Never" shows it whole in portrait (720 x 540).
// The Switch adds the measurement that chose RuntimeProfile::maxDecodeScale (D48): ZR to 4 x on the split half, then the
// decoded size, sharpMs, the upload and the zoom steps' frames and memory are logged. Run it at another cap with
// SNX_MAX_DECODE_SCALE=2 (the test config's "maxDecodeScale"). Needs the test server; Beta RTL's chapters are reset before
// and after.

import { expect } from 'vitest';

import { chaptersOf, mangaIds, resetChapters } from '../lib/chapters.ts';
import type { AppEvent } from '../lib/events.ts';
import { chooseSetting, expectPage, openChapter, openManga, pageOnScreen, readerAfter, tap, waitForLibrary }
    from '../lib/reader.ts';
import { type Scenario, scenario } from '../lib/scenario.ts';
import { LIBRARY_MORE, MANGA, type Orientation, PORTRAIT, portraitPoint, READER } from '../lib/screens.ts';
import { appServerUrl, optionsFromEnv, SuwayomiClient } from '../lib/suwayomi.ts';
import { doubleTap } from '../lib/touches.ts';

const MiB = 1024 * 1024;
const SPREAD = 3;  // page 4 (index 3) of Beta RTL's chapter 1

/** Waits for config.json's save after `mark` and for the app to settle (see reader-portrait). */
async function waitForSave(s: Scenario, mark: number): Promise<void> {
    const saved = await s.events.waitForEvent('config_saved', {}, { from: mark, timeoutMs: 10_000 });
    await s.events.waitForEvent('idle', {}, { from: s.events.events.indexOf(saved) });
}

function lastReader(s: Scenario): AppEvent | undefined {
    return s.events.events.filter((e) => e.ev === 'reader').at(-1);
}

/** The cap the app decodes with: the profile's (4, D48) unless SNX_MAX_DECODE_SCALE asks for another (the measurement). */
function decodeScale(): number | undefined {
    const value = Number(process.env.SNX_MAX_DECODE_SCALE ?? '');
    return value >= 1 && value <= 4 ? value : undefined;
}

scenario('reader-spread-split', async (s) => {
    const url = appServerUrl(s.driver.name);
    const client = new SuwayomiClient(optionsFromEnv());
    const beta = (await mangaIds(client)).get('Beta RTL') ?? 0;
    expect(beta, 'the test server has Beta RTL: tools/test-server/test-server.sh reset').toBeTruthy();
    const chapters = await resetChapters(client, beta);
    const ch1 = chapters[0].id;
    const scale = decodeScale();
    const way: Orientation = 'portrait-left';
    const at = (p: { x: number; y: number }) => portraitPoint(way, p);

    try {
        await s.launch({ testConfig: { server: { url }, ...(scale ? { maxDecodeScale: scale } : {}) } });
        await waitForLibrary(s);
        await openManga(s, LIBRARY_MORE.reading, 1, 2);  // "Alpha" < "Beta RTL"
        let shown = await openChapter(s, 1, { shown: 'page' });  // chapter 1: the second row (newest first)
        expect(shown).toMatchObject({ chapterId: ch1, page: 0, direction: 'rtl', splitSpreads: 'portrait', split: false });

        // Portrait, turned left (settings entry 4, choice 1 of 3); the menu closes with a tap in the middle.
        let mark = s.events.mark();
        await chooseSetting(s, 4, 1, 3, { shown: 'page', orientation: way, menu: true });
        await waitForSave(s, mark);
        await readerAfter(s, tap(s, at(PORTRAIT.middle)), { shown: 'page', menu: false });

        // Forward (right to left: the left edge) to page 4. It was prefetched, found to be a spread and decoded again at
        // the doubled box before it came on screen: its right half, sharp.
        await readerAfter(s, tap(s, at(PORTRAIT.left)), { shown: 'page', page: 1 });
        await readerAfter(s, tap(s, at(PORTRAIT.left)), { shown: 'page', page: 2 });
        shown = await readerAfter(s, tap(s, at(PORTRAIT.left)), { shown: 'page', page: SPREAD, half: 0, sharp: true });
        expect(shown).toMatchObject({ spread: true, split: true, srcW: 2000, srcH: 1500, w: 720, h: 1080, x: 0, y: 100,
            decW: 1440, decH: 1080, zoom: 1 });
        await expectPage(s, 2, 1, 4, way);  // the page code: top right of the spread, so on this half
        await s.expectGolden('split-right-half');

        // A double tap in the middle (360, 400) zooms the half to 2 x around it, and back: it stays on this half.
        shown = await readerAfter(s, doubleTap(s, PORTRAIT.middle, at), { shown: 'page', zoom: 2 });
        expect(shown).toMatchObject({ page: SPREAD, split: true, half: 0, w: 1440, h: 2160, x: -360, y: -200 });
        shown = await readerAfter(s, doubleTap(s, PORTRAIT.middle, at), { shown: 'page', zoom: 1 });
        expect(shown).toMatchObject({ page: SPREAD, half: 0, w: 720, h: 1080 });

        if (s.driver.name === 'switch') {
            await measureZoom(s, scale ?? 4);
        }

        // The next step: its left half (no page code there); then page 5.
        shown = await readerAfter(s, tap(s, at(PORTRAIT.left)), { shown: 'page', page: SPREAD, half: 1 });
        expect(shown).toMatchObject({ split: true, w: 720, h: 1080, sharp: true });
        expect(await pageOnScreen(s, way), 'the left half shows no page code').toBeNull();
        await s.expectGolden('split-left-half');
        shown = await readerAfter(s, tap(s, at(PORTRAIT.left)), { shown: 'page', page: SPREAD + 1 });
        expect(shown).toMatchObject({ split: false, spread: false });

        // Back: page 4's last (left) half, then its right half.
        await readerAfter(s, tap(s, at(PORTRAIT.right)), { shown: 'page', page: SPREAD, half: 1, split: true });
        await readerAfter(s, tap(s, at(PORTRAIT.right)), { shown: 'page', page: SPREAD, half: 0, split: true });

        // Both halves are page 4 for the server: closing saves lastPageRead 3.
        mark = s.events.mark();
        await s.driver.press('B');
        await s.events.waitForEvent('screen', { name: 'Manga' }, { from: mark });
        await s.events.waitForEvent('idle', {}, { from: mark });
        expect((await chaptersOf(client, beta))[0]).toMatchObject({ id: ch1, lastPageRead: SPREAD, isRead: false });

        // Continue opens chapter 1 where it was left: page 4, its first half (still in portrait).
        shown = await readerAfter(s, tap(s, MANGA.continue), { shown: 'page', page: SPREAD, sharp: true });
        expect(shown).toMatchObject({ chapterId: ch1, orientation: way, split: true, half: 0 });

        // The menu's Rotate (at its portrait place): landscape shows the spread whole.
        await readerAfter(s, tap(s, at(PORTRAIT.middle)), { menu: true });
        mark = s.events.mark();
        shown = await readerAfter(s, tap(s, at(PORTRAIT.rotate)), { shown: 'page', orientation: 'landscape', menu: true });
        expect(shown).toMatchObject({ page: SPREAD, spread: true, split: false, w: 960, h: 720 });
        await waitForSave(s, mark);

        // "Always" (entry 6, choice 1 of 3): split in landscape too, a half of 480 x 720, the right half first.
        mark = s.events.mark();
        shown = await chooseSetting(s, 6, 1, 3, { shown: 'page', splitSpreads: 'always', split: true, menu: true });
        expect(shown).toMatchObject({ page: SPREAD, half: 0, w: 480, h: 720, x: 400, y: 0, sharp: true });
        await waitForSave(s, mark);
        await readerAfter(s, tap(s, READER.middle), { shown: 'page', menu: false });
        await expectPage(s, 2, 1, 4);
        await s.expectGolden('always-landscape-right-half');

        // Left to right (entry 0, choice 0 of 2): the left half comes first, the right half next (the right edge).
        mark = s.events.mark();
        await chooseSetting(s, 0, 0, 2, { shown: 'page', direction: 'ltr', split: true, half: 0, menu: true });
        await waitForSave(s, mark);
        await readerAfter(s, tap(s, READER.middle), { shown: 'page', menu: false });
        expect(await pageOnScreen(s), 'left to right starts with the left half (no page code)').toBeNull();
        await readerAfter(s, tap(s, READER.right), { shown: 'page', page: SPREAD, half: 1, split: true });
        await expectPage(s, 2, 1, 4);

        // "Never" (entry 6, choice 2 of 3), then portrait: the spread whole on the portrait screen, 720 x 540.
        mark = s.events.mark();
        shown = await chooseSetting(s, 6, 2, 3, { shown: 'page', splitSpreads: 'never', split: false, menu: true });
        expect(shown).toMatchObject({ page: SPREAD, w: 960, h: 720 });
        await waitForSave(s, mark);
        mark = s.events.mark();
        shown = await chooseSetting(s, 4, 1, 3, { shown: 'page', orientation: way, menu: true });
        expect(shown).toMatchObject({ page: SPREAD, split: false, w: 720, h: 540 });
        await waitForSave(s, mark);

        mark = s.events.mark();
        await s.driver.press('B');
        await s.events.waitForEvent('screen', { name: 'Manga' }, { from: mark });
        await s.events.waitForEvent('idle', {}, { from: mark });
        s.expectNoErrorEvents();
        await s.driver.quit();
    } finally {
        await resetChapters(client, beta);
    }
});

/**
 * The Switch only (the desktop can't press ZR / ZL): ZR four times to 4 x on the split half, the sharper decode if the cap
 * allows one, then ZL back to the fit. Logs what D48's maxDecodeScale choice needs: the decoded size at 4 x, sharpMs and the
 * upload, the zoom steps' frame intervals, the memory and the GPU budget used.
 */
async function measureZoom(s: Scenario, scale: number): Promise<void> {
    const press = (btn: Parameters<Scenario['driver']['press']>[0]) => () => s.driver.press(btn);
    const fit = lastReader(s);
    expect(fit).toMatchObject({ zoom: 1, half: 0 });
    const mark = s.events.mark();
    for (const zoom of [1.5, 2, 3, 4]) {
        await readerAfter(s, press('ZR'), { shown: 'page', zoom, half: 0 });
    }
    // The decode for 4 x has settled (the reader is idle only then); it is sharp as far as the cap allows.
    const zoomed = lastReader(s);
    expect(zoomed).toMatchObject({ zoom: 4, split: true, half: 0, sharp: true });
    const after = s.events.events.slice(mark);
    const sharp = after.filter((e) => e.ev === 'reader' && e.sharpMs !== undefined).at(-1);
    const steps = after.filter((e) => e.ev === 'gesture' && e.kind === 'zoom_step');
    const worst = Math.max(...steps.map((e) => Number(e.maxMs)));
    const p90 = Math.max(...steps.map((e) => Number(e.p90Ms)));
    console.log(`[reader-spread-split] maxDecodeScale ${scale}: at 4 x decoded ${zoomed?.decW} x ${zoomed?.decH}, ` +
        `sharpMs ${sharp?.sharpMs ?? 'none (no sharper decode)'}, upload ${sharp?.uploadMs ?? '-'} ms, zoom steps p90 ` +
        `${p90} / max ${worst} ms, memory ${(Number(fit?.memUsed) / MiB).toFixed(1)} -> ` +
        `${(Number(zoomed?.memUsed) / MiB).toFixed(1)} MiB, textures ${(Number(fit?.texUsed) / MiB).toFixed(1)} -> ` +
        `${(Number(zoomed?.texUsed) / MiB).toFixed(1)} MiB`);
    for (const zoom of [3, 2, 1.5, 1]) {
        await readerAfter(s, press('ZL'), { shown: 'page', zoom, half: 0 });
    }
    // Back at the fit: the split decode's textures again.
    expect(lastReader(s)).toMatchObject({ zoom: 1, texUsed: fit?.texUsed });
}
