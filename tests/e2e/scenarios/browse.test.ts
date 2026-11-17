// browse (WP-14, D56, TESTING.md): the Browse tab and a source's screen on the test server's test source (WP-23, D55).
// Checks, on desktop and on the Switch:
// - the Browse tab lists Local, SNX Test (EN) and SNX Test (JA) with their language labels (golden "sources");
// - SNX Test (EN) → Popular: 25 manga, then 50 and 60 as the grid scrolls down, then no more (golden "source-popular");
// - Latest starts with Paper Garden (en-14, the newest); Popular again;
// - Search "comet" (the text command): the 3 comets (golden "source-search"); B clears it (Popular again);
// - Filters (golden "filters", Genres open, Action include): Action back to any (no Completed Webtoon has Action), Status =
//   Completed, Type → Webtoon, Sort by Title ascending → Apply: the 4 manga selectManga() predicts, in that order; Reset →
//   Apply: Popular again;
// - the switch file fails en "search": the error "SNX test failure: search"; Refresh (Retry) after clearing it works.
// Still to come (the next session, see docs/STATUS.md): add / remove, the same-title warning, the Switch-only Y / MINUS steps and the memory rounds.
// Known state first and at the end, also after a failure: the test source as seeded, no test-source manga in the library.
// Needs the test server (tools/test-server/test-server.sh start).

import { expect } from 'vitest';

import type { AppEvent } from '../lib/events.ts';
import { scenario, type Scenario } from '../lib/scenario.ts';
import { CLOCK_MASK, tab } from '../lib/screens.ts';
import { appServerUrl } from '../lib/suwayomi.ts';
import { typedText } from '../lib/touches.ts';
import {
    clearTestSourceControl, removeTestSourceMangaFromLibrary, resetTestSource, setTestSourceControl, TEST_SOURCE_IDS,
} from '../lib/test-source.ts';
import { CATALOG_EN, selectManga } from '../../../tools/gen-fixtures/test-source.ts';

/** The Browse tab's rows (BrowseTab: one column of 80 px rows, 4 px apart, under the 72 px bar). */
const BROWSE = {
    row: (i: number) => ({ x: 834, y: 223 + 84 * i }),
};
/** A source's screen: the bar (Popular | Latest on the left; Search, Filters, Refresh on the right). */
const SOURCE = {
    popular: { x: 72, y: 123 },
    latest: { x: 170, y: 123 },
    search: { x: 1012, y: 123 },
    filters: { x: 1110, y: 123 },
    refresh: { x: 1211, y: 123 },
};

/** The filters screen's Reset and Apply (top right, above the list). */
const FILTERS = {
    reset: { x: 1064, y: 136 },
    apply: { x: 1160, y: 136 },
};

type SourceRowEvent = { id: string; name: string; lang: string; nsfw: boolean; label: string; lastUsed: boolean };

/** Taps (x, y), waits for the first event that `match` accepts and for the screen to settle. */
async function tapUntil(s: Scenario, at: { x: number; y: number }, what: string, match: (e: AppEvent) => boolean): Promise<AppEvent> {
    const mark = s.events.mark();
    await s.driver.touch(at.x, at.y);
    const e = await s.events.waitFor(what, match, { from: mark, timeoutMs: 30_000 });
    await s.events.waitForEvent('idle', {}, { from: mark });
    return e;
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

async function knownState(): Promise<void> {
    await resetTestSource();
    await removeTestSourceMangaFromLibrary();
}

scenario('browse', async (s) => {
    const url = appServerUrl(s.driver.name);
    await knownState();
    try {
        await s.launch({ testConfig: { server: { url } } });
        await s.events.waitForEvent('screen', { name: 'Library' });
        await s.events.waitForEvent('library', { state: 'ready' }, { timeoutMs: 30_000 });

        // 1. The Browse tab: Local, then English and Japanese, a label on each language's first row, no Last used yet.
        const list = await tapUntil(s, tab('Browse'), 'the sources', isEv('sources', { state: 'ready' }));
        const rows = list.rows as SourceRowEvent[];
        expect(rows.map((r) => r.id)).toEqual(['0', TEST_SOURCE_IDS.en, TEST_SOURCE_IDS.ja]);
        expect(rows.map((r) => r.label)).toEqual(['Local', 'English', 'Japanese']);
        expect(rows.some((r) => r.lastUsed)).toBe(false);
        expect(list.lastUsed).toBe('');
        await s.expectGolden('sources', { masks: [CLOCK_MASK] });

        // 2. SNX Test (EN) → Popular: page 1 (25), then the pages as the grid scrolls, 60 in all, no more.
        let mark = s.events.mark();
        await s.driver.touch(BROWSE.row(1).x, BROWSE.row(1).y);
        await s.events.waitForEvent('screen', { name: 'Source' }, { from: mark });
        const first = await s.events.waitForEvent('browse', { source: TEST_SOURCE_IDS.en, mode: 'popular', state: 'ready' },
            { from: mark, timeoutMs: 30_000 });
        expect(Number(first.items)).toBeGreaterThanOrEqual(25);
        expect((first.titles as string[])[0]).toBe('Comet Courier');
        await s.events.waitForEvent('idle', {}, { from: mark });
        await s.expectGolden('source-popular', { masks: [CLOCK_MASK] });

        // Down through the grid until the list ends.
        let last = first;
        for (let i = 0; i < 20 && last.hasNext !== false; i++) {
            mark = s.events.mark();
            await s.driver.press('DOWN');
            await s.events.waitForEvent('idle', {}, { from: mark });
            last = s.events.events.filter((e) => e.ev === 'browse').at(-1) ?? last;
        }
        expect(last.items).toBe(60);
        expect(last.hasNext).toBe(false);
        expect(last.pages).toBe(3);

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
    } finally {
        await knownState();
    }
});
