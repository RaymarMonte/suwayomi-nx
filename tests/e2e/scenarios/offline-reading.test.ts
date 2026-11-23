// offline-reading (WP-18b, D61, TESTING.md): reading away from home. On the test server's Local manga Alpha (3 chapters of
// 10 JPEG pages, in the Reading category), on desktop and on the Switch:
// 1. online: Alpha's Chapters 1 and 2 downloaded to the Switch (the details screen's menu); a "downloaded_progress" refresh
//    after the check knows both; quit;
// 2. the server stopped, the app starts: offline ("connection" offline / cant_reach, no Retry screen), the Library shows one
//    shelf "On this Switch" with Alpha (a "library" event offline, 1 item); Alpha opens offline ("manga" offline, 2
//    chapters); Mark read answers "This needs the server." (a toast, no chapter_action);
// 3. Chapter 1 opens at once from its files (no chapterState, no request), 3 page turns; its saves go to the waiting-save
//    queue (progress_queue "added", held while the reader is open, `ok` progress events); close: the hold ends; the list
//    shows page 3; quit (the queue is on the SD card);
// 3b. no network (test config networkConnected false): offline at once without a request, the shelf all the same;
// 4. the server starts, the app starts: the check works, the queue sends Chapter 1's page 3 ("sent"), the server has it, and
//    the refresh learns it; the Library is the server's again ("library" offline false);
// 5. back online by the timer: the server stopped, the app starts offline (offlineCheckMs 3000), the server starts: the
//    "connection" event online by "timer", the note, the Library swaps back.
// Known state first and at the end: Alpha's chapters unread at page 0, none of the test server's downloads on the SD card.
// Switch: the device steps (HOME / sleep while offline, the owner's away-from-home try) are in docs/STATUS.md.
// Needs the test server (tools/test-server/test-server.sh start); it starts it again if a step stopped it.

import { expect } from 'vitest';

import { chaptersOf, mangaIds } from '../lib/chapters.ts';
import type { AppEvent } from '../lib/events.ts';
import { openManga, readerAfter, waitForLibrary } from '../lib/reader.ts';
import { scenario, type Scenario } from '../lib/scenario.ts';
import { LIBRARY, LIBRARY_MORE, MANGA, READER } from '../lib/screens.ts';
import { patchChapters, serverChapters } from '../lib/server-downloads.ts';
import { appServerUrl, optionsFromEnv, startServer, stopServer, SuwayomiClient } from '../lib/suwayomi.ts';
import { removeTestServerFolders } from '../lib/switch-downloads.ts';

/** A stopped server can take a while to fail on the console (a firewall that drops: 5 s per try, 3 tries). */
const SLOW = { timeoutMs: 90_000 };

const isEv = (name: string, fields: Record<string, unknown> = {}) => (e: AppEvent) =>
    e.ev === name && Object.entries(fields).every(([k, v]) => e[k] === v);

/** Taps (x, y), waits for the first event that `match` accepts and for the screen to settle. */
async function tapUntil(s: Scenario, at: { x: number; y: number }, what: string, match: (e: AppEvent) => boolean,
    timeoutMs = 30_000): Promise<AppEvent> {
    const mark = s.events.mark();
    await s.driver.touch(at.x, at.y);
    const read = s.events.mark(); // desktop: the app has read the release by now (IS-005)
    const e = await s.events.waitFor(what, match, { from: mark, timeoutMs });
    await s.events.waitForIdleAfter(read, e);
    return e;
}

async function tapSettled(s: Scenario, at: { x: number; y: number }): Promise<void> {
    await s.driver.touch(at.x, at.y);
    await s.events.waitForIdleAfter(s.events.mark()); // desktop: after the app read the release (IS-005)
}

/** Row `row`'s menu → Download to Switch (Local: 4 actions + the Switch entry), until that chapter is on the SD card. */
async function downloadRow(s: Scenario, row: number, chapterId: number): Promise<void> {
    await tapSettled(s, MANGA.rowMenu(row));
    let action = await tapUntil(s, MANGA.menuEntry(4, 5), `row ${row}: the Switch entry`, isEv('switch_download_action'));
    if (action.action === 'delete') {
        // On the Switch a run that stopped early leaves its downloads (the driver can't delete there): that tap deleted
        // the leftover, this one downloads it again, so the steps below start from the same state.
        await tapSettled(s, MANGA.rowMenu(row));
        action = await tapUntil(s, MANGA.menuEntry(4, 5), `row ${row}: Download to Switch`, isEv('switch_download_action'));
    }
    expect(action.action, `row ${row}: Download to Switch`).toBe('enqueue');
    expect(action.ok, 'the enqueue worked').toBe(true);
    const done = await s.events.waitFor(`chapter ${chapterId} on the Switch`, isEv('switch_download', { chapter: chapterId }),
        { from: s.events.events.indexOf(action), timeoutMs: 60_000 });
    expect(done.ok, `chapter ${chapterId} downloaded (${String(done.error ?? '')})`).toBe(true);
}

/** The Switch: the driver can't delete folders there (S10), so the downloads go through the app (rows 1 and 2 = Chapters 2
 *  and 1: newest first; the row menu's 5th entry is Delete from Switch for a downloaded chapter, no question). */
async function deleteDownloads(s: Scenario): Promise<void> {
    if (s.driver.name !== 'switch') return;
    await openManga(s, LIBRARY_MORE.reading, 0, 3);
    for (const row of [1, 2]) {
        await tapSettled(s, MANGA.rowMenu(row));
        const deleted = await tapUntil(s, MANGA.menuEntry(4, 5), `row ${row}: Delete from Switch`,
            isEv('switch_download_action', { action: 'delete' }));
        expect(deleted.ok, `row ${row} deleted from the Switch`).toBe(true);
    }
}

/** Where a tap turns to the next page: the left edge when the reader goes right to left. */
function nextPage(reader: AppEvent): { x: number; y: number } {
    return reader.direction === 'rtl' ? READER.left : READER.right;
}

/** The menu's Close (opening the menu first): back on the details screen once it has settled. */
async function closeReader(s: Scenario): Promise<void> {
    await readerAfter(s, () => s.driver.touch(READER.middle.x, READER.middle.y), { menu: true });
    const mark = s.events.mark();
    await s.driver.touch(READER.close.x, READER.close.y);
    await s.events.waitForEvent('screen', { name: 'Manga' }, { from: mark });
    await s.events.waitForEvent('idle', {}, { from: mark });
}

/** The offline Library after a start without the server: the connection event, then the shelf. */
async function offlineStart(s: Scenario, from: number): Promise<AppEvent> {
    const away = await s.events.waitFor('offline', isEv('connection', { state: 'offline' }), { from, ...SLOW });
    expect(away.reason).toBe('cant_reach');
    const shelf = await s.events.waitFor('the offline Library', isEv('library', { offline: true }), { from, ...SLOW });
    await s.events.waitForEvent('idle', {}, { from: s.events.events.indexOf(shelf) });
    return shelf;
}

scenario('offline-reading', async (s) => {
    const client = new SuwayomiClient(optionsFromEnv());
    const url = appServerUrl(s.driver.name);
    startServer();
    const alpha = (await mangaIds(client)).get('Alpha') ?? 0;
    expect(alpha, 'the test server has Alpha: tools/test-server/test-server.sh reset').toBeGreaterThan(0);
    const chapters = (await serverChapters(alpha, client)).sort((a, b) => a.sourceOrder - b.sourceOrder);
    expect(chapters).toHaveLength(3);
    const [one, two, three] = chapters.map((c) => c.id);
    const knownState = async () => {
        startServer();
        await patchChapters([one, two, three], { isRead: false, isBookmarked: false, lastPageRead: 0 }, client);
        await removeTestServerFolders(s.driver, url);
    };
    await knownState();

    try {
        // 1. Online: Chapters 1 and 2 (rows 2 and 1: newest first) downloaded to the Switch.
        await s.launch({ testConfig: { server: { url } } });
        await waitForLibrary(s);
        await openManga(s, LIBRARY_MORE.reading, 0, 3);
        await downloadRow(s, 2, one);
        await downloadRow(s, 1, two);
        await s.driver.quit();

        // 2. The server stopped: the app starts offline, the Library is what's on this Switch.
        // Each launch has its own event stream: `from: 0` is everything of this launch. `keepConfig`: the waiting-save queue
        // (progress-queue.json) stays from launch to launch, as on a real Switch.
        stopServer();
        await s.launch({ testConfig: { server: { url } }, keepConfig: true });
        let mark = 0;
        const shelf = await offlineStart(s, mark);
        expect(shelf.items, 'one manga on the Switch').toBe(1);
        expect(shelf.categories).toBe(1);
        const check = await s.events.waitForEvent('server_check', { where: 'main' }, { from: mark });
        expect(check.ok).toBe(false);
        const details = await tapUntil(s, LIBRARY.cover(0), 'Alpha offline',
            (e) => e.ev === 'manga' && e.offline === true && e.state === 'ready');
        expect(details.items, 'the downloaded chapters only').toBe(2);
        // Mark read needs the server: a toast, nothing changes.
        mark = s.events.mark();
        await tapSettled(s, MANGA.rowMenu(1));
        await tapSettled(s, MANGA.menuEntry(0, 5));
        expect(s.events.events.slice(mark).filter((e) => e.ev === 'chapter_action'), 'no action offline').toEqual([]);

        // 3. Chapter 1 (row 1 now: Chapters 2 and 1) at once from its files; 3 page turns go to the queue.
        mark = s.events.mark();
        const opened = await readerAfter(s, () => s.driver.touch(MANGA.row(1).x, MANGA.row(1).y), { shown: 'page', chapterId: one });
        for (let turn = 1; turn <= 3; turn++) {
            await readerAfter(s, () => s.driver.touch(nextPage(opened).x, nextPage(opened).y), { shown: 'page', page: turn });
        }
        await closeReader(s);
        const since = s.events.events.slice(mark);
        const requests = since.filter((e) => e.ev === 'net' && /graphql|\/chapter\/\d+\/page\//.test(String(e.url)));
        expect(requests.map((e) => e.url), 'no request while reading offline').toEqual([]);
        const added = since.filter((e) => e.ev === 'progress_queue' && e.what === 'added' && e.chapterId === one);
        expect(added.length, 'the saves went to the queue').toBeGreaterThanOrEqual(1);
        expect(added.at(-1)?.page).toBe(3);
        expect(added.some((e) => e.held === true), 'held while the reader was open').toBe(true);
        const saved = since.filter((e) => e.ev === 'progress' && e.chapterId === one);
        expect(saved.every((e) => e.ok === true), 'offline saves count as saved').toBe(true);
        const list = await s.events.waitFor('the list again', (e) => e.ev === 'manga' && e.offline === true && e.state === 'ready',
            { from: mark });
        expect(list.continue, 'Continue follows the offline read').toMatchObject({ kind: 'continue', chapter: one, page: 3 });
        await s.driver.quit();

        // 3b. No network (airplane mode): offline at once, no request, and still the shelf. The check ends before the
        //     downloads are read from the SD card; the Library must swap in once they are (found on the console).
        await s.launch({ testConfig: { server: { url }, networkConnected: false }, keepConfig: true });
        const noNetwork = await s.events.waitFor('offline', isEv('connection', { state: 'offline' }), { from: 0, ...SLOW });
        expect(noNetwork.reason).toBe('no_network');
        const noNetworkShelf = await s.events.waitFor('the offline Library', isEv('library', { offline: true }),
            { from: 0, timeoutMs: 15_000 });
        expect(noNetworkShelf.items).toBe(1);
        expect(s.events.events.filter((e) => e.ev === 'net' && /graphql/.test(String(e.url))), 'no request').toEqual([]);
        await s.events.waitForEvent('idle', {}, { from: s.events.events.indexOf(noNetworkShelf) });
        await s.driver.quit();

        // 4. The server starts: the queue sends the page, the server has it, the refresh learns it.
        startServer();
        await s.launch({ testConfig: { server: { url } }, keepConfig: true });
        mark = 0;
        await waitForLibrary(s);
        const sent = await s.events.waitFor('chapter 1 sent', isEv('progress_queue', { what: 'sent', chapterId: one }),
            { from: mark, timeoutMs: 60_000 });
        expect(sent.page).toBe(3);
        const onServer = (await chaptersOf(client, alpha)).find((c) => c.id === one);
        expect(onServer?.lastPageRead, 'the server has the offline page').toBe(3);
        const refreshed = await s.events.waitFor('the refresh', isEv('downloaded_progress'), { from: mark, timeoutMs: 60_000 });
        expect(refreshed.ok).toBe(true);
        expect(refreshed.chapters).toBe(2);
        await s.driver.quit();

        // 5. Back online by the timer.
        stopServer();
        await s.launch({ testConfig: { server: { url }, offlineCheckMs: 3000 } });
        mark = 0;
        await offlineStart(s, mark);
        startServer();
        const back = await s.events.waitFor('online again', isEv('connection', { state: 'online' }), { from: mark, ...SLOW });
        expect(['timer', 'focus']).toContain(back.by);
        await s.events.waitFor('the server\'s Library', (e) => e.ev === 'library' && e.offline === false && e.state === 'ready',
            { from: mark, ...SLOW });
        await s.events.waitForEvent('idle', {}, { from: mark });
        await deleteDownloads(s);  // the scenarios after this one expect nothing of the test server on the SD card
        await s.driver.quit();
    } finally {
        await knownState();
    }
}, { timeoutMs: 8 * 60_000 });
