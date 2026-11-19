// browse (WP-14, D56, TESTING.md): the Browse tab and a source's screen on the test server's test source (WP-23, D55).
// Checks, on desktop and on the Switch:
// - the Browse tab lists Local, SNX Test (EN) and SNX Test (JA) with their language labels (golden "sources");
// - SNX Test (EN) → Popular: 25 manga, then 50 and 60 as the grid scrolls down, then no more (golden "source-popular");
// - Latest starts with Paper Garden (en-14, the newest); Popular again;
// - Search "comet" (the text command): the 3 comets (golden "source-search"); B clears it (Popular again);
// - Filters (golden "filters", Genres open, Action include): Action back to any (no Completed Webtoon has Action), Status =
//   Completed, Type → Webtoon, Sort by Title ascending → Apply: the 4 manga selectManga() predicts, in that order; Reset →
//   Apply: Popular again;
// - the switch file fails en "search": the error "SNX test failure: search"; Refresh (Retry) after clearing it works;
// - Comet Courier's preview (loaded from the source) → Add to library → the picker in choose mode (golden "add-picker") →
//   Reading → Add: in the library in exactly Reading; its cover gets the "In library" tag (golden "source-tagged");
// - Paper Garden → Add with nothing ticked: Default. SNX Mature installed: its row has "18+" (golden "sources-mature"); its
//   Paper Garden → Add → the same-title box (golden "same-title") → Show entry opens SNX Test's; Add anyway: in the library twice;
// - WP-14b: Languages (golden "languages"): Japanese hidden → its row goes; the last shown language can't be unticked;
//   Japanese shown again;
// - WP-14b: Search all "comet" with every test-source call slowed by 2 s: Local, SNX Mature (EN), SNX Test (EN), SNX Test
//   (JA) (the list's order, Last used not twice), 3 searching and 1 waiting at first; done (golden "search-all"): only SNX
//   Test (EN) found, its 3 comets; a move into an empty place lands on the next row's card; A on SNX Test (EN)'s card →
//   its search for "comet". Then over the 3 sources (SNX Mature uninstalled), unslowed: done ≤ 3 s;
// - the Library's Reading tab has Comet Courier; Remove from its preview's menu → the confirm (golden "remove-confirm") → out;
// - WP-14b: Tick for new manga on the Categories screen (golden "categories-ticked"): Long series gets the server's
//   "default" flag, and a preview's Add then opens the picker with Long series ticked;
// - Switch only: the Y menus, Don't ask again (the next add goes straight into Long series), Settings, L / R;
// - 10 rounds of opening SNX Test (EN) and scrolling to the end: the memory in use grows by less than 16 MiB;
// - the D56 times, on the app's clock (D54's way): the sources ≤ 1 s, a first page ≤ 2 s, the next pages ≤ 2 s, the filters
//   ≤ 0.5 s, an add and a remove ≤ 1 s, Search all over 3 sources ≤ 3 s.
// Known state first and at the end, also after a failure: the test source as seeded, no test-source manga in the library,
// the seed's categories (none ticked for new manga).
// Needs the test server (tools/test-server/test-server.sh start).

import { expect } from 'vitest';

import { mangaCategoryIds, restoreSeed, serverCategories } from '../lib/categories.ts';
import type { AppEvent } from '../lib/events.ts';
import { scenario, type Scenario } from '../lib/scenario.ts';
import { CATEGORIES, CLOCK_MASK, LANGUAGES, LIBRARY, MANGA, PICKER, tab } from '../lib/screens.ts';
import { appServerUrl, optionsFromEnv, SuwayomiClient } from '../lib/suwayomi.ts';
import { playTouches, tapFrames, typedText } from '../lib/touches.ts';
import {
    clearTestSourceControl, EXTENSIONS, installExtension, removeTestSourceMangaFromLibrary, resetTestSource,
    setTestSourceControl, TEST_SOURCE_IDS, uninstallExtension,
} from '../lib/test-source.ts';
import { CATALOG_EN, selectManga } from '../../../tools/gen-fixtures/test-source.ts';

const MiB = 1024 * 1024;
const ROUNDS = 10;
/** D56's limits (WP-14 Acceptance), all on the app's own clock. */
const LIMITS = { sourcesMs: 1_000, firstPageMs: 2_000, nextPageMs: 2_000, filtersMs: 500, addMs: 1_000, removeMs: 1_000,
    searchAllMs: 3_000, roundsBytes: 16 * MiB };

/** The details screen's footer hint "Menu" (MINUS): a tap opens the menu, the desktop's way to it. */
const MENU_HINT = { x: 945, y: 683 };

/** The Browse tab's rows (BrowseTab: one column of 80 px rows, 4 px apart, under the 72 px bar) and its bar's buttons. */
const BROWSE = {
    row: (i: number) => ({ x: 834, y: 223 + 84 * i }),
    searchAll: { x: 959, y: 123 },
    languages: { x: 1090, y: 123 },
    refresh: { x: 1200, y: 123 },
};
/**
 * The same-title box, a borealis dialog with three buttons: the third (Cancel) gets a row of its own on top, Show entry and
 * Add anyway share the bottom row. Measured with the 3-line Paper Garden text (golden "same-title").
 */
const SAME_TITLE = {
    cancel: { x: 640, y: 441 },
    showEntry: { x: 460, y: 513 },
    addAnyway: { x: 820, y: 513 },
};
/** The picker's "Don't ask again: no / yes" button in choose mode (left of New category; golden "add-picker"). */
const PICKER_DONT_ASK = { x: 855, y: 136 };
/** The remove question (3 lines of text, so lower than WP-24's DIALOG): Cancel on the left, Remove on the right. */
const REMOVE_CONFIRM = {
    cancel: { x: 460, y: 476 },
    remove: { x: 820, y: 476 },
};
/** A source's screen: the bar (Popular | Latest on the left; Search, Filters, Refresh on the right). */
const SOURCE = {
    popular: { x: 72, y: 123 },
    latest: { x: 170, y: 123 },
    search: { x: 1012, y: 123 },
    filters: { x: 1110, y: 123 },
    refresh: { x: 1211, y: 123 },
    /** The middle of cover `index`'s image while the grid is at the top (9 columns, 136 px apart). */
    cover: (index: number) => ({ x: 96 + 136 * (index % 9), y: 274 + 246 * Math.floor(index / 9) }),
};

/** The filters screen's Reset and Apply (top right, above the list). */
const FILTERS = {
    reset: { x: 1064, y: 136 },
    apply: { x: 1160, y: 136 },
};

type SourceRowEvent = { id: string; name: string; lang: string; nsfw: boolean; label: string; lastUsed: boolean };
type LanguageRowEvent = { code: string; name: string; shown: boolean };
type SearchRowEvent = { id: string; name: string; state: string; found: number; more: boolean; shown: number; titles: string[] };

/** Taps (x, y), waits for the first event that `match` accepts and for the screen to settle. */
async function tapUntil(s: Scenario, at: { x: number; y: number }, what: string, match: (e: AppEvent) => boolean): Promise<AppEvent> {
    const mark = s.events.mark();
    await s.driver.touch(at.x, at.y);
    const e = await s.events.waitFor(what, match, { from: mark, timeoutMs: 30_000 });
    await s.events.waitForEvent('idle', {}, { from: mark });
    return e;
}

/**
 * Like tapUntil, but the tap is played through the test build's touch channel and the time is the app's own: from the finger
 * touching (the `command` event's `started`) to the event `match` accepts (D54's way: a driver tap on the Switch adds ~0.9 s).
 */
async function timedTap(s: Scenario, at: { x: number; y: number }, what: string, match: (e: AppEvent) => boolean): Promise<{ e: AppEvent; ms: number }> {
    const mark = s.events.mark();
    const played = await playTouches(s, tapFrames(at));
    const e = await s.events.waitFor(what, match, { from: mark, timeoutMs: 30_000 });
    await s.events.waitForEvent('idle', {}, { from: mark });
    const from = played.started !== undefined ? Number(played.started) : Number(played.t);
    return { e, ms: Math.max(0, Number(e.t) - from) };
}

/** Waits for the next "mem" event (one every 10 s) and returns its "used". */
async function memNow(s: Scenario): Promise<number> {
    const mem = await s.events.waitForEvent('mem', {}, { from: s.events.mark(), timeoutMs: 15_000 });
    return Number(mem.used);
}

/** Presses DOWN until the source's list has no next page; returns the last "browse" event. */
async function scrollToEnd(s: Scenario, from: AppEvent): Promise<AppEvent> {
    let last = from;
    for (let i = 0; i < 20 && last.hasNext !== false; i++) {
        const mark = s.events.mark();
        await s.driver.press('DOWN');
        await s.events.waitForEvent('idle', {}, { from: mark });
        last = s.events.events.filter((e) => e.ev === 'browse').at(-1) ?? last;
    }
    return last;
}

const isEv = (name: string, fields: Record<string, unknown>) => (e: AppEvent) =>
    e.ev === name && Object.entries(fields).every(([k, v]) => e[k] === v);

/** The row index of the filters row with the focus, or -1 (the focus is elsewhere). */
function focusedFilterRow(s: Scenario): number {
    const focus = s.events.events.filter((e) => e.ev === 'focus').at(-1);
    const m = /filters\/row-(\d+)/.exec(String(focus?.view ?? ''));
    return m ? Number(m[1]) : -1;
}

/** Moves the focus to filters row `row` with the D-pad (the controller's way; the list scrolls with the focus). */
async function focusFilterRow(s: Scenario, row: number): Promise<void> {
    for (let i = 0; i < 40; i++) {
        const now = focusedFilterRow(s);
        if (now === row) return;
        const mark = s.events.mark();
        await s.driver.press(now > row ? 'UP' : 'DOWN');
        try {
            await s.events.waitForEvent('focus', {}, { from: mark, timeoutMs: 2_000 });
        } catch {
            // the first press after a touch only shows the highlight: press again
        }
    }
    throw new Error(`the focus didn't reach filters row ${row} (it is on ${focusedFilterRow(s)})`);
}

/** Presses `button` (`times` times), waiting for the screen to settle after each. */
async function pressAndSettle(s: Scenario, button: 'A' | 'B' | 'UP' | 'DOWN', times = 1): Promise<void> {
    for (let i = 0; i < times; i++) {
        const mark = s.events.mark();
        await s.driver.press(button);
        await s.events.waitForEvent('idle', {}, { from: mark });
    }
}

type FilterRowEvent = { kind: string; name: string; value: string; depth: number; open?: boolean };

/** The latest "filters" event's rows. */
function filterRows(s: Scenario): FilterRowEvent[] {
    return (s.events.events.filter((e) => e.ev === 'filters').at(-1)?.rows ?? []) as FilterRowEvent[];
}

/** B, then waits for the `screen` event of the screen it goes back to, and for the screen to settle. */
async function backTo(s: Scenario, screen: string): Promise<void> {
    const mark = s.events.mark();
    await s.driver.press('B');
    await s.events.waitForEvent('screen', { name: screen }, { from: mark });
    await s.events.waitForEvent('idle', {}, { from: mark });
}

/** How many library manga are called `title` (any source). */
async function libraryCount(title: string): Promise<number> {
    const client = new SuwayomiClient(optionsFromEnv());
    const r = await client.query<{ mangas: { totalCount: number } }>(
        'query M($t: String!) { mangas(condition: { inLibrary: true, title: $t }) { totalCount } }', { t: title });
    return r.mangas.totalCount;
}

/** The row of source `id` in the Browse tab's latest "sources" event (the Last used row counts, it comes first). */
function sourceRow(s: Scenario, id: string): number {
    const rows = (s.events.events.filter((e) => e.ev === 'sources').at(-1)?.rows ?? []) as SourceRowEvent[];
    const row = rows.findIndex((r) => r.id === id);
    if (row < 0) throw new Error(`the Browse tab has no source ${id}`);
    return row;
}

/** Whether manga `id` is in the library (the server's answer). */
async function isInLibrary(id: number): Promise<boolean> {
    const client = new SuwayomiClient(optionsFromEnv());
    const r = await client.query<{ manga: { inLibrary: boolean } }>('query M($id: Int!) { manga(id: $id) { inLibrary } }', { id });
    return r.manga.inLibrary;
}

/** The view id in the latest "focus" event ("" when none). */
function focusedView(s: Scenario): string {
    return String(s.events.events.filter((e) => e.ev === 'focus').at(-1)?.view ?? '');
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

/** Presses `button` and waits for the screen to settle (a dropdown opening, a mode change). */
async function pressSettled(s: Scenario, button: 'Y' | 'L' | 'R' | 'MINUS' | 'A'): Promise<number> {
    const mark = s.events.mark();
    await s.driver.press(button);
    await s.events.waitForEvent('idle', {}, { from: mark });
    return mark;
}

/**
 * WP doc step 10, Switch only. On SNX Test (EN): Y on Comet Courier → Add to library... → the picker (Long series ticked:
 * the step before ticked it for new manga) → Don't ask again → Add (Long series); Y on Midnight Knight → Add to library...
 * goes straight into Long series, no picker; R / L switch Latest / Popular. Settings: "Ask for categories when adding" back
 * on. Library: search "Midnight Knight", Y on its cover → Remove from library... → the confirm → out. Ends on the Browse tab.
 */
async function switchButtons(s: Scenario, comet: number, longSeries: number): Promise<void> {
    await tapUntil(s, BROWSE.row(sourceRow(s, TEST_SOURCE_IDS.en)), 'SNX Test (EN)',
        isEv('browse', { source: TEST_SOURCE_IDS.en, mode: 'popular', state: 'ready' }));
    await pressUntilFocus(s, 'RIGHT', 'source/cover-1');
    await pressUntilFocus(s, 'LEFT', 'source/cover-0');  // Comet Courier (removed in step 9)
    await pressSettled(s, 'Y');
    await tapUntil(s, MANGA.menuEntry(1, 2), 'the picker from the cover menu', isEv('category_picker', { state: 'ready', mode: 'choose' }));
    await tapUntil(s, PICKER_DONT_ASK, "Don't ask again: yes", (e) => e.ev === 'category_picker' && e.dontAsk === true);
    const chosen = await tapUntil(s, PICKER.save, 'the add', isEv('library_add', { ok: true }));
    expect(chosen).toMatchObject({ manga: comet, categories: [longSeries], asked: true });

    // The next add asks nothing: straight into the category ticked for new manga.
    await pressUntilFocus(s, 'RIGHT', 'source/cover-2');  // Midnight Knight
    await pressSettled(s, 'Y');
    const direct = await tapUntil(s, MANGA.menuEntry(1, 2), 'the add without a question', isEv('library_add', { ok: true }));
    expect(direct).toMatchObject({ categories: [longSeries], asked: false });
    expect(s.events.events.slice(s.events.events.indexOf(chosen)).some((e) => e.ev === 'screen' && e.name === 'CategoryPicker'))
        .toBe(false);
    const midnight = Number(direct.manga);
    expect(await isInLibrary(midnight)).toBe(true);
    expect(await mangaCategoryIds(midnight)).toEqual([longSeries]);

    // R: Latest, L: Popular.
    let mark = await pressSettled(s, 'R');
    await s.events.waitForEvent('browse', { mode: 'latest', state: 'ready' }, { from: mark, timeoutMs: 30_000 });
    mark = await pressSettled(s, 'L');
    await s.events.waitForEvent('browse', { mode: 'popular', state: 'ready' }, { from: mark, timeoutMs: 30_000 });
    await backTo(s, 'Browse');

    // Settings: "Ask for categories when adding" is off (Don't ask again); A turns it back on.
    await tapUntil(s, tab('Settings'), 'the Settings tab', isEv('screen', { name: 'Settings' }));
    await pressUntilFocus(s, 'RIGHT', 'settings/', 3);
    await pressUntilFocus(s, 'DOWN', 'settings/ask_categories', 12);
    mark = await pressSettled(s, 'A');
    await s.events.waitForEvent('setting', { name: 'askCategories', value: true }, { from: mark });

    // Library: search "Midnight Knight", Y on its cover → Remove from library... → Remove.
    await tapUntil(s, tab('Library'), 'the Library tab', isEv('screen', { name: 'Library' }));
    await pressUntilFocus(s, 'RIGHT', 'library/', 3);
    await pressSettled(s, 'MINUS');
    await typedText(s, 'Midnight Knight');
    await tapUntil(s, MANGA.menuEntry(2, 5), 'MINUS → Search...',
        (e) => e.ev === 'library' && e.search === 'Midnight Knight' && e.state === 'ready');
    await pressUntilFocus(s, 'DOWN', 'library/cover-0', 4);
    await pressSettled(s, 'Y');
    const ask = await tapUntil(s, MANGA.menuEntry(2, 3), 'the remove question', isEv('dialog', {}));
    expect(ask.text).toBe('Remove Midnight Knight from your library? Reading progress stays on the server.');
    const removed = await tapUntil(s, REMOVE_CONFIRM.remove, 'the remove', isEv('library_remove', { ok: true }));
    expect(Number(removed.manga)).toBe(midnight);
    expect(await isInLibrary(midnight)).toBe(false);
    mark = s.events.mark();
    await s.driver.press('B');  // ends the search
    await s.events.waitForEvent('library', { search: null, state: 'ready' }, { from: mark });
    await s.events.waitForEvent('idle', {}, { from: mark });
    await tapUntil(s, tab('Browse'), 'the Browse tab', isEv('screen', { name: 'Browse' }));
}

/** The ids of the library manga in category `category`. */
async function libraryIdsIn(category: number): Promise<number[]> {
    const client = new SuwayomiClient(optionsFromEnv());
    const r = await client.query<{ mangas: { nodes: { id: number }[] } }>(
        `query M($c: Int!) { mangas(condition: { inLibrary: true }, filter: { categoryId: { equalTo: $c } }) { nodes { id } } }`,
        { c: category });
    return r.mangas.nodes.map((n) => n.id);
}

async function knownState(): Promise<void> {
    await resetTestSource();
    await removeTestSourceMangaFromLibrary();
    await restoreSeed();  // WP-14b ticks Long series for new manga
}

/** The latest "global_search" event's rows. */
function searchRows(e: AppEvent): SearchRowEvent[] {
    return (e.rows ?? []) as SearchRowEvent[];
}

scenario('browse', async (s) => {
    const url = appServerUrl(s.driver.name);
    await knownState();
    try {
        await s.launch({ testConfig: { server: { url } } });
        await s.events.waitForEvent('screen', { name: 'Library' });
        await s.events.waitForEvent('library', { state: 'ready' }, { timeoutMs: 30_000 });
        await s.events.waitForEvent('idle', {});  // a tap before the first frame settles can get lost

        // 1. The Browse tab: Local, then English and Japanese, a label on each language's first row, no Last used yet.
        const times: Record<string, number> = {};
        const tabOpened = await timedTap(s, tab('Browse'), 'the sources', isEv('sources', { state: 'ready' }));
        times.sources = tabOpened.ms;
        const list = tabOpened.e;
        const rows = list.rows as SourceRowEvent[];
        expect(rows.map((r) => r.id)).toEqual(['0', TEST_SOURCE_IDS.en, TEST_SOURCE_IDS.ja]);
        expect(rows.map((r) => r.label)).toEqual(['Local', 'English', 'Japanese']);
        expect(rows.some((r) => r.lastUsed)).toBe(false);
        expect(list.lastUsed).toBe('');
        await s.expectGolden('sources', { masks: [CLOCK_MASK] });

        // Languages (WP-14b): every language ticked; untick Japanese (golden "languages"); the last one shown can't be
        // unticked; Done → the Japanese row goes. Then Japanese again.
        const languages = await tapUntil(s, BROWSE.languages, 'the Languages checklist', isEv('languages', { state: 'open' }));
        expect((languages.rows as LanguageRowEvent[]).map((r) => [r.code, r.name, r.shown])).toEqual(
            [['localsourcelang', 'Local', true], ['en', 'English', true], ['ja', 'Japanese', true]]);
        await tapUntil(s, LANGUAGES.row(2), 'Japanese unticked',
            (e) => e.ev === 'languages' && (e.rows as LanguageRowEvent[])[2]?.shown === false);
        await s.expectGolden('languages', { masks: [CLOCK_MASK] });
        await tapUntil(s, LANGUAGES.row(0), 'Local unticked',
            (e) => e.ev === 'languages' && (e.rows as LanguageRowEvent[])[0]?.shown === false);
        const refused = await tapUntil(s, LANGUAGES.row(1), 'English stays', isEv('languages', { state: 'refused' }));
        expect((refused.rows as LanguageRowEvent[])[1].shown).toBe(true);
        await tapUntil(s, LANGUAGES.row(0), 'Local ticked again',
            (e) => e.ev === 'languages' && (e.rows as LanguageRowEvent[])[0]?.shown === true);
        const hiding = await tapUntil(s, LANGUAGES.done, 'Japanese hidden',
            (e) => e.ev === 'sources' && e.state === 'ready' && JSON.stringify(e.hidden) === '["ja"]');
        expect((hiding.rows as SourceRowEvent[]).map((r) => r.id)).toEqual(['0', TEST_SOURCE_IDS.en]);
        await tapUntil(s, BROWSE.languages, 'the Languages checklist again', isEv('languages', { state: 'open' }));
        await tapUntil(s, LANGUAGES.row(2), 'Japanese ticked',
            (e) => e.ev === 'languages' && (e.rows as LanguageRowEvent[])[2]?.shown === true);
        const showing = await tapUntil(s, LANGUAGES.done, 'every language again',
            (e) => e.ev === 'sources' && e.state === 'ready' && (e.hidden as string[]).length === 0);
        expect((showing.rows as SourceRowEvent[]).map((r) => r.id)).toEqual(['0', TEST_SOURCE_IDS.en, TEST_SOURCE_IDS.ja]);
        // Those taps were driver taps: borealis is in touch mode now and hides the focus frame. One D-pad press only brings
        // the frame back (it moves nothing), so the source below opens with the frame on its first cover, as before.
        const leaveTouch = s.events.mark();
        await s.driver.press('UP');
        await s.events.waitForEvent('idle', {}, { from: leaveTouch });
        expect(focusedView(s)).toContain('browse/languages');

        // 2. SNX Test (EN) → Popular: page 1 (25), then the pages as the grid scrolls, 60 in all, no more.
        let mark = s.events.mark();
        const source = await timedTap(s, BROWSE.row(1), 'the first page',
            isEv('browse', { source: TEST_SOURCE_IDS.en, mode: 'popular', state: 'ready' }));
        times.firstPage = source.ms;
        const first = source.e;
        expect(Number(first.items)).toBeGreaterThanOrEqual(25);
        expect((first.titles as string[])[0]).toBe('Comet Courier');
        await s.expectGolden('source-popular', { masks: [CLOCK_MASK] });

        // Down through the grid until the list ends. Each next page's "ms": the grid asked → on screen.
        const last = await scrollToEnd(s, first);
        expect(last.items).toBe(60);
        expect(last.hasNext).toBe(false);
        expect(last.pages).toBe(3);
        const nextPages = s.events.events.slice(mark).filter((e) => e.ev === 'browse' && e.state === 'ready' && Number(e.pages) > 1);
        times.nextPage = Math.max(...nextPages.map((e) => Number(e.ms)));

        // Latest: the newest first (the catalog's last manga, en-14); then Popular again.
        const latest = await tapUntil(s, SOURCE.latest, 'latest', isEv('browse', { mode: 'latest', state: 'ready' }));
        expect((latest.titles as string[])[0]).toBe('Paper Garden');
        const popular = await tapUntil(s, SOURCE.popular, 'popular', isEv('browse', { mode: 'popular', state: 'ready' }));
        expect((popular.titles as string[])[0]).toBe('Comet Courier');

        // 3. Search "comet": the three comets; B clears the search, Popular again.
        await typedText(s, 'comet');
        const found = await tapUntil(s, SOURCE.search, 'the search', isEv('browse', { mode: 'search', query: 'comet', state: 'ready' }));
        expect([...(found.titles as string[])].sort()).toEqual(['Comet Courier', 'Comet Over the Bay', 'The Last Comet']);
        expect(found.hasNext).toBe(false);
        await s.expectGolden('source-search', { masks: [CLOCK_MASK] });
        mark = s.events.mark();
        await s.driver.press('B');
        await s.events.waitForEvent('browse', { mode: 'popular', query: '', state: 'ready' }, { from: mark, timeoutMs: 30_000 });
        await s.events.waitForEvent('idle', {}, { from: mark });

        // 4. Filters: Status = Completed, Genres → Action include, Type → Webtoon, Sort by Title ascending.
        const opened = await tapUntil(s, SOURCE.filters, 'the filters', isEv('filters', { state: 'ready' }));
        times.filters = Number(opened.ms);  // the screen opened → ready
        expect((opened.rows as FilterRowEvent[]).map((r) => r.name)).toEqual(
            ['Filters also work without a search text', 'Author', 'Status', 'In colour', 'Genres', 'Type', 'Sort by']);
        await focusFilterRow(s, 2);  // Status: A opens its list at "Any"; down twice = Completed
        await pressAndSettle(s, 'A');
        await pressAndSettle(s, 'DOWN', 2);
        await pressAndSettle(s, 'A');
        expect(filterRows(s)[2].value).toBe('Completed');
        await focusFilterRow(s, 4);  // Genres opens: Action .. Long Strip below it
        await pressAndSettle(s, 'A');
        expect(filterRows(s).length).toBe(14);
        await focusFilterRow(s, 5);  // Action: include
        await pressAndSettle(s, 'A');
        expect(filterRows(s)[5]).toMatchObject({ name: 'Action', value: 'include', depth: 1 });
        await s.expectGolden('filters', { masks: [CLOCK_MASK] });
        // No Completed Webtoon has Action (the catalog): A twice more, exclude, then any again.
        await pressAndSettle(s, 'A');
        expect(filterRows(s)[5].value).toBe('exclude');
        await pressAndSettle(s, 'A');
        expect(filterRows(s)[5].value).toBe('any');
        await focusFilterRow(s, 12);  // Type opens: Manga, Manhwa, Webtoon
        await pressAndSettle(s, 'A');
        await focusFilterRow(s, 15);  // Webtoon
        await pressAndSettle(s, 'A');
        expect(filterRows(s)[15]).toMatchObject({ name: 'Webtoon', value: 'on' });
        await focusFilterRow(s, 16);  // Sort by: Title (a new sort starts descending), then Title again = ascending
        await pressAndSettle(s, 'A');
        await pressAndSettle(s, 'DOWN');
        await pressAndSettle(s, 'A');
        await pressAndSettle(s, 'A');
        await pressAndSettle(s, 'A');
        expect(filterRows(s)[16].value).toBe('Title, ascending');
        const applied = await tapUntil(s, FILTERS.apply, 'the filtered search',
            isEv('browse', { mode: 'search', query: '', filters: 3, state: 'ready' }));
        const predicted = selectManga(CATALOG_EN, 'search',
            { status: 'completed', types: ['Webtoon'], sort: { index: 1, ascending: true } });
        expect(predicted.length).toBeGreaterThan(0);
        expect(applied.titles).toEqual(predicted.slice(0, 10).map((m) => m.title));
        expect(applied.items).toBe(Math.min(predicted.length, 25));
        // Reset → Apply: nothing changed, so Popular again.
        await tapUntil(s, SOURCE.filters, 'the filters again', isEv('filters', { state: 'ready' }));
        mark = s.events.mark();
        await s.driver.touch(FILTERS.reset.x, FILTERS.reset.y);
        await s.events.waitFor('reset', (e) => e.ev === 'filters' && e.changed === 0, { from: mark });
        await tapUntil(s, FILTERS.apply, 'popular again', isEv('browse', { mode: 'popular', filters: 0, state: 'ready' }));

        // 5. A source failure: the switch file fails en "search"; Refresh retries once it is cleared.
        setTestSourceControl({ fail: [{ lang: 'en', call: 'search' }] });
        await typedText(s, 'x');
        const failed = await tapUntil(s, SOURCE.search, 'the failed search', isEv('browse', { mode: 'search', query: 'x', state: 'failed' }));
        // The source's message, cut to its first line without "Exception while fetching data (…) : ".
        expect(failed.message).toBe('The server reported a problem: SNX test failure: search');
        expect(failed.kind).toBe('server');
        clearTestSourceControl();
        const retried = await tapUntil(s, SOURCE.refresh, 'the search again',
            (e) => e.ev === 'browse' && e.query === 'x' && (e.state === 'ready' || e.state === 'empty'));
        expect(retried.items).toBe(selectManga(CATALOG_EN, 'search', { query: 'x' }).slice(0, 25).length);
        mark = s.events.mark();
        await s.driver.press('B');
        await s.events.waitForEvent('browse', { mode: 'popular', query: '', state: 'ready' }, { from: mark, timeoutMs: 30_000 });
        await s.events.waitForEvent('idle', {}, { from: mark });

        // 6. Comet Courier (the first cover): the preview loads it from the source (the author shows), Add to library → the
        // picker in choose mode (golden "add-picker") → tick Reading → Add: in the library, exactly in Reading.
        const reading = (await serverCategories()).find((c) => c.name === 'Reading')!.id;
        const preview = await tapUntil(s, SOURCE.cover(0), 'the preview with its details',
            (e) => e.ev === 'manga' && e.state === 'ready' && e.categoriesButton === 'Add to library');
        expect(preview.inLibrary).toBe(false);
        expect(String(preview.author)).not.toBe('');
        const picker = await tapUntil(s, MANGA.categories, 'the picker', isEv('category_picker', { state: 'ready', mode: 'choose' }));
        expect((picker.rows as { name: string; checked: boolean }[]).map((r) => r.checked)).toEqual([false, false, false, false]);
        await s.expectGolden('add-picker', { masks: [CLOCK_MASK] });
        await tapUntil(s, PICKER.row(0), 'Reading ticked',
            (e) => e.ev === 'category_picker' && (e.rows as { checked: boolean }[])[0]?.checked === true);
        const added = await tapUntil(s, PICKER.save, 'the add', isEv('library_add', { ok: true }));
        expect(added).toMatchObject({ categories: [reading], duplicate: false, asked: true });
        const comet = Number(added.manga);
        expect(await mangaCategoryIds(comet)).toEqual([reading]);
        await s.events.waitFor('Categories: Reading', (e) => e.ev === 'manga' && e.inLibrary === true &&
            e.categoriesButton === 'Categories: Reading', { from: s.events.events.indexOf(added) });
        // The source's grid already knows (its cover gets the "In library" tag); back to it.
        await s.events.waitFor('the tag', (e) => e.ev === 'browse' && (e.inLibrary as number[]).includes(comet),
            { from: s.events.events.indexOf(added) });
        mark = s.events.mark();
        await s.driver.press('B');
        await s.events.waitForEvent('screen', { name: 'Source' }, { from: mark });
        await s.events.waitForEvent('idle', {}, { from: mark });
        await s.expectGolden('source-tagged', { masks: [CLOCK_MASK] });

        // 7. Paper Garden (the second cover, en-14) → Add with nothing ticked → Default.
        await tapUntil(s, SOURCE.cover(1), 'the Paper Garden preview',
            (e) => e.ev === 'manga' && e.state === 'ready' && e.categoriesButton === 'Add to library');
        await tapUntil(s, MANGA.categories, 'the picker', isEv('category_picker', { state: 'ready', mode: 'choose' }));
        const paperAdded = await tapUntil(s, PICKER.save, 'the add', isEv('library_add', { ok: true }));
        expect(paperAdded).toMatchObject({ categories: [], duplicate: false, asked: true });
        const paper = Number(paperAdded.manga);
        expect(await mangaCategoryIds(paper)).toEqual([]);
        await backTo(s, 'Source');
        await backTo(s, 'Browse');

        // SNX Mature (an 18+ extension) installed, Refresh: its row has the "18+" tag.
        await installExtension(EXTENSIONS.mature.pkg);
        const withMature = await tapUntil(s, BROWSE.refresh, 'the sources again',
            (e) => e.ev === 'sources' && e.state === 'ready' && (e.rows as SourceRowEvent[]).some((r) => r.id === TEST_SOURCE_IDS.matureEn));
        const matureRows = withMature.rows as SourceRowEvent[];
        const matureRow = matureRows.findIndex((r) => r.id === TEST_SOURCE_IDS.matureEn && !r.lastUsed);
        expect(matureRows[matureRow].nsfw).toBe(true);
        await s.expectGolden('sources-mature', { masks: [CLOCK_MASK] });

        // Its Paper Garden → Add → the same-title box (golden "same-title") → Show entry: SNX Test's Paper Garden, in the library.
        mark = s.events.mark();
        await s.driver.touch(BROWSE.row(matureRow).x, BROWSE.row(matureRow).y);
        const mature = await s.events.waitForEvent('browse', { source: TEST_SOURCE_IDS.matureEn, mode: 'popular', state: 'ready' },
            { from: mark, timeoutMs: 30_000 });
        await s.events.waitForEvent('idle', {}, { from: mark });
        const maturePaper = (mature.titles as string[]).indexOf('Paper Garden');
        expect(maturePaper).toBeGreaterThanOrEqual(0);
        await tapUntil(s, SOURCE.cover(maturePaper), 'the SNX Mature Paper Garden preview',
            (e) => e.ev === 'manga' && e.state === 'ready' && e.categoriesButton === 'Add to library');
        const box = await tapUntil(s, MANGA.categories, 'the same-title box', isEv('dialog', {}));
        expect(box.text).toBe('You have an entry in your library with the same name: Paper Garden (SNX Test (EN))');
        await s.expectGolden('same-title', { masks: [CLOCK_MASK] });
        const shown = await tapUntil(s, SAME_TITLE.showEntry, 'the entry', (e) => e.ev === 'manga' && e.id === paper && e.state === 'ready');
        expect(shown.inLibrary).toBe(true);
        await backTo(s, 'Manga');

        // Add anyway → the picker → Add: Paper Garden is in the library twice.
        await tapUntil(s, MANGA.categories, 'the same-title box again', isEv('dialog', {}));
        await tapUntil(s, SAME_TITLE.addAnyway, 'the picker', isEv('category_picker', { state: 'ready', mode: 'choose' }));
        const twice = await tapUntil(s, PICKER.save, 'the second add', isEv('library_add', { ok: true }));
        expect(twice).toMatchObject({ duplicate: true });
        expect(await libraryCount('Paper Garden')).toBe(2);
        await backTo(s, 'Source');
        await backTo(s, 'Browse');

        // 8. Search all "comet" (WP-14b), every test-source call 2 s slower: 4 sources, 3 at a time. The order is the list's
        // without its Last used row (SNX Mature (EN), opened in step 7, isn't searched twice). Done: golden "search-all".
        setTestSourceControl({ delayMs: 2000 });
        await typedText(s, 'comet');
        mark = s.events.mark();
        await s.driver.touch(BROWSE.searchAll.x, BROWSE.searchAll.y);
        const started = await s.events.waitForEvent('global_search', { query: 'comet' }, { from: mark, timeoutMs: 30_000 });
        expect(searchRows(started).map((r) => r.id)).toEqual(
            ['0', TEST_SOURCE_IDS.matureEn, TEST_SOURCE_IDS.en, TEST_SOURCE_IDS.ja]);
        expect(searchRows(started).map((r) => r.state)).toEqual(['searching', 'searching', 'searching', 'waiting']);
        const searched = await s.events.waitFor('Search all done', (e) => e.ev === 'global_search' && e.finished === true,
            { from: mark, timeoutMs: 60_000 });
        await s.events.waitForEvent('idle', {}, { from: s.events.events.indexOf(searched) });
        expect(searchRows(searched).map((r) => r.state)).toEqual(['nothing_found', 'nothing_found', 'found', 'nothing_found']);
        const comets = searchRows(searched)[2];
        expect(comets).toMatchObject({ found: 3, more: false, shown: 3 });
        expect([...comets.titles].sort()).toEqual(['Comet Courier', 'Comet Over the Bay', 'The Last Comet']);
        // Every row waited for its own answer: nothing jumped while the others came in.
        const order = s.events.events.slice(mark).filter((e) => e.ev === 'global_search').map((e) => searchRows(e).map((r) => r.id).join());
        expect(new Set(order).size).toBe(1);

        // The D-pad: down to SNX Test (EN)'s card, right to its last cover; down into SNX Test (JA)'s row (nothing found:
        // only its card can take the focus) lands on that card; up again; A on the card: the source's search for "comet".
        await pressUntilFocus(s, 'DOWN', 'search/card-2', 4);
        await s.expectGolden('search-all', { masks: [CLOCK_MASK] });  // SNX Test (EN)'s row in the middle, its card focused
        await pressUntilFocus(s, 'RIGHT', 'search/cover-2-2', 3);
        mark = s.events.mark();
        await s.driver.press('RIGHT');  // no more covers: the focus stays (nothing moves, so no "focus" or "idle" event)
        const moved = await s.events.waitForEvent('focus', {}, { from: mark, timeoutMs: 1_500 }).then(() => true, () => false);
        expect(moved, 'RIGHT past the last cover moves the focus').toBe(false);
        expect(focusedView(s)).toContain('search/cover-2-2');
        await pressUntilFocus(s, 'DOWN', 'search/card-3', 1);
        await pressUntilFocus(s, 'UP', 'search/card-2', 1);
        clearTestSourceControl();
        mark = await pressSettled(s, 'A');
        const fromCard = await s.events.waitForEvent('browse', { source: TEST_SOURCE_IDS.en, mode: 'search', query: 'comet', state: 'ready' },
            { from: mark, timeoutMs: 30_000 });
        expect(fromCard.items).toBe(3);
        mark = s.events.mark();
        await s.driver.press('B');  // ends the search (the source's own rule), then B leaves
        await s.events.waitForEvent('browse', { mode: 'popular', state: 'ready' }, { from: mark, timeoutMs: 30_000 });
        await s.events.waitForEvent('idle', {}, { from: mark });
        await backTo(s, 'GlobalSearch');
        await backTo(s, 'Browse');

        // Over the 3 sources (SNX Mature uninstalled, nothing slowed): the time from the text to the last answer.
        await uninstallExtension(EXTENSIONS.mature.pkg);
        await tapUntil(s, BROWSE.refresh, 'the sources without SNX Mature',
            (e) => e.ev === 'sources' && e.state === 'ready' && !(e.rows as SourceRowEvent[]).some((r) => r.id === TEST_SOURCE_IDS.matureEn));
        await typedText(s, 'comet');
        const quick = await tapUntil(s, BROWSE.searchAll, 'Search all over 3 sources',
            (e) => e.ev === 'global_search' && e.finished === true);
        expect(searchRows(quick).map((r) => r.id)).toEqual(['0', TEST_SOURCE_IDS.en, TEST_SOURCE_IDS.ja]);
        expect(searchRows(quick)[1].found).toBe(3);
        times.searchAll = Number(quick.ms);
        await backTo(s, 'Browse');

        // The Library's Reading tab has Comet Courier (step 6).
        await tapUntil(s, tab('Library'), 'the library', isEv('library', { state: 'ready' }));
        const shelf = await tapUntil(s, LIBRARY.categories.reading, 'Reading', isEv('library', { state: 'ready', category: reading }));
        const inReading = await libraryIdsIn(reading);
        expect(inReading).toContain(comet);
        expect(shelf.total).toBe(inReading.length);

        // 9. Remove Comet Courier from its preview's menu (the footer's Menu hint, then Remove: 5th of 6 since WP-17) → the
        // confirm (golden "remove-confirm") → out of the library; the preview offers Add to library again, the grid drops the tag.
        await tapUntil(s, tab('Browse'), 'the Browse tab', isEv('screen', { name: 'Browse' }));
        await tapUntil(s, BROWSE.row(sourceRow(s, TEST_SOURCE_IDS.en)), 'SNX Test (EN)',
            isEv('browse', { source: TEST_SOURCE_IDS.en, mode: 'popular', state: 'ready' }));
        await tapUntil(s, SOURCE.cover(0), 'Comet Courier, in the library',
            (e) => e.ev === 'manga' && e.state === 'ready' && e.id === comet && e.categoriesButton === 'Categories: Reading');
        mark = s.events.mark();
        await s.driver.touch(MENU_HINT.x, MENU_HINT.y);
        await s.events.waitForEvent('idle', {}, { from: mark });
        // 7 entries since WP-18 (Download to Switch...): the dropdown shows 6 at a time, so Remove keeps its 6-entry place.
        const ask = await tapUntil(s, MANGA.menuEntry(4, 6), 'the remove question', isEv('dialog', {}));
        expect(ask.text).toBe('Remove Comet Courier from your library? Reading progress stays on the server.');
        await s.expectGolden('remove-confirm', { masks: [CLOCK_MASK] });
        const removed = await tapUntil(s, REMOVE_CONFIRM.remove, 'the remove', isEv('library_remove', { ok: true }));
        expect(Number(removed.manga)).toBe(comet);
        expect(await isInLibrary(comet)).toBe(false);
        const afterRemove = s.events.events.indexOf(removed);
        await s.events.waitFor('Add to library again', (e) => e.ev === 'manga' && e.inLibrary === false &&
            e.categoriesButton === 'Add to library', { from: afterRemove });
        await s.events.waitFor('the tag gone', (e) => e.ev === 'browse' && e.source === TEST_SOURCE_IDS.en &&
            !(e.inLibrary as number[]).includes(comet), { from: afterRemove });
        await backTo(s, 'Source');
        await backTo(s, 'Browse');

        // 10. Tick for new manga (WP-14b): Library → Edit → Long series → its menu's Tick for new manga: the row says so
        // (golden "categories-ticked") and the server has the "default" flag. A preview's Add then opens the picker with
        // Long series ticked (B: no add).
        const longSeries = (await serverCategories()).find((c) => c.name === 'Long series')!.id;
        await tapUntil(s, tab('Library'), 'the library', isEv('library', { state: 'ready' }));
        await tapUntil(s, LIBRARY.edit, 'the Categories screen', isEv('categories', { state: 'ready' }));
        await tapUntil(s, CATEGORIES.row(1), 'the row menu', (e) => e.ev === 'focus');
        const ticked = await tapUntil(s, MANGA.menuEntry(4, 5), 'the tick', isEv('category_action', { action: 'tick', ok: true }));
        expect(ticked.id).toBe(longSeries);
        await s.events.waitFor('the ticked row', (e) => e.ev === 'categories' &&
            (e.rows as { id: number; tick: boolean }[]).some((r) => r.id === longSeries && r.tick), { from: s.events.events.indexOf(ticked) });
        expect((await serverCategories()).find((c) => c.id === longSeries)?.default).toBe(true);
        await s.expectGolden('categories-ticked', { masks: [CLOCK_MASK] });
        await backTo(s, 'Library');
        await tapUntil(s, tab('Browse'), 'the Browse tab', isEv('screen', { name: 'Browse' }));
        await tapUntil(s, BROWSE.row(sourceRow(s, TEST_SOURCE_IDS.en)), 'SNX Test (EN)',
            isEv('browse', { source: TEST_SOURCE_IDS.en, mode: 'popular', state: 'ready' }));
        await tapUntil(s, SOURCE.cover(2), 'the Midnight Knight preview',
            (e) => e.ev === 'manga' && e.state === 'ready' && e.categoriesButton === 'Add to library');
        const preticked = await tapUntil(s, MANGA.categories, 'the picker', isEv('category_picker', { state: 'ready', mode: 'choose' }));
        expect((preticked.rows as { name: string; checked: boolean }[]).map((r) => [r.name, r.checked])).toEqual(
            [['Reading', false], ['Long series', true], ['Big shelf', false], ['Empty', false]]);
        await backTo(s, 'Manga');
        await backTo(s, 'Source');
        await backTo(s, 'Browse');

        // 11. Switch only (the desktop can't press Y, L, R or MINUS): the Y menus, Don't ask again, Settings, L / R.
        if (s.driver.name === 'switch') await switchButtons(s, comet, longSeries);

        // 12. Memory: 10 rounds of opening SNX Test (EN) and scrolling to the end; the memory in use stays flat.
        const before = await memNow(s);
        for (let i = 0; i < ROUNDS; i++) {
            const round = await tapUntil(s, BROWSE.row(sourceRow(s, TEST_SOURCE_IDS.en)), `round ${i + 1}: SNX Test (EN)`,
                isEv('browse', { source: TEST_SOURCE_IDS.en, mode: 'popular', state: 'ready' }));
            expect((await scrollToEnd(s, round)).items).toBe(60);
            await backTo(s, 'Browse');
        }
        const after = await memNow(s);
        // Every add and remove counts (Add / the confirm → the server's answer; an add of a manga the server hasn't
        // loaded yet includes loading its details from the source).
        const slowest = (ev: string) => Math.max(...s.events.events.filter((e) => e.ev === ev && e.ok === true).map((e) => Number(e.ms)));
        times.add = slowest('library_add');
        times.remove = slowest('library_remove');
        console.log(`[browse] ${s.driver.name}: ${JSON.stringify(times)} ms; ${ROUNDS} rounds ${(before / MiB).toFixed(1)} -> ${(after / MiB).toFixed(1)} MiB`);
        expect(after - before, `memory growth over ${ROUNDS} source rounds (bytes)`).toBeLessThan(LIMITS.roundsBytes);

        // The D56 numbers (WP-14 Acceptance).
        expect(times.sources, 'the sources on screen after the tab opens (ms)').toBeLessThanOrEqual(LIMITS.sourcesMs);
        expect(times.firstPage, "a source's first page in the grid (ms)").toBeLessThanOrEqual(LIMITS.firstPageMs);
        expect(times.nextPage, 'the slowest next page (ms)').toBeLessThanOrEqual(LIMITS.nextPageMs);
        expect(times.filters, 'the filters screen ready (ms)').toBeLessThanOrEqual(LIMITS.filtersMs);
        expect(times.add, 'an add answered (ms)').toBeLessThanOrEqual(LIMITS.addMs);
        expect(times.remove, 'a remove answered (ms)').toBeLessThanOrEqual(LIMITS.removeMs);
        expect(times.searchAll, 'Search all over 3 sources done (ms)').toBeLessThanOrEqual(LIMITS.searchAllMs);
    } finally {
        await knownState();
    }
});
