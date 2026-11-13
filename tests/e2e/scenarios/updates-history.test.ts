// updates-history (WP-15, TESTING.md 17): the Updates and History tabs on the test server. On desktop and on the Switch:
// - a known state first (GraphQL): every chapter but Epsilon Weekly's unread at page 0, Epsilon's held-back chapter 3 gone;
// - Updates is empty (golden "updates-empty"); the new chapter appears on the server (addNewChapter), Update library finds it:
//   1 manga updated, 156 skipped, 1 new chapter, the row "Today" + Epsilon Weekly Chapter 3 (golden "updates-new");
// - a tap on the row opens that chapter (page code 8 / 3 / 1), one page on, B: the row says "Page 2", History has Epsilon first;
// - Alpha chapter 1 to page 3 and chapter 2 to page 2 in the reader: History shows one Alpha row, with chapter 2 (golden
//   "history"); a tap on it continues chapter 2 at page 2;
// - X in Updates marks Epsilon's chapter 3 read on the server, X again unread; a tap on a row's cover opens the details screen;
// - Delta Big stamped through GraphQL (chapter 150 at page 2, as if read on another device): X refreshes History, a tap
//   continues it at page 2 (the "chapter_open" event says how long the open took).
// The waiting-save queue steps come with WP-15b. Needs the test server; puts back what it changed.

import { expect } from 'vitest';

import { chaptersOf, mangaIds, resetChapters } from '../lib/chapters.ts';
import type { AppEvent } from '../lib/events.ts';
import { expectPage, openChapter, openManga, readerAfter, waitForLibrary } from '../lib/reader.ts';
import { scenario, type Scenario } from '../lib/scenario.ts';
import { CLOCK_MASK, LIBRARY_MORE, tab, UPDATES } from '../lib/screens.ts';
import {
    addNewChapter,
    appServerUrl,
    EPSILON_MANGA_ID,
    optionsFromEnv,
    removeNewChapter,
    SuwayomiClient,
} from '../lib/suwayomi.ts';

interface Row {
    chapterId: number;
    mangaId: number;
    title: string;
    chapter: string;
    page: number;
    read: boolean;
    day: string;
}

function rowsOf(e: AppEvent): Row[] {
    return (e.first ?? []) as Row[];
}

/** Every chapter of every manga but Epsilon Weekly unread at page 0 (keeps them out of History and the update), one call. */
async function knownState(client: SuwayomiClient): Promise<void> {
    const data = await client.query<{ chapters: { nodes: { id: number }[] } }>(
        `query K($e: Int!) { chapters(filter: { mangaId: { notEqualTo: $e } }) { nodes { id } } }`, { e: EPSILON_MANGA_ID });
    const ids = data.chapters.nodes.map((c) => c.id);
    await client.query(`mutation U($ids: [Int!]!) { updateChapters(input: { ids: $ids,
        patch: { isRead: false, lastPageRead: 0 } }) { chapters { id } } }`, { ids });
    await removeNewChapter(client);
    // Epsilon's two chapters read (the seed's state): an update only looks at manga with nothing left to read.
    const epsilon = (await chaptersOf(client, EPSILON_MANGA_ID)).map((c) => c.id);
    await client.query(`mutation R($ids: [Int!]!) { updateChapters(input: { ids: $ids, patch: { isRead: true } }) {
        chapters { id } } }`, { ids: epsilon });
}

/** Taps a main-screen tab and waits for its list event (`updates` / `history`) that matches `fields`. */
async function showTab(s: Scenario, name: 'Updates' | 'History', fields: Record<string, unknown> = {}): Promise<AppEvent> {
    const mark = s.events.mark();
    await s.driver.touch(tab(name).x, tab(name).y);
    const shown = await s.events.waitForEvent(name.toLowerCase(), { state: 'ready', ...fields }, { from: mark, timeoutMs: 30_000 });
    await s.events.waitForEvent('idle', {}, { from: mark });
    return shown;
}

/** Waits for a list event after `from` whose first row matches. */
async function firstRow(s: Scenario, ev: 'updates' | 'history', from: number, what: string,
    match: (row: Row) => boolean): Promise<AppEvent> {
    return s.events.waitFor(what, (e) => e.ev === ev && e.state === 'ready' && rowsOf(e).length > 0 && match(rowsOf(e)[0]),
        { from, timeoutMs: 30_000 });
}

/**
 * From the tab list onto the rows with RIGHT (X and Y act on the focused row). After a touch, borealis's first press only
 * brings the focus highlight back and moves nothing, so this presses until a "focus" event names a row.
 */
async function focusRows(s: Scenario, list: 'updates' | 'history'): Promise<void> {
    for (let i = 0; i < 3; i++) {
        const focus = s.events.events.filter((e) => e.ev === 'focus').at(-1);
        if (String(focus?.view ?? '').includes(`${list}/row-`)) return;
        const mark = s.events.mark();
        await s.driver.press('RIGHT');
        try {
            await s.events.waitForEvent('focus', {}, { from: mark, timeoutMs: 2_000 });
        } catch {
            // the press only showed the highlight: press again
        }
    }
    throw new Error(`RIGHT didn't bring the focus onto the ${list} rows`);
}

/** B in the reader: waits for the saved progress, then for the screen below. */
async function closeReader(s: Scenario, chapterId: number, page: number, screen: string): Promise<void> {
    const mark = s.events.mark();
    await s.driver.press('B');
    const saved = await s.events.waitForEvent('progress', { chapterId, page }, { from: mark });
    expect(saved.ok, JSON.stringify(saved)).toBe(true);
    await s.events.waitForEvent('screen', { name: screen }, { from: mark });
    await s.events.waitForEvent('idle', {}, { from: mark });
}

scenario('updates-history', async (s) => {
    const url = appServerUrl(s.driver.name);
    const client = new SuwayomiClient(optionsFromEnv());
    const ids = await mangaIds(client);
    const alpha = ids.get('Alpha') ?? 0;
    const delta = ids.get('Delta Big') ?? 0;
    expect(alpha && delta && ids.get('Epsilon Weekly') === EPSILON_MANGA_ID,
        'the test server has Alpha, Delta Big and Epsilon Weekly: tools/test-server/test-server.sh reset').toBeTruthy();
    await knownState(client);
    const alphaChapters = await chaptersOf(client, alpha);
    const deltaChapters = await chaptersOf(client, delta);
    const masks = [CLOCK_MASK, UPDATES.statusMask];

    try {
        await s.launch({ testConfig: { server: { url } } });
        await waitForLibrary(s);

        // 1. Updates: nothing new yet.
        let shown = await showTab(s, 'Updates', { state: 'empty' });
        expect(shown, JSON.stringify(shown)).toMatchObject({ rows: 0, end: true });
        await s.expectGolden('updates-empty', { masks });

        // 2. A new chapter on the server; Update library finds it.
        addNewChapter();
        let mark = s.events.mark();
        await s.driver.touch(UPDATES.updateButton.x, UPDATES.updateButton.y);
        await s.events.waitForEvent('library_update', { phase: 'started' }, { from: mark });
        const done = await s.events.waitForEvent('library_update', { phase: 'done' }, { from: mark, timeoutMs: 60_000 });
        expect(done, JSON.stringify(done)).toMatchObject({ total: 1, skipped: 156, newChapters: 1, failed: 0 });
        shown = await firstRow(s, 'updates', mark, 'Epsilon Weekly chapter 3 in Updates',
            (row) => row.mangaId === EPSILON_MANGA_ID && row.chapter === 'Chapter 3');
        expect(rowsOf(shown)[0]).toMatchObject({ title: 'Epsilon Weekly', page: 0, read: false, day: 'Today' });
        expect(shown.rows).toBe(1);
        await s.events.waitForEvent('idle', {}, { from: s.events.events.indexOf(shown) });
        await s.expectGolden('updates-new', { masks });
        const newChapter = rowsOf(shown)[0].chapterId;

        // 3. The row opens that chapter at its first page; one page on, B: the row and History show the page.
        mark = s.events.mark();
        let reader = await readerAfter(s, () => s.driver.touch(UPDATES.row(0).x, UPDATES.row(0).y),
            { shown: 'page', chapterId: newChapter });
        expect(reader.page).toBe(0);
        const opened = await s.events.waitForEvent('chapter_open', { from: 'updates' }, { from: mark });
        expect(opened).toMatchObject({ chapterId: newChapter, page: 0, chapters: 3 });
        await expectPage(s, 8, 3, 1);
        await readerAfter(s, () => s.driver.press('LEFT'), { shown: 'page', page: 1 });
        await expectPage(s, 8, 3, 2);
        mark = s.events.mark();
        await closeReader(s, newChapter, 1, 'Updates');
        shown = await firstRow(s, 'updates', mark, 'the Updates row at page 2', (row) => row.chapterId === newChapter && row.page === 1);
        let history = await showTab(s, 'History');
        expect(rowsOf(history)[0]).toMatchObject({ mangaId: EPSILON_MANGA_ID, chapterId: newChapter, page: 1, read: false });

        // 4. Alpha in the reader: chapter 1 to page 3, chapter 2 to page 2. History: one Alpha row, chapter 2.
        await s.driver.touch(tab('Library').x, tab('Library').y);
        await openManga(s, LIBRARY_MORE.reading, 0, 3);
        await openChapter(s, 2, { shown: 'page', chapterId: alphaChapters[0].id, page: 0 });
        for (let page = 1; page <= 2; page++) {
            await readerAfter(s, () => s.driver.press('LEFT'), { shown: 'page', page });
        }
        await closeReader(s, alphaChapters[0].id, 2, 'Manga');
        await openChapter(s, 1, { shown: 'page', chapterId: alphaChapters[1].id, page: 0 });
        await readerAfter(s, () => s.driver.press('LEFT'), { shown: 'page', page: 1 });
        await closeReader(s, alphaChapters[1].id, 1, 'Manga');
        mark = s.events.mark();
        await s.driver.press('B');
        await s.events.waitForEvent('screen', { name: 'Library' }, { from: mark });
        await s.events.waitForEvent('idle', {}, { from: mark });
        // The tab shows its last rows at once, then the reloaded ones (the details screen closed since: D52).
        mark = s.events.mark();
        await s.driver.touch(tab('History').x, tab('History').y);
        history = await firstRow(s, 'history', mark, 'Alpha first in History', (row) => row.mangaId === alpha);
        await s.events.waitForEvent('idle', {}, { from: s.events.events.indexOf(history) });
        const historyRows = rowsOf(history);
        expect(historyRows[0]).toMatchObject({ mangaId: alpha, chapterId: alphaChapters[1].id, page: 1, read: false, day: 'Today' });
        expect(historyRows[1]).toMatchObject({ mangaId: EPSILON_MANGA_ID });
        expect(historyRows.filter((r) => r.mangaId === alpha), 'one row per manga').toHaveLength(1);
        expect(history.rows).toBe(2);
        await s.expectGolden('history', { masks: [CLOCK_MASK] });

        // A tap on the Alpha row continues chapter 2 at page 2.
        mark = s.events.mark();
        reader = await readerAfter(s, () => s.driver.touch(UPDATES.row(0, false).x, UPDATES.row(0, false).y),
            { shown: 'page', chapterId: alphaChapters[1].id });
        expect(reader.page).toBe(1);
        expect(await s.events.waitForEvent('chapter_open', { from: 'history' }, { from: mark })).toMatchObject({
            chapterId: alphaChapters[1].id, page: 1, chapters: 3 });
        await expectPage(s, 1, 2, 2);
        mark = s.events.mark();
        await s.driver.press('B'); // nothing moved: nothing to save
        await s.events.waitForEvent('screen', { name: 'History' }, { from: mark });
        await s.events.waitForEvent('idle', {}, { from: mark });

        // 5. Updates, X on the row: read on the server, X again: unread. The cover opens the details screen.
        await showTab(s, 'Updates');
        await focusRows(s, 'updates');
        mark = s.events.mark();
        await s.driver.press('X');
        await firstRow(s, 'updates', mark, 'Epsilon chapter 3 read', (row) => row.chapterId === newChapter && row.read);
        expect((await chaptersOf(client, EPSILON_MANGA_ID)).find((c) => c.id === newChapter)).toMatchObject({ isRead: true });
        mark = s.events.mark();
        await s.driver.press('X');
        await firstRow(s, 'updates', mark, 'Epsilon chapter 3 unread', (row) => row.chapterId === newChapter && !row.read);
        expect((await chaptersOf(client, EPSILON_MANGA_ID)).find((c) => c.id === newChapter)).toMatchObject({
            isRead: false, lastPageRead: 0 });
        mark = s.events.mark();
        await s.driver.touch(UPDATES.cover(0).x, UPDATES.cover(0).y);
        await s.events.waitForEvent('manga', { state: 'ready', complete: true, id: EPSILON_MANGA_ID }, { from: mark, timeoutMs: 30_000 });
        await s.events.waitForEvent('idle', {}, { from: mark });
        mark = s.events.mark();
        await s.driver.press('B');
        await s.events.waitForEvent('screen', { name: 'Updates' }, { from: mark });
        await s.events.waitForEvent('idle', {}, { from: mark });

        // 6. Delta Big read on "another device": chapter 150 at page 2 (its pages fetched first, or the server keeps 0).
        const ch150 = deltaChapters[149].id;
        await client.query('mutation P($id: Int!) { fetchChapterPages(input: { chapterId: $id }) { pages } }', { id: ch150 });
        await client.query('mutation L($id: Int!) { updateChapter(input: { id: $id, patch: { lastPageRead: 1 } }) { chapter { id } } }',
            { id: ch150 });
        await showTab(s, 'History');
        await focusRows(s, 'history');
        mark = s.events.mark();
        await s.driver.press('X'); // refresh
        history = await firstRow(s, 'history', mark, 'Delta Big first in History', (row) => row.mangaId === delta);
        expect(rowsOf(history)[0]).toMatchObject({ chapterId: ch150, page: 1 });
        await s.events.waitForEvent('idle', {}, { from: s.events.events.indexOf(history) });
        mark = s.events.mark();
        reader = await readerAfter(s, () => s.driver.touch(UPDATES.row(0, false).x, UPDATES.row(0, false).y),
            { shown: 'page', chapterId: ch150 });
        expect(reader.page).toBe(1);
        const deltaOpen = await s.events.waitForEvent('chapter_open', { from: 'history' }, { from: mark });
        expect(deltaOpen).toMatchObject({ chapterId: ch150, page: 1, chapters: 300 });
        console.log(`[updates-history] Delta Big (300 chapters) opened from History in ${deltaOpen.ms} ms`);
        await expectPage(s, 4, 150, 2);
        mark = s.events.mark();
        await s.driver.press('B');
        await s.events.waitForEvent('screen', { name: 'History' }, { from: mark });
        await s.events.waitForEvent('idle', {}, { from: mark });

        s.expectNoErrorEvents();
        await s.driver.quit();
    } finally {
        await resetChapters(client, alpha);
        await resetChapters(client, delta);
        await removeNewChapter(client);
    }
}, { timeoutMs: 240_000 });
