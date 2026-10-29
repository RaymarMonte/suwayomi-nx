// library-browse (WP-09, TESTING.md): the Library tab on the test server's library (seed.ts: Default with 2 manga, Reading,
// Long series, the 150-manga Big shelf, Empty). Checks, on desktop and on the Switch:
// - the first screen shows Default's covers (golden "default"); a tap on a category tab switches to it (golden "grid");
// - a tap on a cover opens the manga's (stand-in) screen and B comes back;
// - the D-pad walks the grid down to the last row while the pages load (60 -> 120 -> 150) and back up; two more passes by
//   flicking (touch); the memory in use stays flat over the three passes (mem events), no cover is downloaded twice, and no
//   cover loads for an item that never had a cell on screen or in the row below it (image vs grid events);
// - X refreshes; Sort (tap) -> Unread loads and sorts the whole category; the Empty category shows its message (golden
//   "empty"); after a restart the library opens at the same category and sort (config.json).
// Needs the test server (tools/test-server/test-server.sh start).

import { expect } from 'vitest';

import type { AppEvent } from '../lib/events.ts';
import { scenario, type Scenario } from '../lib/scenario.ts';
import { CLOCK_MASK, LIBRARY } from '../lib/screens.ts';
import { appServerUrl, optionsFromEnv, SuwayomiClient } from '../lib/suwayomi.ts';

const MiB = 1024 * 1024;
const ROWS = 25; // 150 manga in 6 columns

interface ShelfInfo {
    shelf: number; // category ids
    empty: number;
    order: number[]; // Big shelf manga ids in the app's order (title, then id)
}

async function readServer(): Promise<ShelfInfo> {
    const client = new SuwayomiClient(optionsFromEnv());
    const cats = await client.query<{ categories: { nodes: { id: number; name: string }[] } }>('{ categories { nodes { id name } } }');
    const id = (name: string) => {
        const found = cats.categories.nodes.find((c) => c.name === name);
        if (!found) throw new Error(`the test server has no "${name}" category: run tools/test-server/test-server.sh reset`);
        return found.id;
    };
    const shelf = id('Big shelf');
    const list = await client.query<{ mangas: { nodes: { id: number }[] } }>(
        `query S($c: Int!) { mangas(condition: { inLibrary: true }, filter: { categoryId: { equalTo: $c } },
            order: [{ by: TITLE }, { by: ID }]) { nodes { id } } }`, { c: shelf });
    return { shelf, empty: id('Empty'), order: list.mangas.nodes.map((n) => n.id) };
}

/** Waits for the next "mem" event (one every 10 s) and returns its "used" (the allocator's bytes in use). */
async function memNow(s: Scenario): Promise<number> {
    const mem = await s.events.waitForEvent('mem', {}, { from: s.events.mark(), timeoutMs: 15_000 });
    return Number(mem.used);
}

async function press(s: Scenario, button: 'UP' | 'DOWN', times: number): Promise<void> {
    for (let i = 0; i < times; i++) await s.driver.press(button);
}

/** Flicks over the grid (up to scroll down, or the other way) until the latest "grid" event says `done`. */
async function flickUntil(s: Scenario, direction: 'down' | 'up', done: (grid: AppEvent) => boolean, what: string): Promise<void> {
    const [from, to] = direction === 'down' ? [LIBRARY.flickFrom, LIBRARY.flickTo] : [LIBRARY.flickTo, LIBRARY.flickFrom];
    for (let i = 0; i < LIBRARY.maxFlicks; i++) {
        const grid = s.events.events.filter((e) => e.ev === 'grid').at(-1);
        if (grid && done(grid)) return;
        const mark = s.events.mark();
        await s.driver.swipe(from.x, from.y, to.x, to.y, 120);
        await s.events.waitForEvent('idle', {}, { from: mark }); // the flick's glide has ended
    }
    throw new Error(`${LIBRARY.maxFlicks} flicks ${direction} didn't reach ${what}`);
}

scenario('library-browse', async (s) => {
    const url = appServerUrl(s.driver.name);
    const info = await readServer();
    expect(info.order).toHaveLength(150);

    // The first screen: Default, sorted by title.
    await s.launch({ testConfig: { server: { url } } });
    await s.events.waitForEvent('screen', { name: 'Library' });
    const first = await s.events.waitForEvent('library', { state: 'ready' }, { timeoutMs: 30_000 });
    expect(first).toMatchObject({ category: 0, categories: 5, sort: 'title', items: 2, total: 2 });
    await s.events.waitForEvent('idle', {}, { from: s.events.events.indexOf(first) });
    await s.expectGolden('default', { masks: [CLOCK_MASK] });

    // Touch: the Big shelf tab. Its first page (60) shows.
    let mark = s.events.mark();
    await s.driver.touch(LIBRARY.categories.bigShelf.x, LIBRARY.categories.bigShelf.y);
    const big = await s.events.waitForEvent('library', { state: 'ready', category: info.shelf }, { from: mark });
    expect(big).toMatchObject({ items: 60, total: 150 });
    await s.events.waitForEvent('idle', {}, { from: mark });
    await s.expectGolden('grid', { masks: [CLOCK_MASK] });

    // Touch: a cover opens the manga; B comes back to the same place.
    mark = s.events.mark();
    const second = LIBRARY.cover(1);
    await s.driver.touch(second.x, second.y);
    const manga = await s.events.waitForEvent('screen', { name: 'Manga' }, { from: mark });
    expect(manga.params).toMatchObject({ id: info.order[1], title: 'Shelf 002' });
    await s.events.waitForEvent('idle', {}, { from: mark });
    mark = s.events.mark();
    await s.driver.press('B');
    await s.events.waitForEvent('screen', { name: 'Library' }, { from: mark });
    await s.events.waitForEvent('idle', {}, { from: mark });

    if (s.driver.name === 'switch') await buttons(s, info);

    // Pass 1, D-pad: down to the last row (the pages load on the way), then back to the top.
    mark = s.events.mark();
    await press(s, 'DOWN', ROWS + 1); // the first press may land on the cover row the focus came back to
    const all = await s.events.waitForEvent('library', { state: 'ready', category: info.shelf, items: 150 }, { from: mark });
    expect(all.total).toBe(150);
    await s.events.waitForEvent('idle', {}, { from: mark });
    await press(s, 'UP', ROWS);
    await s.events.waitForEvent('idle', {}, { from: mark });
    const afterOne = await memNow(s);

    // Passes 2 and 3, touch: flick down to the end and back up.
    for (let pass = 2; pass <= 3; pass++) {
        await flickUntil(s, 'down', (g) => Number(g.last) === 150, `pass ${pass}: the end`);
        await flickUntil(s, 'up', (g) => Number(g.first) === 0, `pass ${pass}: the top`);
    }
    const afterThree = await memNow(s);
    expect(afterThree - afterOne, `heap after pass 3 vs pass 1 (${afterOne} -> ${afterThree})`).toBeLessThan(16 * MiB);
    checkCoverLoads(s.events.events, info);

    // X refreshes (title order again starts with the first page).
    mark = s.events.mark();
    await s.driver.press('X');
    const refreshed = await s.events.waitForEvent('library', { state: 'ready', category: info.shelf }, { from: mark });
    expect(refreshed.items).toBe(60);
    await s.events.waitForEvent('idle', {}, { from: mark });

    // Touch: Sort -> Unread loads the whole category, then sorts it.
    mark = s.events.mark();
    await s.driver.touch(LIBRARY.sort.x, LIBRARY.sort.y);
    await s.events.waitForEvent('idle', {}, { from: mark });
    mark = s.events.mark();
    await s.driver.touch(LIBRARY.sortMenu.unread.x, LIBRARY.sortMenu.unread.y);
    const unread = await s.events.waitForEvent('library', { state: 'ready', sort: 'unread' }, { from: mark });
    expect(unread).toMatchObject({ category: info.shelf, items: 150, total: 150 });
    await s.events.waitForEvent('idle', {}, { from: mark });

    // Touch: the Empty category.
    mark = s.events.mark();
    await s.driver.touch(LIBRARY.categories.empty.x, LIBRARY.categories.empty.y);
    await s.events.waitForEvent('library', { state: 'empty', category: info.empty }, { from: mark });
    await s.events.waitForEvent('idle', {}, { from: mark });
    await s.expectGolden('empty', { masks: [CLOCK_MASK] });

    s.expectNoErrorEvents();
    await s.driver.quit();

    // A restart opens the same category with the same sort (config.json "library").
    await s.launch({ testConfig: { server: { url } }, keepConfig: true });
    const again = await s.events.waitForEvent('library', { state: 'empty' }, { timeoutMs: 30_000 });
    expect(again).toMatchObject({ category: info.empty, sort: 'unread' });
    s.expectNoErrorEvents();
    await s.driver.quit();
});

/**
 * The buttons desktop borealis can't press (its keyboard map has only A, B, X and the D-pad), so the Switch only: R and L
 * step through the categories (Big shelf -> Empty -> Big shelf), Y opens the sort menu and MINUS the library menu (the focus
 * leaves the covers), B closes each and the focus is back on a cover. Starts and ends on the Big shelf, focus on a cover.
 */
async function buttons(s: Scenario, info: ShelfInfo): Promise<void> {
    let mark = s.events.mark();
    await s.driver.press('R');
    await s.events.waitForEvent('library', { state: 'empty', category: info.empty }, { from: mark });
    await s.events.waitForEvent('idle', {}, { from: mark });
    mark = s.events.mark();
    await s.driver.press('L');
    await s.events.waitForEvent('library', { state: 'ready', category: info.shelf }, { from: mark });
    await s.events.waitForEvent('idle', {}, { from: mark });
    for (const button of ['Y', 'MINUS'] as const) {
        mark = s.events.mark();
        await s.driver.press(button);
        await s.events.waitFor(`${button}: a menu has the focus`, (e) => e.ev === 'focus' && !String(e.view).includes('CoverCell'), { from: mark });
        await s.events.waitForEvent('idle', {}, { from: mark });
        mark = s.events.mark();
        await s.driver.press('B');
        await s.events.waitFor(`${button}: B gives the focus back to a cover`, (e) => e.ev === 'focus' && String(e.view).includes('CoverCell'), { from: mark });
        await s.events.waitForEvent('idle', {}, { from: mark });
    }
}

/**
 * Every Big shelf cover load was for an item that had a cell (a "grid" event's [first, last)) or sat in the row below it
 * (the prefetch row) at some point before the load finished; and no cover came over the network twice.
 */
function checkCoverLoads(events: readonly AppEvent[], info: ShelfInfo): void {
    const index = new Map(info.order.map((id, i) => [id, i]));
    const fromNet = new Map<string, number>();
    const ranges: { first: number; last: number; columns: number }[] = [];
    let checked = 0;
    for (const e of events) {
        if (e.ev === 'grid') ranges.push({ first: Number(e.first), last: Number(e.last), columns: Number(e.columns) });
        if (e.ev !== 'image') continue;
        const m = /\/api\/v1\/manga\/(\d+)\/thumbnail/.exec(String(e.url));
        const i = m ? index.get(Number(m[1])) : undefined;
        if (i === undefined) continue; // not a Big shelf cover
        checked++;
        const wanted = ranges.some((r) => i >= r.first && i < r.last + r.columns);
        expect(wanted, `cover of item ${i} (${String(e.url)}) loaded but never on screen or in the row below`).toBe(true);
        if (e.source === 'net') {
            fromNet.set(String(e.url), (fromNet.get(String(e.url)) ?? 0) + 1);
        }
    }
    expect(checked, 'Big shelf cover loads seen').toBeGreaterThanOrEqual(150);
    const twice = [...fromNet].filter(([, n]) => n > 1).map(([u]) => u);
    expect(twice, 'covers downloaded more than once').toEqual([]);
}
