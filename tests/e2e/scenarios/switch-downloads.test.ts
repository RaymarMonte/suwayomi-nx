// switch-downloads (WP-18, D59, TESTING.md): downloads to this Switch's SD card (desktop: the app's data folder), from the
// details screen and the Switch downloads tab, on the test server's Local manga Alpha (3 chapters of 10 JPEG pages, in the
// Reading category). Checks, on desktop and on the Switch:
// 1. Alpha → Chapter 1's menu → Download to Switch → downloaded (a "switch_download" event, 10 pages); its page files equal
//    the server's pages byte for byte (desktop: the files; Switch: read back through sys-autopilot);
// 2. the reader opens Chapter 1 from its files: its pages load with "source":"download" and no page request goes out, also
//    over 2 page turns; 2b (WP-18c): the test server stopped, Chapter 1 opens again and turns 3 pages; the server starts;
// 3. MINUS → Download to Switch... → Unread → Chapters 2 and 3 queued (Chapter 1 is skipped: it's on the Switch), both
//    downloaded; Show → Downloaded to Switch lists the 3 (golden "chapters-on-switch");
// 4. the tab: Alpha under "On this Switch" with 3 chapters (golden "switch-downloads-tab", the space line masked: the free
//    space differs per machine); Y on Alpha → Delete all downloads... → the question → Delete → gone, its folder too;
// 5. Settings → "Delete Switch downloads after reading" on: Chapter 2 bookmarked, Chapters 1 and 2 downloaded and read to
//    their last page: closing Chapter 2 keeps its download (bookmarked), closing Chapter 1 deletes it; the switch off;
//    5b (WP-18c): Gamma Webtoon's Chapter 1 (Long series, WebP strips) downloaded opens as the strip from its files;
// 6. relaunch with a 1-byte space for downloads (test config switchDownloadLimitBytes): MINUS → Download to Switch... → All
//    → the queue waits "limit_full" (golden "switch-queue", state text masked); the button → Paused, → Resume (waiting
//    again); Clear... → the question → Clear queue → empty;
// 7. Switch only: with that queue waiting, HOME → "background"; HOME again → back to "limit_full". From the Album (WP-21)
//    HOME ends the app (an exit request); started again, the tab shows the saved queue waiting ("limit_full", 3 chapters).
// Known state first and at the end, also after a failure: none of the test server's downloads on the SD card (desktop: its
// folder deleted, found by its server.json; Switch: deleted through the app's tab, never another server's folder), Alpha's
// and Gamma's chapters unread, at page 0, without bookmarks (Gamma: without strip spots), the switch file cleared.
// The queue's own steps (slowed, Retry, a quit mid-download, "Same server?", the SD card full, 300 entries): switch-download-queue.
// Needs the test server (tools/test-server/test-server.sh start).

import { writeFileSync } from 'node:fs';
import { join } from 'node:path';

import { expect } from 'vitest';

import { chaptersOf, clearStripSpots, mangaIds, resetChapters } from '../lib/chapters.ts';
import type { AppEvent } from '../lib/events.ts';
import { openManga, readerAfter, waitForLibrary } from '../lib/reader.ts';
import { scenario, type Scenario } from '../lib/scenario.ts';
import { CLOCK_MASK, DIALOG, LIBRARY_MORE, MANGA, READER, SERVER_DOWNLOADS, tab } from '../lib/screens.ts';
import { patchChapters, serverChapters } from '../lib/server-downloads.ts';
import { appServerUrl, optionsFromEnv, startServer, stopServer, SuwayomiClient } from '../lib/suwayomi.ts';
import { chapterPageFiles, removeTestServerFolders, serverPageBytes, serverPages, testServerFolders } from '../lib/switch-downloads.ts';
import { clearTestSourceControl } from '../lib/test-source.ts';
import { playTouches, tapFrames } from '../lib/touches.ts';

/** The tab mirrors the Server downloads tab's layout: the button and Clear... on top, rows below. */
const SWITCH_TAB = { primary: SERVER_DOWNLOADS.primary, clear: SERVER_DOWNLOADS.clear };
/** The details screen's footer hint "Menu" (MINUS): a tap opens the menu (server-downloads.test.ts). */
const MENU_HINT = { x: 945, y: 683 };
/** The tab's space line (used / limit / free on the SD card) and the rows' state text change per machine and with timing. */
const SPACE_MASK = { x: 785, y: 105, w: 490, h: 70, why: 'the space line: free space differs per machine' };
/** The footer's Y hint ("Manga") while a manga row has the focus: a tap does what Y does (desktop has no Y). */
const Y_HINT = { x: 812, y: 683 };
const STATE_MASK = { x: 1000, y: 190, w: 280, h: 460, why: 'state text and progress (timing)' };
const PAGES = 10;
/** D59's numbers (on the app's own clock, D54): a downloaded chapter's first page from the played tap; a 10-page Local chapter. */
const LIMITS = { firstPageMs: 300, localChapterMs: 20_000 };

const isEv = (name: string, fields: Record<string, unknown> = {}) => (e: AppEvent) =>
    e.ev === name && Object.entries(fields).every(([k, v]) => e[k] === v);
const lastOf = (s: Scenario, ev: string) => s.events.events.filter((e) => e.ev === ev).at(-1);

/** Taps (x, y), waits for the first event that `match` accepts and for the screen to settle. */
async function tapUntil(s: Scenario, at: { x: number; y: number }, what: string, match: (e: AppEvent) => boolean,
    timeoutMs = 30_000): Promise<AppEvent> {
    const mark = s.events.mark();
    await s.driver.touch(at.x, at.y);
    const e = await s.events.waitFor(what, match, { from: mark, timeoutMs });
    await s.events.waitForEvent('idle', {}, { from: mark });
    return e;
}

async function tapSettled(s: Scenario, at: { x: number; y: number }): Promise<void> {
    const mark = s.events.mark();
    await s.driver.touch(at.x, at.y);
    await s.events.waitForEvent('idle', {}, { from: mark });
}

async function pressUntil(s: Scenario, button: 'A' | 'B' | 'X' | 'Y' | 'HOME', what: string, match: (e: AppEvent) => boolean,
    timeoutMs = 30_000): Promise<AppEvent> {
    const mark = s.events.mark();
    await s.driver.press(button);
    const e = await s.events.waitFor(what, match, { from: mark, timeoutMs });
    await s.events.waitForEvent('idle', {}, { from: mark });
    return e;
}

/** The view id in the latest "focus" event ("" when none). */
function focusedView(s: Scenario): string {
    return String(lastOf(s, 'focus')?.view ?? '');
}

/** Presses `button` until the focused view's id contains `part` (at most `max` presses). */
async function pressUntilFocus(s: Scenario, button: 'UP' | 'DOWN' | 'LEFT' | 'RIGHT', part: string, max = 12): Promise<void> {
    for (let i = 0; i <= max; i++) {
        if (focusedView(s).includes(part)) return;
        if (i === max) break;
        const mark = s.events.mark();
        await s.driver.press(button);
        // On the console a press toward a row below the visible part of a list is sometimes lost (borealis): nothing
        // changes, so no "focus" and no "idle" come. The next press moves on.
        const moved = await s.events.waitForEvent('focus', {}, { from: mark, timeoutMs: 3_000 }).catch(() => undefined);
        if (moved) await s.events.waitForEvent('idle', {}, { from: mark });
    }
    throw new Error(`no focus on ${part} after ${max} presses of ${button} (last: ${focusedView(s)})`);
}

/** The last "switch_downloads" event (what the tab shows). */
function lastState(s: Scenario): AppEvent | undefined {
    return lastOf(s, 'switch_downloads');
}

/** Waits for a "switch_downloads" event (from `from`, default: anything this launch) that `match` accepts. */
async function untilState(s: Scenario, what: string, match: (e: AppEvent) => boolean, from = 0, timeoutMs = 60_000): Promise<AppEvent> {
    return s.events.waitFor(what, (e) => e.ev === 'switch_downloads' && match(e), { from, timeoutMs });
}

/** A chapter row's menu (4 chapter actions + the Switch entry; Local: no server entry) → its 5th entry. */
async function switchEntry(s: Scenario, row: number, what: string, match: (e: AppEvent) => boolean): Promise<AppEvent> {
    await tapSettled(s, MANGA.rowMenu(row));
    return tapUntil(s, MANGA.menuEntry(4, 5), what, match);
}

/** Row `row`'s Download to Switch, then waits until that chapter is on the SD card ("switch_download" ok). */
async function downloadRow(s: Scenario, row: number, chapterId: number): Promise<AppEvent> {
    const mark = s.events.mark();
    const queued = await switchEntry(s, row, `row ${row}: Download to Switch`, isEv('switch_download_action', { action: 'enqueue' }));
    expect(queued.ok, 'the enqueue worked').toBe(true);
    const done = await s.events.waitFor(`chapter ${chapterId} on the Switch`, isEv('switch_download', { chapter: chapterId }),
        { from: mark, timeoutMs: 60_000 });
    expect(done.ok, `chapter ${chapterId} downloaded (${String(done.error ?? '')})`).toBe(true);
    return done;
}

/** MINUS → Download to Switch... (Alpha, in the library: Sort, Show, Refresh, Categories, Remove, Download to Switch...)
 *  → choice `index` of Next 1, Next 5, Next 10, Next 25, Unread, All. */
async function bulkDownload(s: Scenario, index: number): Promise<AppEvent> {
    await tapSettled(s, MENU_HINT);
    await tapSettled(s, MANGA.menuEntry(5, 6));
    return tapUntil(s, MANGA.menuEntry(index, 6), `download choice ${index}`, isEv('switch_download_action', { action: 'enqueue' }));
}

/** Where a tap turns to the next page: the left edge when the reader goes right to left (its "direction"). */
function nextPage(reader: AppEvent): { x: number; y: number } {
    return reader.direction === 'rtl' ? READER.left : READER.right;
}

/** Reads the open chapter (its "reader" event `opened`) to its last page by taps on the next-page edge. */
async function readToEnd(s: Scenario, opened: AppEvent): Promise<void> {
    const from = Number(opened.page);
    const next = nextPage(opened);
    for (let page = from + 1; page < PAGES; page++) {
        await readerAfter(s, () => s.driver.touch(next.x, next.y), { shown: 'page', page });
    }
}

/** The menu's Close (opening the menu first): back on the details screen once it has settled. */
async function closeReader(s: Scenario): Promise<void> {
    await readerAfter(s, () => s.driver.touch(READER.middle.x, READER.middle.y), { menu: true });
    const mark = s.events.mark();
    await s.driver.touch(READER.close.x, READER.close.y);
    await s.events.waitForEvent('screen', { name: 'Manga' }, { from: mark });
    // With the server stopped the details screen's reload fails after the 5 s connect timeout; with Lite's 2 transfers
    // (WP-21) it waits behind the progress save, so its note (and then idle) comes about 10 s later.
    await s.events.waitForEvent('idle', {}, { from: mark, timeoutMs: 30_000 });
}

/** Opens Alpha from the Library's Reading tab (the first of 3 there) and waits until its details screen is complete. */
async function openAlpha(s: Scenario, fromAnotherTab: boolean): Promise<AppEvent> {
    // A tap on the tab that already shows changes nothing (no event at all), so only from another tab.
    if (fromAnotherTab) await tapUntil(s, tab('Library'), 'the Library tab', isEv('screen', { name: 'Library' }));
    // Back from another tab the Library still shows Reading (a tap on it would change nothing either).
    return openManga(s, fromAnotherTab ? null : LIBRARY_MORE.reading, 0, 3);
}

/** B from the details screen: back on the main screen once it has settled. */
async function backToMain(s: Scenario): Promise<void> {
    const mark = s.events.mark();
    await s.driver.press('B');
    await s.events.waitForEvent('idle', {}, { from: mark });
}

/** The Switch downloads tab, its first "switch_downloads" answer after the tap (or the last one when nothing changed). */
async function openTab(s: Scenario): Promise<void> {
    await tapUntil(s, tab('SwitchDownloads'), 'the Switch downloads tab', isEv('screen', { name: 'SwitchDownloads' }));
}

/** Y on manga `mangaId`'s row → Delete all downloads... → Delete; waits until it's gone. */
async function deleteInTab(s: Scenario, mangaId: number): Promise<void> {
    await pressUntilFocus(s, 'RIGHT', 'switch_downloads/', 3);
    await pressUntilFocus(s, 'DOWN', `switch_downloads/manga-${mangaId}`, 8);
    const before = Number(lastState(s)?.manga);
    const mark = s.events.mark();
    if (s.driver.name === 'switch') await s.driver.press('Y');
    else await s.driver.touch(Y_HINT.x, Y_HINT.y);
    await s.events.waitForEvent('idle', {}, { from: mark });
    const ask = await tapUntil(s, MANGA.menuEntry(1, 2), 'the delete question', isEv('dialog'));
    expect(String(ask.text)).toContain('from this Switch?');
    const done = await tapUntil(s, DIALOG.confirm, 'deleted', isEv('switch_download_action', { action: 'delete_manga' }));
    expect(done.ok, 'the delete worked').toBe(true);
    await untilState(s, `manga ${mangaId} gone`, (e) => Number(e.manga) < before, mark);
}

/**
 * The Switch: no file can be deleted from outside the app, so whatever an earlier run left of the test server's downloads
 * goes through the tab (the app sees only the folder of the server it is set up for).
 */
async function deleteLeftovers(s: Scenario, alpha: number, gamma: number): Promise<void> {
    const loaded = await untilState(s, 'the downloads loaded', (e) => e.loaded === true);
    if (Number(loaded.manga) === 0 && Number(loaded.queued) === 0) return;
    expect(Number(loaded.manga), 'only Alpha and Gamma are ever downloaded by this scenario').toBeLessThanOrEqual(2);
    await openTab(s);
    if (Number(loaded.queued) > 0) await clearQueue(s);
    while (Number(lastState(s)?.manga) > 0) {
        await pressUntilFocus(s, 'RIGHT', 'switch_downloads/', 3);
        await pressUntilFocus(s, 'DOWN', 'switch_downloads/manga-', 8);
        await deleteInTab(s, focusedView(s).endsWith(`manga-${gamma}`) ? gamma : alpha);
    }
}

/** The tab's Clear... → the question → Clear queue; waits for the empty queue. */
async function clearQueue(s: Scenario): Promise<void> {
    const mark = s.events.mark();
    await tapUntil(s, SWITCH_TAB.clear, 'the clear question', isEv('dialog'));
    const done = await tapUntil(s, DIALOG.confirm, 'cleared', isEv('switch_download_action', { action: 'clear' }));
    expect(done.ok).toBe(true);
    await untilState(s, 'the queue empty', (e) => e.queued === 0, mark);
}

/** Settings → "Delete Switch downloads after reading" (A toggles it); waits for its "setting" event with `on`. */
async function setDeleteAfterReading(s: Scenario, on: boolean): Promise<void> {
    await tapUntil(s, tab('Settings'), 'the Settings tab', isEv('screen', { name: 'Settings' }));
    await pressUntilFocus(s, 'RIGHT', 'settings/', 3);
    await pressUntilFocus(s, 'DOWN', 'settings/switch_delete_after_reading', 12);
    await pressUntil(s, 'A', `Switch delete after reading: ${on}`, isEv('setting', { name: 'switchDeleteAfterReading', value: on }));
}

scenario('switch-downloads', async (s) => {
    const client = new SuwayomiClient(optionsFromEnv());
    const url = appServerUrl(s.driver.name);
    const library = await mangaIds(client);
    const alpha = library.get('Alpha') ?? 0;
    const gamma = library.get('Gamma Webtoon') ?? 0;
    expect(alpha, 'the test server has Alpha: tools/test-server/test-server.sh reset').toBeGreaterThan(0);
    expect(gamma, 'the test server has Gamma Webtoon').toBeGreaterThan(0);
    const chapters = (await serverChapters(alpha, client)).sort((a, b) => a.sourceOrder - b.sourceOrder);
    expect(chapters).toHaveLength(3);
    const [one, two, three] = chapters.map((c) => c.id);
    const gammaOne = (await chaptersOf(client, gamma))[0].id;  // by sourceOrder: Chapter 1
    const knownState = async () => {
        clearTestSourceControl();
        await patchChapters([one, two, three], { isRead: false, isBookmarked: false, lastPageRead: 0 }, client);
        await resetChapters(client, gamma);
        await clearStripSpots(client, gamma);
        await removeTestServerFolders(s.driver, url);
    };
    await knownState();

    try {
        await s.launch({ testConfig: { server: { url } } });
        await waitForLibrary(s);
        if (s.driver.name === 'switch') await deleteLeftovers(s, alpha, gamma);
        await untilState(s, 'the downloads loaded', (e) => e.loaded === true);
        // The latest state: on the Switch the first "loaded" may come before deleteLeftovers' deletes.
        expect(lastState(s)?.manga, 'nothing of the test server on the SD card').toBe(0);

        // 1. Chapter 1 (the last row: newest first) → Download to Switch; its files equal the server's pages.
        let details = await openAlpha(s, false);
        expect(details.switch).toEqual({ onSwitch: 0, damaged: 0, queued: 0, downloading: 0, failed: 0 });
        const first = await downloadRow(s, 2, one);
        expect(first.pages).toBe(PAGES);
        const numbers: Record<string, number> = { localChapterMs: Number(first.ms), localChapterBytes: Number(first.bytes) };
        numbers.pagesPerSecond = Math.round((PAGES * 1000 / Math.max(1, numbers.localChapterMs)) * 10) / 10;
        const [key] = await testServerFolders(s.driver, url);
        expect(key, 'the test server has its own downloads folder with a server.json').toBeTruthy();
        const files = await chapterPageFiles(s.driver, key, alpha, one);
        const pages = await serverPages(client, one);
        expect(files).toHaveLength(pages.length);
        for (const i of [0, pages.length - 1]) {
            const onSd = await s.driver.readFile(files[i]);
            expect(onSd.equals(await serverPageBytes(client, pages[i])), `page ${i}: the file equals the server's page`).toBe(true);
        }

        // 2. The reader opens Chapter 1 from its files: no page request, also over 2 page turns.
        let mark = s.events.mark();
        // A played tap (D54): the time from the tap's first frame to the first page shown, on the app's clock.
        const played = await playTouches(s, tapFrames(MANGA.row(2)));
        const opened = await s.events.waitFor('Chapter 1 from its files', isEv('reader', { shown: 'page', chapterId: one, page: 0 }),
            { from: mark, timeoutMs: 30_000 });
        await s.events.waitForEvent('idle', {}, { from: mark });
        numbers.firstPageMs = Math.max(0, Number(opened.t) - Number(played.started ?? played.t));
        writeFileSync(join(s.out, 'numbers.json'), JSON.stringify(numbers, null, 2));
        console.log(`[switch-downloads] ${JSON.stringify(numbers)}`);
        expect(numbers.firstPageMs, 'a downloaded chapter\'s first page (ms from the tap)').toBeLessThanOrEqual(LIMITS.firstPageMs);
        expect(numbers.localChapterMs, 'a 10-page Local chapter downloaded (ms)').toBeLessThanOrEqual(LIMITS.localChapterMs);
        await readerAfter(s, () => s.driver.touch(nextPage(opened).x, nextPage(opened).y), { shown: 'page', page: 1 });
        await readerAfter(s, () => s.driver.touch(nextPage(opened).x, nextPage(opened).y), { shown: 'page', page: 2 });
        const since = s.events.events.slice(mark);
        const pageRequests = since.filter((e) => e.ev === 'net' && /\/chapter\/\d+\/page\//.test(String(e.url)));
        expect(pageRequests.map((e) => e.url), 'no page request for a downloaded chapter').toEqual([]);
        const images = since.filter((e) => e.ev === 'image' && /\/c\d+\/\d{4}\./.test(String(e.url)));
        expect(images.length, 'pages loaded from the SD card').toBeGreaterThanOrEqual(3);
        expect(images.every((e) => e.source === 'download' || e.source === 'memory'), 'from the files (or memory)').toBe(true);
        await closeReader(s);

        // 2b. WP-18c: the server stopped: Chapter 1 still opens from its files and turns 3 pages; then the server starts
        // again (the progress saves meanwhile wait for it, WP-15b).
        stopServer();
        try {
            mark = s.events.mark();
            const away = await readerAfter(s, () => s.driver.touch(MANGA.row(2).x, MANGA.row(2).y), { shown: 'page', chapterId: one });
            for (let turn = 1; turn <= 3; turn++) {
                await readerAfter(s, () => s.driver.touch(nextPage(away).x, nextPage(away).y), { shown: 'page', page: Number(away.page) + turn });
            }
            // Each turn above waited for its page on screen. How many pages load here depends on what the reader still
            // holds from step 2 (no image event for those), so at least one, all from the files, and no page request.
            const awaySince = s.events.events.slice(mark);
            const fromFiles = awaySince.filter((e) => e.ev === 'image' && /\/c\d+\/\d{4}\./.test(String(e.url)));
            expect(fromFiles.length, 'pages loaded with the server stopped').toBeGreaterThanOrEqual(1);
            expect(fromFiles.every((e) => e.ok !== false && (e.source === 'download' || e.source === 'memory'))).toBe(true);
            const asked = awaySince.filter((e) => e.ev === 'net' && /\/chapter\/\d+\/page\//.test(String(e.url)));
            expect(asked.map((e) => e.url), 'no page request with the server stopped').toEqual([]);
            await closeReader(s);
        } finally {
            startServer();
        }

        // 3. MINUS → Download to Switch... → Unread: Chapters 2 and 3 (1 is on the Switch already).
        mark = s.events.mark();
        const bulk = await bulkDownload(s, 4);
        expect(bulk.ok).toBe(true);
        expect(bulk.chapters, 'Unread minus the chapter on the Switch').toBe(2);
        await untilState(s, 'all three on the Switch', (e) => e.queued === 0 && e.chapters === 3, mark);
        await tapSettled(s, MANGA.filter);
        details = await tapUntil(s, MANGA.menuEntry(3, 4), 'Show: Downloaded to Switch',
            (e) => e.ev === 'manga' && e.filter === 'on_switch' && e.state === 'ready');
        expect(details.items).toBe(3);
        await s.expectGolden('chapters-on-switch', { masks: [CLOCK_MASK] });
        await tapSettled(s, MANGA.filter);
        await tapUntil(s, MANGA.menuEntry(0, 4), 'Show: All', (e) => e.ev === 'manga' && e.filter === 'all' && e.state === 'ready');

        // 4. The tab: Alpha with 3 chapters; Delete all downloads... → gone, its folder too.
        await backToMain(s);
        await openTab(s);
        expect(lastState(s)?.manga).toBe(1);
        expect(lastState(s)?.chapters).toBe(3);
        await s.expectGolden('switch-downloads-tab', { masks: [CLOCK_MASK, SPACE_MASK] });
        await deleteInTab(s, alpha);
        expect(await s.driver.listFiles(`downloads/${key}/m${alpha}`), 'Alpha\'s folder is gone').toEqual([]);

        // 5. Delete after reading: Chapter 2 (bookmarked) stays, Chapter 1 goes.
        await setDeleteAfterReading(s, true);
        await patchChapters([two], { isBookmarked: true }, client);
        await openAlpha(s, true);
        await downloadRow(s, 1, two);
        await downloadRow(s, 2, one);
        const read = s.events.mark();
        for (const [row, id, bookmarked] of [[1, two, true], [2, one, false]] as const) {
            const opened = await readerAfter(s, () => s.driver.touch(MANGA.row(row).x, MANGA.row(row).y), { shown: 'page', chapterId: id });
            await readToEnd(s, opened);
            mark = s.events.mark();
            await closeReader(s);
            const deleted = s.events.events.slice(mark).filter(isEv('switch_download_action', { action: 'delete_after_reading' }));
            if (bookmarked) {
                expect(deleted, 'a bookmarked chapter keeps its download').toEqual([]);
            } else {
                expect(deleted.map((e) => [e.chapters, e.ok]), 'the read chapter\'s download goes').toEqual([[1, true]]);
            }
        }
        await untilState(s, 'only Chapter 2 left', (e) => e.chapters === 1, read);
        expect(await chapterPageFiles(s.driver, key, alpha, one), 'Chapter 1\'s pages are gone').toEqual([]);
        expect(await chapterPageFiles(s.driver, key, alpha, two)).toHaveLength(PAGES);
        await backToMain(s);
        await setDeleteAfterReading(s, false);
        await openTab(s);
        await deleteInTab(s, alpha);

        // 5b. WP-18c: Gamma Webtoon (Long series, 800 x 12000 WebP strips): its downloaded Chapter 1 opens as the strip,
        // from its files (no page request).
        await tapUntil(s, tab('Library'), 'the Library tab', isEv('screen', { name: 'Library' }));
        const gammaShown = await openManga(s, LIBRARY_MORE.longSeries, 1, 2);  // "Delta Big" < "Gamma Webtoon"
        expect(gammaShown.id).toBe(gamma);
        await downloadRow(s, 1, gammaOne);  // newest first: Chapter 1 is the second row
        mark = s.events.mark();
        const strip = await readerAfter(s, () => s.driver.touch(MANGA.row(1).x, MANGA.row(1).y),
            { mode: 'strip', shown: 'strip', chapterId: gammaOne });
        expect(strip.page).toBe(0);
        const stripSince = s.events.events.slice(mark);
        // The strip loads the next chapter below it (Chapter 2, not downloaded) from the server: only Chapter 1 must not be asked.
        expect(stripSince.filter((e) => e.ev === 'net' && String(e.url).includes(`/manga/${gamma}/chapter/1/page/`)).map((e) => e.url),
            'no page request for the downloaded strip').toEqual([]);
        const strips = stripSince.filter((e) => e.ev === 'image' && /\/c\d+\/\d{4}\.webp$/.test(String(e.url)));
        expect(strips.length, 'the strip\'s pages loaded from the SD card').toBeGreaterThanOrEqual(1);
        expect(strips.every((e) => e.source === 'download' || e.source === 'memory')).toBe(true);
        mark = s.events.mark();
        await s.driver.press('B');  // the strip closes with B (its menu's places depend on its orientation)
        await s.events.waitForEvent('screen', { name: 'Manga' }, { from: mark });
        await s.events.waitForEvent('idle', {}, { from: mark });
        await backToMain(s);
        await openTab(s);
        await deleteInTab(s, gamma);
        await patchChapters([one, two, three], { isRead: false, isBookmarked: false, lastPageRead: 0 }, client);
        await s.driver.quit();
        await resetChapters(client, gamma);  // the strip saved its page and spot (after the quit: no late save)
        await clearStripSpots(client, gamma);

        // 6. A 1-byte space for downloads: the queue waits; Pause, Resume; Clear.
        await s.launch({ testConfig: { server: { url }, switchDownloadLimitBytes: 1 } });
        await waitForLibrary(s);
        await untilState(s, 'the downloads loaded', (e) => e.loaded === true);
        await openAlpha(s, false);
        mark = s.events.mark();
        const all = await bulkDownload(s, 5);
        expect(all.chapters).toBe(3);
        await untilState(s, 'waiting: the space is full', (e) => e.view === 'waiting' && e.wait === 'limit_full' && e.queued === 3, mark);
        await backToMain(s);
        await openTab(s);
        await s.expectGolden('switch-queue', { masks: [CLOCK_MASK, SPACE_MASK, STATE_MASK] });
        mark = s.events.mark();
        await tapUntil(s, SWITCH_TAB.primary, 'paused', isEv('switch_download_action', { action: 'pause' }));
        await untilState(s, 'the tab shows it paused', (e) => e.view === 'paused' && e.paused === true, mark);
        mark = s.events.mark();
        await tapUntil(s, SWITCH_TAB.primary, 'resumed', isEv('switch_download_action', { action: 'resume' }));
        await untilState(s, 'waiting again', (e) => e.view === 'waiting' && e.wait === 'limit_full', mark);

        // 7. Switch only: HOME pauses the downloads (S5), HOME again goes back. From the Album (WP-21) HOME asks the app to
        // exit: it ends normally, and the queue it saved comes back at the next start, still waiting.
        if (s.driver.name === 'switch' && s.mode === 'applet') {
            await s.driver.requestExit();
            await s.driver.waitForExit(15_000);
            await s.launch({ testConfig: { server: { url }, switchDownloadLimitBytes: 1 }, keepConfig: true });
            await waitForLibrary(s);
            await openTab(s);
            await untilState(s, 'the saved queue, waiting again', (e) => e.view === 'waiting' && e.wait === 'limit_full' && e.queued === 3);
        } else if (s.driver.name === 'switch') {
            mark = s.events.mark();
            await s.driver.press('HOME');
            await untilState(s, 'in the background', (e) => e.wait === 'background', mark, 10_000);
            mark = s.events.mark();
            await s.driver.press('HOME');
            await untilState(s, 'back from HOME', (e) => e.wait === 'limit_full', mark, 10_000);
        }
        await clearQueue(s);
        s.expectNoErrorEvents();
        await s.driver.quit();
    } finally {
        // The app is gone (quit, or the failure ended it): the known state again. On the Switch a failed run may leave
        // downloads behind; the next run's start deletes them through the tab.
        await knownState();
    }
}, { timeoutMs: 6 * 60_000 });
