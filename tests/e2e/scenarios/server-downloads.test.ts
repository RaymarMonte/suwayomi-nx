// server-downloads (WP-17, D58, TESTING.md): downloads onto the Suwayomi server's own disk, from the details screen and the
// Server downloads tab, on the test server's test source (SNX Test EN, en-14 "Paper Garden": 8 chapters of 3 pages, the
// first manga in Latest). It opens from the source's screen; it doesn't need to be in the library. Checks, on desktop and
// on the Switch:
// - every source call slowed by 5 s; Chapter 1's menu → Download on server → queued; MINUS → Download on server... → Next 5
//   → chapters 2–6 too (the server's queue: 1–6 in that order);
// - the tab: the rows in the server's order, Running; Pause (Switch: X; desktop: the button) → Paused (golden
//   "server-queue"), the server STOPPED; while paused (so the order stands still), by taps on the rows: Move to bottom,
//   Move to top, Remove → the server's order and queue agree; Resume;
//   the slowdown off → the queue empties (golden "server-empty"), chapters 1, 3, 4, 5, 6 downloaded on the server;
// - the details screen: Show → Downloaded on server lists exactly those (golden "chapters-on-server"); Delete server
//   download → the row goes, the server agrees;
// - the switch file fails "image": Chapter 2 → Download on server → "Failed after 3 tries" in the tab; failure cleared →
//   the row's Retry → downloaded;
// - slowed again: MINUS → Unread → 3 chapters queued; the tab's Clear... → the question → Clear queue → empty, the server too;
// - Alpha (Local): its chapter menu has no server entry, only WP-18's Download to Switch (golden "local-chapter-menu");
// - Settings → "Delete server downloads after reading" on → Chapter 3 read to its last page → closed: its download goes;
//   Chapter 4 (bookmarked) read too: its download stays → the switch off;
// - while the Library tab shows, no polls (the next tab's first answer counts at most one more poll than before);
// - Switch only: 100 rows queued by GraphQL (slowed by 10 s) → the tab: a status of 100 rows applied ≤ 50 ms; 10 rounds
//   tab ↔ Library: the memory in use grows by less than 16 MiB; then Clear;
// - D58: every action → its answer on screen ≤ 1 s (the event's ms, the app's own clock, D54).
// Known state first and at the end, also after a failure: an empty, stopped queue, none of the touched manga's chapters
// downloaded on the server, Paper Garden's chapters unread and without bookmarks, the switch file cleared.
// Needs the test server (tools/test-server/test-server.sh start).

import { expect } from 'vitest';

import type { AppEvent } from '../lib/events.ts';
import { openManga, readerAfter, openChapter, waitForLibrary } from '../lib/reader.ts';
import { driverName, scenario, type Scenario } from '../lib/scenario.ts';
import { CLOCK_MASK, DIALOG, LIBRARY_MORE, MANGA, SERVER_DOWNLOADS, tab } from '../lib/screens.ts';
import {
    clearServerQueue, downloadedOrders, enqueueOnServer, PAPER_GARDEN_KEY, patchChapters, serverChapters, serverDownloadsKnownState,
    serverQueue, sourceMangaWithChapters, stopOnServer, untilServer,
} from '../lib/server-downloads.ts';
import { appServerUrl, optionsFromEnv, SuwayomiClient } from '../lib/suwayomi.ts';
import { clearTestSourceControl, setTestSourceControl, TEST_SOURCE_IDS } from '../lib/test-source.ts';

const MiB = 1024 * 1024;
const ROUNDS = 10;
/** D58's limits (WP-17 Acceptance), on the app's own clock. */
const LIMITS = { actionMs: 1_000, status100Ms: 50, roundsBytes: 16 * MiB };
/** The slowdown while the queue must stand still long enough to look at it: every source call waits this long. */
const SLOW_MS = 8_000;

/** The details screen's footer hint "Menu" (MINUS): a tap opens the menu (browse.test.ts). */
const MENU_HINT = { x: 945, y: 683 };
/** The Browse tab's rows and a source's bar (browse.test.ts). */
const BROWSE_ROW = (i: number) => ({ x: 834, y: 223 + 84 * i });
const SOURCE_LATEST = { x: 170, y: 123 };
const SOURCE_COVER_0 = { x: 96, y: 274 };
/** The tab's rows: the state text and the progress bar on the right change with the timing, so goldens leave them out. */
const STATE_MASK = { x: 1000, y: 190, w: 280, h: 460, why: 'state text and progress (timing)' };

type Row = { chapterId: number; chapter: string; mangaId: number; manga: string; state: string; percent: number; tries: number };
type ServerCounts = { downloaded: number; queued: number; downloading: number; failed: number };

const isEv = (name: string, fields: Record<string, unknown> = {}) => (e: AppEvent) =>
    e.ev === name && Object.entries(fields).every(([k, v]) => e[k] === v);

const rowsOf = (e: AppEvent) => (e.first ?? []) as Row[];
const countsOf = (e: AppEvent) => e.server as ServerCounts;
const lastOf = (s: Scenario, ev: string) => s.events.events.filter((e) => e.ev === ev).at(-1);
const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

/** Taps (x, y), waits for the first event that `match` accepts and for the screen to settle. */
async function tapUntil(s: Scenario, at: { x: number; y: number }, what: string, match: (e: AppEvent) => boolean,
    timeoutMs = 30_000): Promise<AppEvent> {
    const mark = s.events.mark();
    await s.driver.touch(at.x, at.y);
    const e = await s.events.waitFor(what, match, { from: mark, timeoutMs });
    await s.events.waitForEvent('idle', {}, { from: mark });
    return e;
}

/** Taps (x, y) and waits for the screen to settle (a menu opening). */
async function tapSettled(s: Scenario, at: { x: number; y: number }): Promise<void> {
    const mark = s.events.mark();
    await s.driver.touch(at.x, at.y);
    await s.events.waitForEvent('idle', {}, { from: mark });
}

/** Presses `button`, waits for the first event that `match` accepts and for the screen to settle. */
async function pressUntil(s: Scenario, button: 'A' | 'B' | 'X' | 'RIGHT', what: string, match: (e: AppEvent) => boolean,
    timeoutMs = 30_000): Promise<AppEvent> {
    const mark = s.events.mark();
    await s.driver.press(button);
    const e = await s.events.waitFor(what, match, { from: mark, timeoutMs });
    await s.events.waitForEvent('idle', {}, { from: mark });
    return e;
}

async function pressSettled(s: Scenario, button: 'A' | 'B' | 'RIGHT'): Promise<void> {
    const mark = s.events.mark();
    await s.driver.press(button);
    await s.events.waitForEvent('idle', {}, { from: mark });
}

/** B, then waits for the `screen` event of the screen it goes back to, and for the screen to settle. */
async function backTo(s: Scenario, screen: string): Promise<void> {
    const mark = s.events.mark();
    await s.driver.press('B');
    await s.events.waitForEvent('screen', { name: screen }, { from: mark });
    await s.events.waitForEvent('idle', {}, { from: mark });
}

/** The view id in the latest "focus" event ("" when none). */
function focusedView(s: Scenario): string {
    return String(lastOf(s, 'focus')?.view ?? '');
}

/**
 * Presses `button` until the focused view's description contains `part` (at most `max` presses). After a touch the first
 * D-pad press only brings the focus frame back (borealis leaves touch mode), so a press without a focus event is fine.
 */
async function pressUntilFocus(s: Scenario, button: 'UP' | 'DOWN' | 'LEFT' | 'RIGHT', part: string, max = 12): Promise<void> {
    for (let i = 0; i < max && !focusedView(s).includes(part); i++) {
        const mark = s.events.mark();
        await s.driver.press(button);
        await s.events.waitForEvent('focus', {}, { from: mark, timeoutMs: 3_000 }).catch(() => undefined);
        await s.events.waitForEvent('idle', {}, { from: mark });
    }
    expect(focusedView(s), `${button}: the focus reaches ${part}`).toContain(part);
}

/** Waits for the next "mem" event (one every 10 s) and returns its "used". */
async function memNow(s: Scenario): Promise<number> {
    const mem = await s.events.waitForEvent('mem', {}, { from: s.events.mark(), timeoutMs: 15_000 });
    return Number(mem.used);
}

/** Browse → SNX Test (EN) → Latest → its first cover (Paper Garden): the details screen, complete. Returns its event. */
async function openPaperGarden(s: Scenario, id: number): Promise<AppEvent> {
    // Already on the Browse tab (back from a source): a tap on it changes nothing, so its last list counts.
    const onBrowse = lastOf(s, 'screen')?.name === 'Browse' && lastOf(s, 'sources') !== undefined;
    const sources = onBrowse ? lastOf(s, 'sources') as AppEvent
        : await tapUntil(s, tab('Browse'), 'the sources', isEv('sources', { state: 'ready' }));
    const row = (sources.rows as { id: string }[]).findIndex((r) => r.id === TEST_SOURCE_IDS.en);
    expect(row, 'the Browse tab lists SNX Test (EN)').toBeGreaterThanOrEqual(0);
    await tapUntil(s, BROWSE_ROW(row), 'SNX Test (EN)', isEv('browse', { source: TEST_SOURCE_IDS.en, mode: 'popular', state: 'ready' }));
    const latest = await tapUntil(s, SOURCE_LATEST, 'Latest', isEv('browse', { mode: 'latest', state: 'ready' }));
    expect((latest.titles as string[])[0]).toBe('Paper Garden');
    return tapUntil(s, SOURCE_COVER_0, 'Paper Garden',
        (e) => e.ev === 'manga' && e.state === 'ready' && e.complete === true && e.id === id);
}

/** The details screen's back to the Browse tab (Manga → Source → Browse). */
async function detailsToBrowse(s: Scenario): Promise<void> {
    await backTo(s, 'Source');
    await backTo(s, 'Browse');
}

/** The chapter list newest or oldest first (oldest: Chapter 1 on row 0), unless it already is. */
async function sortBy(s: Scenario, sort: 'newest' | 'oldest'): Promise<void> {
    if (lastOf(s, 'manga')?.sort === sort) return;
    await tapSettled(s, MANGA.sort);
    await tapUntil(s, MANGA.menuEntry(sort === 'newest' ? 0 : 1, 2), `${sort} first`,
        (e) => e.ev === 'manga' && e.sort === sort && e.state === 'ready');
}

/** The details screen's Show filter: entry `index` of All, Unread, Bookmarked, Downloaded to Switch (WP-18), Downloaded on server. */
async function showOnly(s: Scenario, index: number, name: string): Promise<AppEvent> {
    await tapSettled(s, MANGA.filter);
    return tapUntil(s, MANGA.menuEntry(index, 5), `Show: ${name}`, (e) => e.ev === 'manga' && e.filter === name && e.state === 'ready');
}

/** A chapter row's menu (4 chapter actions, WP-18's Switch entry, the server entry) → its 6th entry; waits for the answer. */
async function serverEntry(s: Scenario, row: number, action: string): Promise<AppEvent> {
    await tapSettled(s, MANGA.rowMenu(row));
    const done = await tapUntil(s, MANGA.menuEntry(5, 6), `row ${row}: ${action}`, isEv('server_download_action', { action }));
    expect(done.ok, `${action} answered ok`).toBe(true);
    return done;
}

/** MINUS → Download on server... → choice `index` (Next 1, Next 5, Next 10, Next 25, Unread, All); waits for the enqueue. */
async function downloadChoice(s: Scenario, index: number): Promise<AppEvent> {
    await tapSettled(s, MENU_HINT);
    // Not in the library: Sort, Show, Refresh, Add to library, Download to Switch... (WP-18), Download on server...
    await tapSettled(s, MANGA.menuEntry(5, 6));
    const done = await tapUntil(s, MANGA.menuEntry(index, 6), `download choice ${index}`, isEv('server_download_action', { action: 'enqueue' }));
    expect(done.ok, 'the enqueue answered ok').toBe(true);
    return done;
}

/** Opens the Server downloads tab, waits for a "server_downloads" event that `match` accepts. */
async function openTab(s: Scenario, what: string, match: (e: AppEvent) => boolean, timeoutMs = 30_000): Promise<AppEvent> {
    return tapUntil(s, tab('ServerDownloads'), what, (e) => e.ev === 'server_downloads' && match(e), timeoutMs);
}

/**
 * With the tab open: a tap on row `index` → its menu → entry `entry` of `count`. (Taps, not the D-pad and X: on desktop X is
 * a middle click, and the next key then focuses the row under the mouse pointer, not the one the focus events named.)
 */
async function rowAction(s: Scenario, index: number, entry: number, count: number, action: string): Promise<AppEvent> {
    await tapSettled(s, SERVER_DOWNLOADS.row(index));
    const done = await tapUntil(s, MANGA.menuEntry(entry, count), `row ${index}: ${action}`, isEv('server_download_action', { action }));
    expect(done.ok, `${action} answered ok`).toBe(true);
    return done;
}

/** The next "server_downloads" event from `mark` on whose rows are `ids`, in that order. */
async function rowsBecome(s: Scenario, mark: number, ids: number[], what: string): Promise<AppEvent> {
    return s.events.waitFor(what, (e) => e.ev === 'server_downloads' &&
        JSON.stringify(rowsOf(e).map((r) => r.chapterId)) === JSON.stringify(ids), { from: mark, timeoutMs: 15_000 });
}

/** Reads chapter row `row` (oldest first) to its last page and closes the reader. Returns the progress save's event. */
async function readToEnd(s: Scenario, row: number, chapterId: number): Promise<AppEvent> {
    const start = s.events.mark();  // the save of the last page can go out before the reader closes
    const first = await openChapter(s, row, { shown: 'page', chapterId, page: 0 });
    const next = first.direction === 'rtl' ? 'LEFT' : 'RIGHT';
    const pages = Number(first.of);
    for (let page = 1; page < pages; page++) {
        await readerAfter(s, () => s.driver.press(next), { shown: 'page', page });
    }
    const mark = s.events.mark();
    await s.driver.press('B');
    const saved = await s.events.waitForEvent('progress', { chapterId, read: true }, { from: start });
    expect(saved.ok).toBe(true);
    await s.events.waitForEvent('screen', { name: 'Manga' }, { from: mark });
    await s.events.waitForEvent('idle', {}, { from: mark });
    return saved;
}

/** Settings → "Delete server downloads after reading" (A toggles it); waits for the "setting" event with `on`. */
async function setDeleteAfterReading(s: Scenario, on: boolean): Promise<void> {
    await tapUntil(s, tab('Settings'), 'the Settings tab', isEv('screen', { name: 'Settings' }));
    await pressUntilFocus(s, 'RIGHT', 'settings/', 3);
    await pressUntilFocus(s, 'DOWN', 'settings/delete_after_reading', 16);
    await pressUntil(s, 'A', `delete after reading: ${on}`, isEv('setting', { name: 'deleteAfterReading', value: on }));
}

/** Switch only: about 100 chapters of SNX Test (EN) manga after Paper Garden (en-15 on), loaded on the server. */
async function hundredChapters(client: SuwayomiClient): Promise<{ ids: number[]; manga: number[] }> {
    const ids: number[] = [];
    const manga: number[] = [];
    for (let n = 15; n <= 60 && ids.length < 100; n++) {
        const found = await sourceMangaWithChapters(`en-${String(n).padStart(2, '0')}`, client);
        manga.push(found.id);
        ids.push(...found.chapters.map((c) => c.id));
    }
    return { ids: ids.slice(0, 100), manga };
}

scenario('server-downloads', async (s) => {
    const url = appServerUrl(s.driver.name);
    const client = new SuwayomiClient(optionsFromEnv());
    clearTestSourceControl();
    const paper = await sourceMangaWithChapters(PAPER_GARDEN_KEY, client);
    expect(paper.chapters.map((c) => c.sourceOrder)).toEqual([1, 2, 3, 4, 5, 6, 7, 8]);
    const ch = (order: number) => paper.chapters[order - 1].id;
    const touched = [paper.id];
    const knownState = async () => {
        clearTestSourceControl();
        await serverDownloadsKnownState(touched, client);
    };
    await knownState();
    try {
        await s.launch({ testConfig: { server: { url } } });
        await waitForLibrary(s);

        // 1. The details screen (before the slowdown: the source screen loads from the source), oldest first.
        let details = await openPaperGarden(s, paper.id);
        expect(countsOf(details)).toEqual({ downloaded: 0, queued: 0, downloading: 0, failed: 0 });
        await sortBy(s, 'oldest');
        setTestSourceControl({ delayMs: SLOW_MS });

        // Chapter 1's menu → Download on server: queued, then downloading.
        let mark = s.events.mark();
        const one = await serverEntry(s, 0, 'enqueue');
        expect(one.chapters).toBe(1);
        await s.events.waitFor('Chapter 1 downloading on the server', (e) => e.ev === 'manga' && countsOf(e).downloading === 1,
            { from: mark, timeoutMs: 30_000 });
        // MINUS → Download on server... → Next 5: chapters 2–6.
        const five = await downloadChoice(s, 1);
        expect(five.chapters).toBe(5);
        const queued = await serverQueue(client);
        expect(queued.rows.map((r) => r.sourceOrder)).toEqual([1, 2, 3, 4, 5, 6]);

        // 2. The tab: the server's order, Running. Pause (the header's first button, X's twin) → Paused, the server STOPPED.
        await detailsToBrowse(s);
        const running = await openTab(s, 'the queue running', (e) => e.view === 'running' && e.rows === 6);
        expect(rowsOf(running).map((r) => r.chapterId)).toEqual(queued.rows.map((r) => r.chapterId));
        expect(rowsOf(running)[0]).toMatchObject({ manga: 'Paper Garden', chapter: 'Chapter 1' });
        mark = s.events.mark();
        const isPaused = isEv('server_downloads', { view: 'paused' });
        let paused: AppEvent;
        if (s.driver.name === 'switch') {
            await pressUntilFocus(s, 'RIGHT', 'server_downloads/', 3);
            paused = await pressUntil(s, 'X', 'paused', isPaused);
        } else {
            paused = await tapUntil(s, SERVER_DOWNLOADS.primary, 'paused', isPaused);  // desktop X = a middle click (rowAction)
        }
        expect(paused.rows).toBe(6);
        expect(await s.events.waitForEvent('server_download_action', { action: 'pause' }, { from: mark })).toMatchObject({ ok: true });
        await s.expectGolden('server-queue', { masks: [CLOCK_MASK, STATE_MASK] });
        expect((await serverQueue(client)).state).toBe('STOPPED');

        // 3. While paused: Chapter 1 to the bottom, Chapter 3 to the top, Chapter 2 out. The server agrees each time.
        mark = s.events.mark();
        await rowAction(s, 0, 1, 4, 'bottom');
        await rowsBecome(s, mark, [ch(2), ch(3), ch(4), ch(5), ch(6), ch(1)], 'Chapter 1 at the bottom');
        expect((await serverQueue(client)).rows.map((r) => r.sourceOrder)).toEqual([2, 3, 4, 5, 6, 1]);
        mark = s.events.mark();
        await rowAction(s, 1, 0, 4, 'top');
        await rowsBecome(s, mark, [ch(3), ch(2), ch(4), ch(5), ch(6), ch(1)], 'Chapter 3 at the top');
        expect((await serverQueue(client)).rows.map((r) => r.sourceOrder)).toEqual([3, 2, 4, 5, 6, 1]);
        mark = s.events.mark();
        await rowAction(s, 1, 2, 4, 'remove');
        await rowsBecome(s, mark, [ch(3), ch(4), ch(5), ch(6), ch(1)], 'Chapter 2 removed');
        expect((await serverQueue(client)).rows.map((r) => r.sourceOrder)).toEqual([3, 4, 5, 6, 1]);

        // 4. Resume (the same button); the slowdown off → the queue empties (golden "server-empty"): 1, 3, 4, 5, 6 on the server's disk.
        mark = s.events.mark();
        await tapUntil(s, SERVER_DOWNLOADS.primary, 'resumed', isEv('server_download_action', { action: 'resume' }));
        expect(lastOf(s, 'server_download_action')?.ok).toBe(true);
        clearTestSourceControl();
        const emptied = await s.events.waitFor('the queue empty',
            (e) => e.ev === 'server_downloads' && e.view === 'idle' && e.rows === 0, { from: mark, timeoutMs: 90_000 });
        await s.events.waitForEvent('idle', {}, { from: s.events.events.indexOf(emptied) });
        await s.expectGolden('server-empty', { masks: [CLOCK_MASK] });
        expect(await downloadedOrders(paper.id, client)).toEqual([1, 3, 4, 5, 6]);

        // 5. The details screen: Show → Downloaded on server lists exactly them (golden); Delete server download on its first
        // row (newest first: Chapter 6) → gone from the list and from the server's disk.
        details = await openPaperGarden(s, paper.id);
        expect(countsOf(details)).toMatchObject({ downloaded: 5, queued: 0, downloading: 0, failed: 0 });
        await sortBy(s, 'newest');
        const onServer = await showOnly(s, 4, 'downloaded_on_server');
        expect(onServer.items).toBe(5);
        await s.expectGolden('chapters-on-server', { masks: [CLOCK_MASK] });
        mark = s.events.mark();
        const deleted = await serverEntry(s, 0, 'delete');
        expect(deleted.chapters).toBe(1);
        await s.events.waitFor('Chapter 6 off the list', (e) => e.ev === 'manga' && e.filter === 'downloaded_on_server' && e.items === 4,
            { from: mark });
        expect(await downloadedOrders(paper.id, client)).toEqual([1, 3, 4, 5]);
        await showOnly(s, 0, 'all');
        await detailsToBrowse(s);

        // 6. Page images fail: Chapter 2 → Download on server → failed after 3 tries (the tab); failure cleared → Retry.
        // (A fresh details screen each time rows are tapped by place: after a sort or filter change the list keeps the
        // focused chapter in view, so it may have scrolled.)
        await openPaperGarden(s, paper.id);
        await sortBy(s, 'oldest');
        setTestSourceControl({ fail: [{ lang: 'en', call: 'image' }] });
        await serverEntry(s, 1, 'enqueue');
        await detailsToBrowse(s);
        const failed = await openTab(s, 'Chapter 2 failed', (e) => e.failed === 1 && rowsOf(e)[0]?.state === 'failed', 90_000);
        expect(rowsOf(failed)[0]).toMatchObject({ chapterId: ch(2), tries: 3 });
        await s.expectGolden('server-failed', { masks: [CLOCK_MASK] });
        clearTestSourceControl();
        mark = s.events.mark();
        await rowAction(s, 0, 3, 5, 'retry');  // a failed row's menu: Move to top, Move to bottom, Remove, Retry, Open the manga
        await s.events.waitFor('Chapter 2 downloaded', (e) => e.ev === 'server_downloads' && e.view === 'idle' && e.rows === 0,
            { from: mark, timeoutMs: 60_000 });
        expect(await downloadedOrders(paper.id, client)).toEqual([1, 2, 3, 4, 5]);

        // 7. Slowed: MINUS → Unread → chapters 6, 7, 8; the tab's Clear... → the question → Clear queue → empty.
        // A clear while a chapter sits in a source call waits for that call on the server (up to SLOW_MS here; probed
        // 2026-11-19), so before the tap the queue is stopped by GraphQL and its answer awaited: the clear's 1 s is the app's.
        await openPaperGarden(s, paper.id);
        setTestSourceControl({ delayMs: SLOW_MS });
        const unread = await downloadChoice(s, 4);
        expect(unread.chapters).toBe(3);
        await detailsToBrowse(s);
        await openTab(s, 'three queued', (e) => e.rows === 3);
        await untilServer('a chapter downloading', () => serverQueue(client),
            (q) => q.state === 'STARTED' && q.rows.some((r) => r.state === 'DOWNLOADING'), 20_000);
        const beforeStop = s.events.mark();
        await stopOnServer(client);
        await s.events.waitFor('the tab shows it paused', (e) => e.ev === 'server_downloads' && e.view === 'paused' && e.rows === 3,
            { from: beforeStop, timeoutMs: 15_000 });
        const ask = await tapUntil(s, SERVER_DOWNLOADS.clear, 'the clear question', isEv('dialog'));
        expect(ask.text).toBe("Remove all 3 chapters from the server's queue? Chapters already downloaded stay.");
        mark = s.events.mark();
        await tapUntil(s, DIALOG.confirm, 'cleared', isEv('server_download_action', { action: 'clear' }));
        expect(lastOf(s, 'server_download_action')?.ok).toBe(true);
        await s.events.waitFor('the queue empty', (e) => e.ev === 'server_downloads' && e.rows === 0, { from: mark });
        expect((await serverQueue(client)).rows).toEqual([]);
        clearTestSourceControl();

        // 8. Alpha (Local): its chapter menu has the 4 chapter actions and Download to Switch, no server entry (golden "local-chapter-menu").
        await tapUntil(s, tab('Library'), 'the Library tab', isEv('library', { state: 'ready' }));
        const alpha = await openManga(s, LIBRARY_MORE.reading, 0, 3);
        expect(countsOf(alpha)).toEqual({ downloaded: 0, queued: 0, downloading: 0, failed: 0 });
        await tapSettled(s, MANGA.rowMenu(0));
        await s.expectGolden('local-chapter-menu', { masks: [CLOCK_MASK] });
        await pressSettled(s, 'B');  // the menu
        await backTo(s, 'Library');  // the details screen

        // 9. Delete after reading: on → Chapter 3 read to the end → its download goes; Chapter 4 (bookmarked) → stays; off.
        await patchChapters([ch(4)], { isBookmarked: true }, client);
        await setDeleteAfterReading(s, true);
        await openPaperGarden(s, paper.id);
        await sortBy(s, 'oldest');
        mark = s.events.mark();
        await readToEnd(s, 2, ch(3));
        const afterReading = await s.events.waitForEvent('server_download_action', { action: 'delete_after_reading' }, { from: mark });
        expect(afterReading).toMatchObject({ ok: true, chapters: 1 });
        await untilServer('Chapter 3 deleted', () => downloadedOrders(paper.id, client), (d) => !d.includes(3), 10_000);
        await detailsToBrowse(s);
        await openPaperGarden(s, paper.id);
        await sortBy(s, 'oldest');
        mark = s.events.mark();
        await readToEnd(s, 3, ch(4));
        await sleep(3_000);
        expect(s.events.events.slice(mark).some((e) => e.ev === 'server_download_action'), 'no delete for a bookmarked chapter').toBe(false);
        expect(await downloadedOrders(paper.id, client)).toEqual([1, 2, 4, 5]);
        expect((await serverChapters(paper.id, client))[3]).toMatchObject({ isRead: true, isBookmarked: true, isDownloaded: true });
        await detailsToBrowse(s);
        await setDeleteAfterReading(s, false);

        // 10. While the Library tab shows, nothing polls: a running queue (chapter 8, slowed), the tab polls; the Library
        // tab 5 s; the tab again: its first answer counts at most one poll more than the last before (one may have been out).
        setTestSourceControl({ delayMs: SLOW_MS });
        await enqueueOnServer([ch(8)], client);
        await untilServer('the downloader started', () => serverQueue(client), (q) => q.state === 'STARTED', 10_000);
        await openTab(s, 'the queue running', (e) => e.view === 'running' && e.rows === 1);
        mark = s.events.mark();
        await s.events.waitFor('a poll', (e) => e.ev === 'server_downloads', { from: mark, timeoutMs: 5_000 });
        const polledBefore = Number(lastOf(s, 'server_downloads')?.polls);
        await tapUntil(s, tab('Library'), 'the Library tab', isEv('library', { state: 'ready' }));
        await sleep(5_000);
        const again = await openTab(s, 'the tab again', () => true);
        console.log(`[server-downloads] polls: ${polledBefore} before the Library tab, ${again.polls} after 5 s there`);
        expect(Number(again.polls) - polledBefore, 'polls while the Library tab showed (+1: the tab\'s own read)').toBeLessThanOrEqual(2);
        await clearServerQueue(client);
        clearTestSourceControl();

        // 11. Switch only: 100 rows (slowed by 10 s), applied ≤ 50 ms; 10 rounds tab ↔ Library, the memory flat; Clear.
        if (s.driver.name === 'switch') {
            await tapUntil(s, tab('Library'), 'the Library tab', isEv('library', { state: 'ready' }));
            const many = await hundredChapters(client);
            touched.push(...many.manga);
            expect(many.ids.length).toBe(100);
            setTestSourceControl({ delayMs: 10_000 });
            await enqueueOnServer(many.ids, client);
            await untilServer('the downloader started', () => serverQueue(client), (q) => q.state === 'STARTED', 10_000);
            mark = s.events.mark();
            await openTab(s, '100 rows', (e) => e.rows === 100);
            await s.events.waitFor('a poll of 100 rows', (e) => e.ev === 'server_downloads' && e.rows === 100 && Number(e.ms) > 0,
                { from: mark, timeoutMs: 5_000 }).catch(() => undefined);
            const before = await memNow(s);
            for (let i = 0; i < ROUNDS; i++) {
                await tapUntil(s, tab('Library'), `round ${i + 1}: Library`, isEv('library', { state: 'ready' }));
                await openTab(s, `round ${i + 1}: 100 rows`, (e) => e.rows === 100);
            }
            const after = await memNow(s);
            const applied = s.events.events.filter((e) => e.ev === 'server_downloads' && e.rows === 100).map((e) => Number(e.ms));
            console.log(`[server-downloads] 100 rows applied in ≤ ${Math.max(...applied)} ms; ${ROUNDS} rounds ` +
                `${(before / MiB).toFixed(1)} -> ${(after / MiB).toFixed(1)} MiB`);
            expect(Math.max(...applied), 'a status of 100 rows applied (ms)').toBeLessThanOrEqual(LIMITS.status100Ms);
            expect(after - before, `memory growth over ${ROUNDS} tab rounds (bytes)`).toBeLessThan(LIMITS.roundsBytes);
            await tapUntil(s, SERVER_DOWNLOADS.clear, 'the clear question', isEv('dialog'));
            await tapUntil(s, DIALOG.confirm, 'cleared', isEv('server_download_action', { action: 'clear' }));
            expect((await serverQueue(client)).rows).toEqual([]);
            clearTestSourceControl();
        }

        // D58: every action's answer on screen ≤ 1 s (delete after reading runs in the background, after the reader closed).
        const actions = s.events.events.filter((e) => e.ev === 'server_download_action' && e.action !== 'delete_after_reading');
        const slowest = Math.max(...actions.map((e) => Number(e.ms)));
        console.log(`[server-downloads] ${s.driver.name}: ${actions.length} actions, the slowest ${slowest} ms`);
        expect(slowest, 'the slowest action → its answer (ms)').toBeLessThanOrEqual(LIMITS.actionMs);
        s.expectNoErrorEvents();
        await s.driver.quit();
    } finally {
        await knownState();
    }
}, { timeoutMs: driverName() === 'switch' ? 20 * 60_000 : 8 * 60_000 });
