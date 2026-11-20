// switch-download-queue (WP-18c, D59, TESTING.md): the Switch downloads queue's life, on the test source's Paper Garden (SNX
// Test en-14, Browse -> Latest's first, 8 chapters of 3 pages) and the Local manga Alpha and Delta Big (300 chapters).
// switch-downloads checks reading downloaded chapters; this one the queue. Checks, on desktop and on the Switch:
// 1. slowed (every source call waits 6 s): MINUS -> Download to Switch... -> Next 5 -> the tab: Chapters 1-5 in order,
//    Running, Chapter 1 downloading, keep-awake on ("keep_awake"). Once a page of Chapter 1 is on the SD card: Remove it ->
//    its folder gone, Chapter 2 downloads; Chapter 2 (downloading) to the bottom: it goes on downloading there; Chapter 4
//    to the top. Pause -> keep-awake off (golden "switch-queue-paused", state text masked); Resume -> on; the Library tab
//    -> off; the tab again -> on. Clear... -> empty, the chapters' folders gone;
// 2. page images fail: Chapter 1 -> "Failed at page 1 of 3" (golden "switch-failed"); the failure off -> Retry -> on the
//    Switch;
// 3. slowed: Chapter 2 -> quit once a page of it is on the SD card; the slowdown off; relaunch: the queue comes back and
//    Chapter 2 finishes, asking for none of its kept pages again ("net" events);
// 4. desktop only (the console reaches the test server by one address): Settings -> Server address -> the test server's LAN
//    address -> Continue -> "Is <LAN> the same server as <url>? Its 2 downloaded chapters ..." (golden "same-server") ->
//    Same server -> the same 2 chapters at the new address; server.json lists both;
// 5. relaunch with the SD card nearly full (test config sdFreeBytes 1 GiB + 4 KiB): the tab -> Delete all downloads... of
//    Paper Garden; Alpha's Chapter 1 -> "sd_full"; Delta Big -> All: 301 waiting; Switch only: 10 rounds tab <-> Library,
//    the memory flat (+ < 16 MiB); Clear... -> empty.
// Known state first and at the end, also after a failure: the switch file cleared, the server's page cache emptied (so the
// slowdown and the failure reach every page), Paper Garden's chapters unread, none of the test server's downloads on the
// SD card (desktop: its folder deleted; Switch: through the tab at the start of the next run).
// Needs the test server (tools/test-server/test-server.sh start).

import { basename } from 'node:path';

import { expect } from 'vitest';

import { mangaIds } from '../lib/chapters.ts';
import type { AppEvent } from '../lib/events.ts';
import { openManga, waitForLibrary } from '../lib/reader.ts';
import { scenario, type Scenario } from '../lib/scenario.ts';
import { CLOCK_MASK, DIALOG, LIBRARY_MORE, MANGA, SERVER_DOWNLOADS, SETUP, settingsRow, tab } from '../lib/screens.ts';
import { PAPER_GARDEN_KEY, patchChapters, sourceMangaWithChapters } from '../lib/server-downloads.ts';
import { appServerUrl, optionsFromEnv, SuwayomiClient } from '../lib/suwayomi.ts';
import { chapterPageFiles, removeTestServerFolders, testServerFolders } from '../lib/switch-downloads.ts';
import { clearServerPageCache, clearTestSourceControl, setTestSourceControl, TEST_SOURCE_IDS } from '../lib/test-source.ts';
import { typedText } from '../lib/touches.ts';

const MiB = 1024 * 1024;
const GiB = 1024 * MiB;
const ROUNDS = 10;
const LIMITS = { roundsBytes: 16 * MiB };
/** Every source call waits this long while the queue must be looked at mid-download (a chapter: ~18 s). */
const SLOW_MS = 6_000;
/** The tab mirrors the Server downloads tab's layout: the button and Clear... on top, one row per 104 px below. */
const SWITCH_TAB = SERVER_DOWNLOADS;
/** The details screen's footer hint "Menu" (MINUS): a tap opens the menu. */
const MENU_HINT = { x: 945, y: 683 };
/** "Same server?" with the desktop's two test server addresses (4 lines of text): its left button (golden same-server). */
const SAME_SERVER_BUTTON = { x: 459, y: 497 };
/** The Browse tab's rows and a source's bar (browse.test.ts). */
const BROWSE_ROW = (i: number) => ({ x: 834, y: 223 + 84 * i });
const SOURCE_LATEST = { x: 170, y: 123 };
const SOURCE_COVER_0 = { x: 96, y: 274 };
/** The tab's space line (free space differs per machine) and the rows' state text (timing). */
const SPACE_MASK = { x: 785, y: 105, w: 490, h: 70, why: 'the space line: free space differs per machine' };
const STATE_MASK = { x: 1000, y: 190, w: 280, h: 460, why: 'state text and progress (timing)' };
/** The footer's Y hint ("Manga") while a manga row has the focus: a tap does what Y does (desktop has no Y). */
const Y_HINT = { x: 812, y: 683 };

type Row = { chapter: number; state: string; done: number; pages: number; failedPage: number };

const isEv = (name: string, fields: Record<string, unknown> = {}) => (e: AppEvent) =>
    e.ev === name && Object.entries(fields).every(([k, v]) => e[k] === v);
const lastOf = (s: Scenario, ev: string) => s.events.events.filter((e) => e.ev === ev).at(-1);
const idsOf = (e: AppEvent) => (e.ids as number[] | undefined) ?? [];
const rowsOf = (e: AppEvent) => (e.rows as Row[] | undefined) ?? [];
const same = (a: number[], b: number[]) => JSON.stringify(a) === JSON.stringify(b);
const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

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

async function backTo(s: Scenario, screen: string): Promise<void> {
    const mark = s.events.mark();
    await s.driver.press('B');
    await s.events.waitForEvent('screen', { name: screen }, { from: mark });
    await s.events.waitForEvent('idle', {}, { from: mark });
}

function focusedView(s: Scenario): string {
    return String(lastOf(s, 'focus')?.view ?? '');
}

/** Presses `button` until the focused view's id contains `part` (switch-downloads.test.ts: a lost press is pressed again). */
async function pressUntilFocus(s: Scenario, button: 'UP' | 'DOWN' | 'LEFT' | 'RIGHT', part: string, max = 12): Promise<void> {
    for (let i = 0; i <= max; i++) {
        if (focusedView(s).includes(part)) return;
        if (i === max) break;
        const mark = s.events.mark();
        await s.driver.press(button);
        const moved = await s.events.waitForEvent('focus', {}, { from: mark, timeoutMs: 3_000 }).catch(() => undefined);
        if (moved) await s.events.waitForEvent('idle', {}, { from: mark });
    }
    throw new Error(`no focus on ${part} after ${max} presses of ${button} (last: ${focusedView(s)})`);
}

/** Waits for a "switch_downloads" event (from `from`, default: anything this launch) that `match` accepts. */
async function untilState(s: Scenario, what: string, match: (e: AppEvent) => boolean, from = 0, timeoutMs = 60_000): Promise<AppEvent> {
    return s.events.waitFor(what, (e) => e.ev === 'switch_downloads' && match(e), { from, timeoutMs });
}

/** Asks `read` every 250 ms until `done` accepts its answer (files the store's thread deletes a moment later). */
async function until<T>(what: string, read: () => Promise<T>, done: (value: T) => boolean, timeoutMs = 10_000): Promise<T> {
    const end = Date.now() + timeoutMs;
    for (;;) {
        const value = await read();
        if (done(value)) return value;
        if (Date.now() > end) throw new Error(`${what}: still ${JSON.stringify(value)} after ${timeoutMs} ms`);
        await sleep(250);
    }
}

/** Browse -> SNX Test (EN) -> Latest -> its first cover (Paper Garden): the details screen, complete. */
async function openPaperGarden(s: Scenario, id: number): Promise<AppEvent> {
    const onBrowse = lastOf(s, 'screen')?.name === 'Browse' && lastOf(s, 'sources') !== undefined;
    const sources = onBrowse ? lastOf(s, 'sources') as AppEvent
        : await tapUntil(s, tab('Browse'), 'the sources', isEv('sources', { state: 'ready' }));
    const row = (sources.rows as { id: string }[]).findIndex((r) => r.id === TEST_SOURCE_IDS.en);
    expect(row, 'the Browse tab lists SNX Test (EN)').toBeGreaterThanOrEqual(0);
    await tapUntil(s, BROWSE_ROW(row), 'SNX Test (EN)', isEv('browse', { source: TEST_SOURCE_IDS.en, mode: 'popular', state: 'ready' }));
    const latest = await tapUntil(s, SOURCE_LATEST, 'Latest', isEv('browse', { mode: 'latest', state: 'ready' }));
    expect((latest.titles as string[])[0]).toBe('Paper Garden');
    const details = await tapUntil(s, SOURCE_COVER_0, 'Paper Garden',
        (e) => e.ev === 'manga' && e.state === 'ready' && e.complete === true && e.id === id);
    if (details.sort !== 'oldest') {
        await tapSettled(s, MANGA.sort);
        await tapUntil(s, MANGA.menuEntry(1, 2), 'oldest first', (e) => e.ev === 'manga' && e.sort === 'oldest' && e.state === 'ready');
    }
    return details;
}

/** The details screen's back to the Browse tab (Manga -> Source -> Browse). */
async function detailsToBrowse(s: Scenario): Promise<void> {
    await backTo(s, 'Source');
    await backTo(s, 'Browse');
}

/** A source chapter row's menu (4 chapter actions, Download to Switch, the server entry) -> Download to Switch. */
async function sourceRowToSwitch(s: Scenario, row: number): Promise<AppEvent> {
    await tapSettled(s, MANGA.rowMenu(row));
    const done = await tapUntil(s, MANGA.menuEntry(4, 6), `row ${row}: Download to Switch`, isEv('switch_download_action', { action: 'enqueue' }));
    expect(done.ok).toBe(true);
    return done;
}

/** MINUS -> Download to Switch... (entry `entry` of 6) -> choice `index` of Next 1, Next 5, Next 10, Next 25, Unread, All. */
async function bulkToSwitch(s: Scenario, entry: number, index: number): Promise<AppEvent> {
    await tapSettled(s, MENU_HINT);
    await tapSettled(s, MANGA.menuEntry(entry, 6));
    const done = await tapUntil(s, MANGA.menuEntry(index, 6), `download choice ${index}`, isEv('switch_download_action', { action: 'enqueue' }));
    expect(done.ok).toBe(true);
    return done;
}

async function openTab(s: Scenario): Promise<void> {
    await tapUntil(s, tab('SwitchDownloads'), 'the Switch downloads tab', isEv('screen', { name: 'SwitchDownloads' }));
}

/** With the tab open: a tap on queue row `index` -> its menu -> entry `entry` of `count` (Move to top, Move to bottom,
 * Remove, [Retry,] Open the manga). */
async function rowAction(s: Scenario, index: number, entry: number, count: number, action: string): Promise<AppEvent> {
    await tapSettled(s, SWITCH_TAB.row(index));
    const done = await tapUntil(s, MANGA.menuEntry(entry, count), `row ${index}: ${action}`, isEv('switch_download_action', { action }));
    expect(done.ok, `${action} worked`).toBe(true);
    return done;
}

/** The tab's Clear... -> the question -> Clear queue; waits for the empty queue. Returns the question's text. */
async function clearQueue(s: Scenario): Promise<string> {
    const mark = s.events.mark();
    const ask = await tapUntil(s, SWITCH_TAB.clear, 'the clear question', isEv('dialog'));
    const done = await tapUntil(s, DIALOG.confirm, 'cleared', isEv('switch_download_action', { action: 'clear' }));
    expect(done.ok).toBe(true);
    await untilState(s, 'the queue empty', (e) => e.queued === 0, mark);
    return String(ask.text);
}

/** With the tab open and its queue empty: Y on the first manga row -> Delete all downloads... -> Delete, until none is left. */
async function deleteAllInTab(s: Scenario): Promise<void> {
    while (Number(lastOf(s, 'switch_downloads')?.manga ?? 0) > 0) {
        await pressUntilFocus(s, 'RIGHT', 'switch_downloads/', 3);
        await pressUntilFocus(s, 'DOWN', 'switch_downloads/manga-', 8);
        const before = Number(lastOf(s, 'switch_downloads')?.manga);
        const mark = s.events.mark();
        if (s.driver.name === 'switch') await s.driver.press('Y');
        else await s.driver.touch(Y_HINT.x, Y_HINT.y);
        await s.events.waitForEvent('idle', {}, { from: mark });
        await tapUntil(s, MANGA.menuEntry(1, 2), 'the delete question', isEv('dialog'));
        const done = await tapUntil(s, DIALOG.confirm, 'deleted', isEv('switch_download_action', { action: 'delete_manga' }));
        expect(done.ok).toBe(true);
        await untilState(s, 'one manga fewer', (e) => Number(e.manga) < before, mark);
    }
}

/** The Switch can't delete files from outside the app: what an earlier run left goes through the tab. */
async function deleteLeftovers(s: Scenario): Promise<void> {
    const loaded = await untilState(s, 'the downloads loaded', (e) => e.loaded === true);
    if (Number(loaded.manga) === 0 && Number(loaded.queued) === 0) return;
    await openTab(s);
    if (Number(loaded.queued) > 0) await clearQueue(s);
    await deleteAllInTab(s);
}

/** Waits for the next "mem" event (one every 10 s) and returns its "used". */
async function memNow(s: Scenario): Promise<number> {
    const mem = await s.events.waitForEvent('mem', {}, { from: s.events.mark(), timeoutMs: 15_000 });
    return Number(mem.used);
}

/** The page indexes of a chapter's files on the SD card ("0002.jpg" -> 2). */
async function pageIndexes(s: Scenario, key: string, mangaId: number, chapterId: number): Promise<number[]> {
    return (await chapterPageFiles(s.driver, key, mangaId, chapterId)).map((f) => Number(basename(f).slice(0, 4)));
}

scenario('switch-download-queue', async (s) => {
    const client = new SuwayomiClient(optionsFromEnv());
    const url = appServerUrl(s.driver.name);
    const paper = await sourceMangaWithChapters(PAPER_GARDEN_KEY, client);
    expect(paper.chapters.map((c) => c.sourceOrder)).toEqual([1, 2, 3, 4, 5, 6, 7, 8]);
    const ch = (order: number) => paper.chapters[order - 1].id;
    const ids = (...orders: number[]) => orders.map(ch);
    const library = await mangaIds(client);
    const delta = library.get('Delta Big') ?? 0;
    expect(delta, 'the test server has Delta Big: tools/test-server/test-server.sh reset').toBeGreaterThan(0);
    const knownState = async () => {
        clearTestSourceControl();
        await clearServerPageCache(client);
        await patchChapters(paper.chapters.map((c) => c.id), { isRead: false, isBookmarked: false, lastPageRead: 0 }, client);
        await removeTestServerFolders(s.driver, url);
    };
    await knownState();

    try {
        await s.launch({ testConfig: { server: { url } } });
        await waitForLibrary(s);
        if (s.driver.name === 'switch') await deleteLeftovers(s);
        const loaded = await untilState(s, 'the downloads loaded', (e) => e.loaded === true);
        expect(loaded.manga, 'nothing of the test server on the SD card').toBe(0);

        // 1. Slowed: Next 5 -> the tab: 1-5, Running, keep-awake on.
        await openPaperGarden(s, paper.id);
        setTestSourceControl({ delayMs: SLOW_MS });
        let mark = s.events.mark();
        const five = await bulkToSwitch(s, 4, 1);  // not in the library: Sort, Show, Refresh, Add to library, Download to Switch..., ...
        expect(five.chapters).toBe(5);
        await detailsToBrowse(s);
        await openTab(s);
        const running = await untilState(s, 'Chapter 1 downloading', (e) => e.view === 'running' && e.downloading === ch(1), mark);
        expect(idsOf(running)).toEqual(ids(1, 2, 3, 4, 5));
        await s.events.waitFor('keep-awake on', isEv('keep_awake', { on: true, by: 'switch_downloads' }), { from: mark });

        // Remove Chapter 1 once a page of it is on the SD card: its folder goes, Chapter 2 downloads.
        await untilState(s, 'a page of Chapter 1 on the SD card', (e) => e.downloading === ch(1) && Number(e.pagesDone) >= 1, mark, 60_000);
        const [key] = await testServerFolders(s.driver, url);
        expect(key, 'the test server has its own downloads folder').toBeTruthy();
        expect((await pageIndexes(s, key, paper.id, ch(1))).length).toBeGreaterThanOrEqual(1);
        mark = s.events.mark();
        await rowAction(s, 0, 2, 4, 'remove');
        await untilState(s, 'Chapter 1 removed, Chapter 2 downloading', (e) => same(idsOf(e), ids(2, 3, 4, 5)) && e.downloading === ch(2), mark);
        await until('Chapter 1\'s pages gone', () => pageIndexes(s, key, paper.id, ch(1)), (p) => p.length === 0);

        // Chapter 2 (downloading) to the bottom: it goes on downloading there. Chapter 4 to the top.
        mark = s.events.mark();
        await rowAction(s, 0, 1, 4, 'bottom');
        const bottom = await untilState(s, 'Chapter 2 at the bottom', (e) => same(idsOf(e), ids(3, 4, 5, 2)), mark);
        expect(bottom.downloading, 'the downloading chapter goes on where it ends up').toBe(ch(2));
        mark = s.events.mark();
        await rowAction(s, 1, 0, 4, 'top');
        await untilState(s, 'Chapter 4 at the top', (e) => same(idsOf(e), ids(4, 3, 5, 2)), mark);

        // Pause: keep-awake off; Resume: on; another tab: off; the tab again: on.
        mark = s.events.mark();
        await tapUntil(s, SWITCH_TAB.primary, 'paused', isEv('switch_download_action', { action: 'pause' }));
        await untilState(s, 'the tab shows it paused', (e) => e.view === 'paused' && e.paused === true, mark);
        await s.events.waitFor('keep-awake off (paused)', isEv('keep_awake', { on: false }), { from: mark });
        await s.expectGolden('switch-queue-paused', { masks: [CLOCK_MASK, SPACE_MASK, STATE_MASK] });
        mark = s.events.mark();
        await tapUntil(s, SWITCH_TAB.primary, 'resumed', isEv('switch_download_action', { action: 'resume' }));
        await s.events.waitFor('keep-awake on (resumed)', isEv('keep_awake', { on: true }), { from: mark, timeoutMs: 15_000 });
        mark = s.events.mark();
        await tapUntil(s, tab('Library'), 'the Library tab', isEv('screen', { name: 'Library' }));
        await s.events.waitFor('keep-awake off (another tab)', isEv('keep_awake', { on: false }), { from: mark });
        mark = s.events.mark();
        await openTab(s);
        await s.events.waitFor('keep-awake on (the tab again)', isEv('keep_awake', { on: true }), { from: mark, timeoutMs: 15_000 });
        mark = s.events.mark();
        const asked = await clearQueue(s);
        expect(asked).toBe('Remove all 4 chapters from the queue? Chapters already downloaded stay.');
        await s.events.waitFor('keep-awake off (nothing to download)', isEv('keep_awake', { on: false }), { from: mark });
        for (const order of [2, 3, 4, 5]) {
            await until(`Chapter ${order}'s pages gone`, () => pageIndexes(s, key, paper.id, ch(order)), (p) => p.length === 0);
        }
        clearTestSourceControl();

        // 2. Page images fail: Chapter 1 -> failed at page 1 of 3; the failure off -> Retry -> on the Switch.
        await clearServerPageCache(client);
        setTestSourceControl({ fail: [{ lang: 'en', call: 'image' }] });
        await openPaperGarden(s, paper.id);
        mark = s.events.mark();
        await sourceRowToSwitch(s, 0);
        const failedDownload = await s.events.waitFor('Chapter 1 failed', isEv('switch_download', { chapter: ch(1) }), { from: mark, timeoutMs: 60_000 });
        expect(failedDownload.ok).toBe(false);
        await detailsToBrowse(s);
        await openTab(s);
        const failed = lastOf(s, 'switch_downloads') as AppEvent;
        expect(failed).toMatchObject({ view: 'stopped', failed: 1, queued: 1 });
        expect(rowsOf(failed)[0]).toMatchObject({ chapter: ch(1), state: 'failed', failedPage: 0, pages: 3 });
        await s.expectGolden('switch-failed', { masks: [CLOCK_MASK, SPACE_MASK] });
        clearTestSourceControl();
        mark = s.events.mark();
        await rowAction(s, 0, 3, 5, 'retry');  // a failed row: Move to top, Move to bottom, Remove, Retry, Open the manga
        const retried = await s.events.waitFor('Chapter 1 on the Switch', isEv('switch_download', { chapter: ch(1) }), { from: mark, timeoutMs: 60_000 });
        expect(retried.ok, String(retried.error ?? '')).toBe(true);
        expect(retried.pages).toBe(3);

        // 3. Quit in the middle of Chapter 2; relaunched, it finishes without asking for its kept pages again.
        await clearServerPageCache(client);
        await openPaperGarden(s, paper.id);
        setTestSourceControl({ delayMs: SLOW_MS });
        mark = s.events.mark();
        await sourceRowToSwitch(s, 1);
        await untilState(s, 'a page of Chapter 2 on the SD card', (e) => e.downloading === ch(2) && Number(e.pagesDone) >= 1, mark, 60_000);
        await s.driver.quit();
        clearTestSourceControl();
        const kept = await pageIndexes(s, key, paper.id, ch(2));
        expect(kept.length, 'some of Chapter 2\'s pages were on the SD card at the quit').toBeGreaterThanOrEqual(1);
        expect(kept.length, 'not all of them').toBeLessThan(3);
        await s.launch({ testConfig: { server: { url } } });
        await waitForLibrary(s);
        await untilState(s, 'the queue back from queue.json', (e) => e.loaded === true && same(idsOf(e), ids(2)));
        const resumed = await s.events.waitFor('Chapter 2 finished', isEv('switch_download', { chapter: ch(2) }), { timeoutMs: 60_000 });
        expect(resumed.ok, String(resumed.error ?? '')).toBe(true);
        const pageUrl = new RegExp(`/manga/${paper.id}/chapter/2/page/(\\d+)`);
        const fetched = s.events.events.filter((e) => e.ev === 'net' && pageUrl.test(String(e.url)))
            .map((e) => Number(pageUrl.exec(String(e.url))?.[1]));
        console.log(`[switch-download-queue] kept at the quit: pages ${kept.join(', ')}; fetched after: ${fetched.join(', ')}`);
        expect(fetched.filter((i) => kept.includes(i)), 'no kept page is asked for again').toEqual([]);
        expect(await pageIndexes(s, key, paper.id, ch(2))).toEqual([0, 1, 2]);

        // 4. Desktop only: "Same server?" for the test server's LAN address -> Same server -> the same downloads there.
        if (s.driver.name === 'desktop') {
            const lan = appServerUrl('switch');
            expect(lan, 'the LAN address is another spelling of the test server').not.toBe(url);
            await tapUntil(s, tab('Settings'), 'the Settings tab', isEv('screen', { name: 'Settings' }));
            await tapUntil(s, settingsRow('address', false), 'the setup screen', isEv('screen', { name: 'Setup' }));
            await typedText(s, lan);
            await tapSettled(s, SETUP.url);  // the field takes the typed text (test builds), no keyboard
            mark = s.events.mark();
            await s.driver.touch(SETUP.fromSettings.continue.x, SETUP.fromSettings.continue.y);
            const tested = await s.events.waitForEvent('server_check', { where: 'setup' }, { from: mark, timeoutMs: 30_000 });
            expect(tested.ok, `the LAN address answers from WSL: ${JSON.stringify(tested)}`).toBe(true);
            const ask = await s.events.waitFor('the question', isEv('dialog'), { from: mark, timeoutMs: 10_000 });
            expect(ask.text).toBe(`Is ${lan} the same server as ${url}? Its 2 downloaded chapters stay with ${url} otherwise.`);
            await s.events.waitForEvent('idle', {}, { from: s.events.events.indexOf(ask) });
            await s.expectGolden('same-server', { masks: [CLOCK_MASK] });
            mark = s.events.mark();
            // A tap: after touches, borealis spends a first key press on showing the focus again. The 4-line question puts
            // the buttons lower than DIALOG's.
            await s.driver.touch(SAME_SERVER_BUTTON.x, SAME_SERVER_BUTTON.y);
            const answer = await s.events.waitFor('the answer', isEv('same_server'), { from: mark });
            expect(answer).toMatchObject({ old: url, new: lan, chapters: 2, same: true });
            await s.events.waitForEvent('server_check', { where: 'main', ok: true }, { from: mark, timeoutMs: 30_000 });
            await untilState(s, 'the same 2 chapters at the LAN address', (e) => e.loaded === true && e.chapters === 2, mark);
            const server = JSON.parse((await s.driver.readFile(`downloads/${key}/server.json`)).toString('utf8')) as { urls: string[] };
            expect(server.urls).toEqual([url, lan]);
            expect(await testServerFolders(s.driver, lan), 'no folder of its own for the LAN address').toEqual([key]);
        }
        s.expectNoErrorEvents();
        await s.driver.quit();

        // 5. The SD card nearly full: Paper Garden deleted through the tab; Alpha waits sd_full; Delta Big's 300 join it.
        await s.launch({ testConfig: { server: { url }, sdFreeBytes: GiB + 4096 } });
        await waitForLibrary(s);
        await untilState(s, 'the downloads loaded', (e) => e.loaded === true && e.manga === 1);
        await openTab(s);
        await deleteAllInTab(s);
        await tapUntil(s, tab('Library'), 'the Library tab', isEv('screen', { name: 'Library' }));
        await openManga(s, LIBRARY_MORE.reading, 0, 3);  // Alpha, the first of Reading's 3
        mark = s.events.mark();
        await tapSettled(s, MANGA.rowMenu(2));  // Chapter 1 (newest first); Local: 4 chapter actions + Download to Switch
        await tapUntil(s, MANGA.menuEntry(4, 5), 'Alpha Chapter 1: Download to Switch', isEv('switch_download_action', { action: 'enqueue' }));
        await untilState(s, 'waiting: the SD card nearly full', (e) => e.view === 'waiting' && e.wait === 'sd_full' && e.queued === 1, mark);
        await backTo(s, 'Library');
        const deltaShown = await openManga(s, LIBRARY_MORE.longSeries, 0, 2);  // "Delta Big" < "Gamma Webtoon"
        expect(deltaShown.id).toBe(delta);
        mark = s.events.mark();
        const all = await bulkToSwitch(s, 5, 5);  // in the library: Sort, Show, Refresh, Categories, Remove, Download to Switch...
        expect(all.chapters).toBe(300);
        await untilState(s, '301 waiting', (e) => e.queued === 301 && e.wait === 'sd_full', mark);
        await backTo(s, 'Library');
        await openTab(s);
        if (s.driver.name === 'switch') {
            const before = await memNow(s);
            for (let i = 0; i < ROUNDS; i++) {
                await tapUntil(s, tab('Library'), `round ${i + 1}: Library`, isEv('screen', { name: 'Library' }));
                await openTab(s);
            }
            const after = await memNow(s);
            console.log(`[switch-download-queue] ${ROUNDS} rounds with 301 rows: ${(before / MiB).toFixed(1)} -> ${(after / MiB).toFixed(1)} MiB`);
            expect(after - before, `memory growth over ${ROUNDS} tab rounds (bytes)`).toBeLessThan(LIMITS.roundsBytes);
        }
        expect(await clearQueue(s)).toBe('Remove all 301 chapters from the queue? Chapters already downloaded stay.');
        s.expectNoErrorEvents();
        await s.driver.quit();
    } finally {
        await knownState();
    }
}, { timeoutMs: 12 * 60_000 });
