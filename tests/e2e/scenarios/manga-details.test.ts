// manga-details (WP-10, TESTING.md): the details screen and its chapter list on the test server (seed.ts: Alpha in
// Reading, Delta Big with 300 chapters in Long series). Checks, on desktop and on the Switch:
// - Alpha: details, the long description opens and closes by touch (goldens "alpha", "alpha-open");
// - a tap on a chapter opens the reader (a stand-in until WP-11) at the right chapter and page; B comes back to it;
// - X marks the focused chapter read, X again unread; a row's "..." menu marks read / unread and bookmarks; Show
//   bookmarked; every change is checked on the server with GraphQL from the harness;
// - Delta Big: all 300 chapters load (pages of 100); Sort oldest first; "Mark all before as read" on chapter 4 marks
//   exactly chapters 1-3 on the server; Show unread (golden "delta"); Continue then starts chapter 4; progress saved on
//   the server meanwhile shows after Refresh, and Continue opens that page.
// The harness resets the chapters it touches (read, bookmark, progress) before and after. Needs the test server.

import { expect } from 'vitest';

import { scenario, type Scenario } from '../lib/scenario.ts';
import { CLOCK_MASK, LIBRARY, LIBRARY_MORE, MANGA } from '../lib/screens.ts';
import { appServerUrl, optionsFromEnv, SuwayomiClient } from '../lib/suwayomi.ts';

interface Row {
    id: number;
    sourceOrder: number;
    isRead: boolean;
    isBookmarked: boolean;
    lastPageRead: number;
}

/** A manga's chapters on the server, oldest first. */
async function chaptersOf(client: SuwayomiClient, mangaId: number): Promise<Row[]> {
    const data = await client.query<{ chapters: { nodes: Row[] } }>(
        `query C($m: Int!) { chapters(condition: { mangaId: $m }, order: [{ by: SOURCE_ORDER }]) {
            nodes { id sourceOrder isRead isBookmarked lastPageRead } } }`, { m: mangaId });
    return data.chapters.nodes;
}

/** Every chapter unread, without bookmark, at page 0. */
async function resetChapters(client: SuwayomiClient, mangaId: number): Promise<Row[]> {
    const ids = (await chaptersOf(client, mangaId)).map((c) => c.id);
    await client.query(`mutation U($ids: [Int!]!) { updateChapters(input: { ids: $ids,
        patch: { isRead: false, isBookmarked: false, lastPageRead: 0 } }) { chapters { id } } }`, { ids });
    return chaptersOf(client, mangaId);
}

async function mangaIds(client: SuwayomiClient): Promise<Map<string, number>> {
    const data = await client.query<{ mangas: { nodes: { id: number; title: string }[] } }>(
        '{ mangas(condition: { inLibrary: true }) { nodes { id title } } }');
    return new Map(data.mangas.nodes.map((m) => [m.title, m.id]));
}

/** Taps, then waits for the chapter action's event and for the app to settle. */
async function tapAction(s: Scenario, tap: { x: number; y: number }, action: string, chapter: number): Promise<void> {
    const mark = s.events.mark();
    await s.driver.touch(tap.x, tap.y);
    const done = await s.events.waitForEvent('chapter_action', { action, chapter }, { from: mark });
    expect(done.ok, `${action} on chapter ${chapter}`).toBe(true);
    await s.events.waitForEvent('idle', {}, { from: mark });
}

/** Opens a row's "..." menu and taps entry `entry` of its four. */
async function rowAction(s: Scenario, row: number, entry: number, action: string, chapter: number): Promise<void> {
    const mark = s.events.mark();
    const menu = MANGA.rowMenu(row);
    await s.driver.touch(menu.x, menu.y);
    await s.events.waitForEvent('idle', {}, { from: mark });
    await tapAction(s, MANGA.menuEntry(entry, 4), action, chapter);
}

/** Opens a bar menu (Sort or Show), taps an entry, and waits for the "manga" event that shows the result. */
async function barChoice(s: Scenario, button: { x: number; y: number }, entry: number, count: number,
    expected: Record<string, unknown>): Promise<void> {
    let mark = s.events.mark();
    await s.driver.touch(button.x, button.y);
    await s.events.waitForEvent('idle', {}, { from: mark });
    mark = s.events.mark();
    const tap = MANGA.menuEntry(entry, count);
    await s.driver.touch(tap.x, tap.y);
    await s.events.waitForEvent('manga', expected, { from: mark });
    await s.events.waitForEvent('idle', {}, { from: mark });
}

scenario('manga-details', async (s) => {
    const url = appServerUrl(s.driver.name);
    const client = new SuwayomiClient(optionsFromEnv());
    const ids = await mangaIds(client);
    const alpha = ids.get('Alpha') ?? 0;
    const delta = ids.get('Delta Big') ?? 0;
    expect(alpha && delta, 'the test server has Alpha and Delta Big: tools/test-server/test-server.sh reset').toBeTruthy();
    const alphaChapters = await resetChapters(client, alpha);
    const deltaChapters = await resetChapters(client, delta);
    expect(deltaChapters).toHaveLength(300);
    const alphaId = (n: number) => alphaChapters[n - 1].id;
    const deltaId = (n: number) => deltaChapters[n - 1].id;

    await s.launch({ testConfig: { server: { url } } });
    const library = await s.events.waitForEvent('library', { state: 'ready' }, { timeoutMs: 30_000 });
    await s.events.waitForEvent('idle', {}, { from: s.events.events.indexOf(library) });

    // Alpha (Reading, the first cover).
    let mark = s.events.mark();
    await s.driver.touch(LIBRARY_MORE.reading.x, LIBRARY_MORE.reading.y);
    await s.events.waitForEvent('library', { state: 'ready', items: 2 }, { from: mark });
    await s.events.waitForEvent('idle', {}, { from: mark });
    mark = s.events.mark();
    await s.driver.touch(LIBRARY.cover(0).x, LIBRARY.cover(0).y);
    const opened = await s.events.waitForEvent('screen', { name: 'Manga' }, { from: mark });
    expect(opened.params).toEqual({ id: alpha, title: 'Alpha' });
    const shown = await s.events.waitForEvent('manga', { state: 'ready', complete: true }, { from: mark });
    expect(shown).toMatchObject({ id: alpha, items: 3, total: 3, first: 3, unread: 3, sort: 'newest', description: 'closed' });
    await s.events.waitForEvent('idle', {}, { from: mark });
    await s.expectGolden('alpha', { masks: [CLOCK_MASK] });

    // The description opens and closes.
    mark = s.events.mark();
    await s.driver.touch(MANGA.description.x, MANGA.description.y);
    await s.events.waitForEvent('manga', { description: 'open' }, { from: mark });
    await s.events.waitForEvent('idle', {}, { from: mark });
    await s.expectGolden('alpha-open', { masks: [CLOCK_MASK] });
    mark = s.events.mark();
    await s.driver.touch(MANGA.description.x, MANGA.description.y);
    await s.events.waitForEvent('manga', { description: 'closed' }, { from: mark });
    await s.events.waitForEvent('idle', {}, { from: mark });

    // A tap on chapter 2 (row 1, newest first) opens the reader there; B comes back with the focus on it.
    mark = s.events.mark();
    await s.driver.touch(MANGA.row(1).x, MANGA.row(1).y);
    const reader = await s.events.waitForEvent('screen', { name: 'Reader' }, { from: mark });
    expect(reader.params).toEqual({ mangaId: alpha, chapterId: alphaId(2), page: 0 });
    await s.events.waitForEvent('idle', {}, { from: mark });
    mark = s.events.mark();
    await s.driver.press('B');
    await s.events.waitForEvent('screen', { name: 'Manga' }, { from: mark });
    await s.events.waitForEvent('idle', {}, { from: mark });

    // X toggles chapter 2 (it has the focus): read, then unread again; the server agrees each time.
    for (const [action, read] of [['mark_read', true], ['mark_unread', false]] as const) {
        mark = s.events.mark();
        await s.driver.press('X');
        const done = await s.events.waitForEvent('chapter_action', { action, chapter: alphaId(2) }, { from: mark });
        expect(done.ok).toBe(true);
        await s.events.waitForEvent('idle', {}, { from: mark });
        expect((await chaptersOf(client, alpha))[1].isRead, `${action}: chapter 2 on the server`).toBe(read);
    }

    // The "..." menu on chapter 3 (row 0): read, unread, bookmark; then Show bookmarked and back to all.
    await rowAction(s, 0, 0, 'mark_read', alphaId(3));
    expect((await chaptersOf(client, alpha))[2].isRead).toBe(true);
    await rowAction(s, 0, 0, 'mark_unread', alphaId(3));
    expect((await chaptersOf(client, alpha))[2].isRead).toBe(false);
    await rowAction(s, 0, 1, 'bookmark', alphaId(3));
    expect((await chaptersOf(client, alpha))[2].isBookmarked).toBe(true);
    await barChoice(s, MANGA.filter, 2, 3, { filter: 'bookmarked', items: 1, first: 3 });
    await barChoice(s, MANGA.filter, 0, 3, { filter: 'all', items: 3 });

    if (s.driver.name === 'switch') await buttons(s);

    // Back to the library, then Delta Big (Long series, the first cover: "Delta Big" < "Gamma Webtoon").
    mark = s.events.mark();
    await s.driver.press('B');
    await s.events.waitForEvent('screen', { name: 'Library' }, { from: mark });
    await s.events.waitForEvent('idle', {}, { from: mark });
    mark = s.events.mark();
    await s.driver.touch(LIBRARY_MORE.longSeries.x, LIBRARY_MORE.longSeries.y);
    await s.events.waitForEvent('library', { state: 'ready', items: 2 }, { from: mark });
    await s.events.waitForEvent('idle', {}, { from: mark });
    mark = s.events.mark();
    await s.driver.touch(LIBRARY.cover(0).x, LIBRARY.cover(0).y);
    const big = await s.events.waitForEvent('manga', { state: 'ready', complete: true }, { from: mark, timeoutMs: 30_000 });
    expect(big).toMatchObject({ id: delta, items: 300, loaded: 300, total: 300, first: 300, unread: 300 });
    await s.events.waitForEvent('idle', {}, { from: mark });

    // Oldest first, then "Mark all before as read" on chapter 4 (row 3): exactly chapters 1-3 on the server.
    await barChoice(s, MANGA.sort, 1, 2, { sort: 'oldest', first: 1 });
    await rowAction(s, 3, 2, 'mark_previous_read', deltaId(4));
    const afterMark = await chaptersOf(client, delta);
    expect(afterMark.filter((c) => c.isRead).map((c) => c.sourceOrder)).toEqual([1, 2, 3]);
    const marked = s.events.events.filter((e) => e.ev === 'manga').at(-1);
    expect(marked).toMatchObject({ unread: 297, continue: { kind: 'continue', chapter: deltaId(4), page: 0 } });

    // Show unread: chapter 4 comes first.
    await barChoice(s, MANGA.filter, 1, 3, { filter: 'unread', items: 297, first: 4 });
    await s.expectGolden('delta', { masks: [CLOCK_MASK] });

    // Progress saved elsewhere (page index 1 of chapter 4) shows after Refresh; Continue opens that page.
    await client.query('mutation P($id: Int!) { fetchChapterPages(input: { chapterId: $id }) { pages } }', { id: deltaId(4) });
    await client.query('mutation Q($id: Int!) { updateChapter(input: { id: $id, patch: { lastPageRead: 1 } }) { chapter { id } } }',
        { id: deltaId(4) });
    mark = s.events.mark();
    await s.driver.touch(MANGA.refresh.x, MANGA.refresh.y);
    const refreshed = await s.events.waitForEvent('chapter_action', { action: 'refresh' }, { from: mark, timeoutMs: 30_000 });
    expect(refreshed.ok).toBe(true);
    const after = await s.events.waitForEvent('manga', { state: 'ready', complete: true }, { from: mark });
    expect(after).toMatchObject({ items: 297, continue: { kind: 'continue', chapter: deltaId(4), page: 1 } });
    await s.events.waitForEvent('idle', {}, { from: mark });
    mark = s.events.mark();
    await s.driver.touch(MANGA.continue.x, MANGA.continue.y);
    const resumed = await s.events.waitForEvent('screen', { name: 'Reader' }, { from: mark });
    expect(resumed.params).toEqual({ mangaId: delta, chapterId: deltaId(4), page: 1 });
    await s.events.waitForEvent('idle', {}, { from: mark });
    mark = s.events.mark();
    await s.driver.press('B');
    await s.events.waitForEvent('screen', { name: 'Manga' }, { from: mark });
    await s.events.waitForEvent('idle', {}, { from: mark });

    s.expectNoErrorEvents();
    await s.driver.quit();
    await resetChapters(client, alpha);
    await resetChapters(client, delta);
});

/**
 * The buttons desktop borealis can't press (its keyboard map has only A, B, X and the D-pad), so the Switch only: Y opens
 * the focused chapter's menu, MINUS the screen menu; B closes each and the focus is back on the chapter.
 */
async function buttons(s: Scenario): Promise<void> {
    // The focus is on Show (tapped last). A tap on a chapter opens it and gives it the focus; B comes back to it.
    let mark = s.events.mark();
    await s.driver.touch(MANGA.row(0).x, MANGA.row(0).y);
    await s.events.waitForEvent('screen', { name: 'Reader' }, { from: mark });
    await s.events.waitForEvent('idle', {}, { from: mark });
    mark = s.events.mark();
    await s.driver.press('B');
    await s.events.waitFor('B: a chapter has the focus', (e) => e.ev === 'focus' && String(e.view).includes('ChapterCell'), { from: mark });
    await s.events.waitForEvent('idle', {}, { from: mark });
    for (const button of ['Y', 'MINUS'] as const) {
        mark = s.events.mark();
        await s.driver.press(button);
        await s.events.waitFor(`${button}: a menu has the focus`, (e) => e.ev === 'focus' && !String(e.view).includes('ChapterCell'), { from: mark });
        await s.events.waitForEvent('idle', {}, { from: mark });
        mark = s.events.mark();
        await s.driver.press('B');
        await s.events.waitFor(`${button}: B gives the focus back to a chapter`, (e) => e.ev === 'focus' && String(e.view).includes('ChapterCell'), { from: mark });
        await s.events.waitForEvent('idle', {}, { from: mark });
    }
}
