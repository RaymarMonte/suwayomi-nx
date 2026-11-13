// library-tools (WP-24, D54, TESTING.md): search, the Categories screen and the category picker on the test server's library
// (seed.ts: Default, Reading, Long series, Big shelf, Empty; Alpha is in Reading). Checks, on desktop and on the Switch:
// - Search: "alp" finds Alpha in the whole library (golden "search"; desktop types into the real text dialog once), "zzz"
//   finds nothing (the search-empty message), B ends the search and the category tabs come back;
// - Edit opens the Categories screen (golden "categories"): Add "Test shelf", a taken name (" reading ") is refused, Rename,
//   Move up, Delete after its question (golden "dialog"); the server has the seed again afterwards;
// - Alpha's details -> Categories: the picker ticks Reading; tick Long series, Save (golden "picker"): the server has both;
// - 10 rounds Library -> Categories -> back: the memory in use grows by less than 16 MiB;
// - Switch only (desktop can't press Y or MINUS): Y on a cover opens its menu -> Categories... opens the picker; MINUS ->
//   Search... searches.
// Times (asserted on the Switch): search <= 1.5 s, picker open <= 1 s, a category action <= 1 s, in the app's own clock from
// a played tap's finger down (timedTap); Save from the X press in the harness's time (an upper bound).
// Text goes in through the test build's text command (lib/touches.ts typedText): the Switch's keyboard can't be typed into.
// The scenario puts the seed back at the end, also after a failure (lib/categories.ts restoreSeed).
// Needs the test server (tools/test-server/test-server.sh start).

import { expect } from 'vitest';

import { libraryMangaId, mangaCategoryIds, restoreSeed, SEED_CATEGORIES, serverCategories } from '../lib/categories.ts';
import type { AppEvent } from '../lib/events.ts';
import { scenario, type Scenario } from '../lib/scenario.ts';
import { CATEGORIES, CLOCK_MASK, DIALOG, KEYBOARD_OK, LIBRARY, MANGA, PICKER } from '../lib/screens.ts';
import { appServerUrl } from '../lib/suwayomi.ts';
import { playTouches, tapFrames, typedText } from '../lib/touches.ts';

const MiB = 1024 * 1024;
const ROUNDS = 10;
const LIMITS = { searchMs: 1_500, pickerMs: 1_000, actionMs: 1_000, roundsBytes: 16 * MiB };

type Row = { id: number; name: string; count?: number; checked?: boolean };

/** Waits for the next "mem" event (one every 10 s) and returns its "used". */
async function memNow(s: Scenario): Promise<number> {
    const mem = await s.events.waitForEvent('mem', {}, { from: s.events.mark(), timeoutMs: 15_000 });
    return Number(mem.used);
}

/** Taps (x, y), waits for the first event that `match` accepts and for the screen to settle; returns it and the time. */
async function tapUntil(s: Scenario, at: { x: number; y: number }, what: string, match: (e: AppEvent) => boolean): Promise<{ e: AppEvent; ms: number }> {
    const mark = s.events.mark();
    const started = Date.now();
    await s.driver.touch(at.x, at.y);
    const e = await s.events.waitFor(what, match, { from: mark, timeoutMs: 30_000 });
    const ms = Date.now() - started;
    await s.events.waitForEvent('idle', {}, { from: mark });
    return { e, ms };
}

/**
 * Like tapUntil, but the tap is played through the test build's touch channel, and the time is the app's own: from the
 * finger touching (the `command` event's `started`) to the event `match` accepts, so it includes the 50 ms the finger is
 * down. A driver tap on the Switch adds about 0.9 s of its own (sys-autopilot, the event polling), so the limits are
 * checked on this time.
 */
async function timedTap(s: Scenario, at: { x: number; y: number }, what: string, match: (e: AppEvent) => boolean): Promise<{ e: AppEvent; ms: number }> {
    const mark = s.events.mark();
    const played = await playTouches(s, tapFrames(at));
    const e = await s.events.waitFor(what, match, { from: mark, timeoutMs: 30_000 });
    await s.events.waitForEvent('idle', {}, { from: mark });
    const from = played.started !== undefined ? Number(played.started) : Number(played.t);
    return { e, ms: Math.max(0, Number(e.t) - from) };
}

const isEv = (name: string, fields: Record<string, unknown>) => (e: AppEvent) =>
    e.ev === name && Object.entries(fields).every(([k, v]) => e[k] === v);
const rowsOf = (e: AppEvent) => e.rows as Row[];
const names = (e: AppEvent) => rowsOf(e).map((r) => r.name);

scenario('library-tools', async (s) => {
    const url = appServerUrl(s.driver.name);
    const onSwitch = s.driver.name === 'switch';
    await restoreSeed();
    const seed = await serverCategories();
    expect(seed.map((c) => c.name)).toEqual([...SEED_CATEGORIES]);
    const reading = seed[0].id;
    const longSeries = seed[1].id;
    const alpha = await libraryMangaId('Alpha');
    const times: Record<string, number> = {};

    try {
        await s.launch({ testConfig: { server: { url } } });
        await s.events.waitForEvent('screen', { name: 'Library' });
        const first = await s.events.waitForEvent('library', { state: 'ready' }, { timeoutMs: 30_000 });
        await s.events.waitForEvent('idle', {}, { from: s.events.events.indexOf(first) });

        // Search "alp": the whole library. Desktop types it into the real text dialog; the Switch uses the text command.
        let mark = s.events.mark();
        let started = Date.now();
        if (onSwitch) {
            await typedText(s, 'alp');
            started = Date.now();
            await s.driver.touch(LIBRARY.search.x, LIBRARY.search.y);
        } else {
            await s.driver.touch(LIBRARY.search.x, LIBRARY.search.y); // opens the text dialog
            await s.events.waitForEvent('idle', {}, { from: mark });
            await s.driver.typeText('alp');
            started = Date.now();
            await s.driver.touch(KEYBOARD_OK.x, KEYBOARD_OK.y);
        }
        const found = await s.events.waitForEvent('library', { search: 'alp', state: 'ready' }, { from: mark, timeoutMs: 30_000 });
        times.alpWithHarnessMs = Date.now() - started;  // logged only: includes the driver's tap
        expect(found.titles as string[]).toContain('Alpha');
        expect(found.titles as string[]).not.toContain('Beta');
        await s.events.waitForEvent('idle', {}, { from: mark });
        await s.expectGolden('search', { masks: [CLOCK_MASK] });

        // "zzz": nothing; B ends the search and Default's covers come back.
        await typedText(s, 'zzz');
        const none = await timedTap(s, LIBRARY.search, 'no results for zzz', isEv('library', { search: 'zzz', state: 'empty' }));
        times.searchMs = none.ms;  // the server's title filter over all 157 manga
        mark = s.events.mark();
        await s.driver.press('B');
        await s.events.waitForEvent('library', { search: null, state: 'ready', category: 0 }, { from: mark });
        await s.events.waitForEvent('idle', {}, { from: mark });

        // Edit -> the Categories screen.
        const opened = await tapUntil(s, LIBRARY.edit, 'the categories list', isEv('categories', { state: 'ready' }));
        expect(names(opened.e)).toEqual([...SEED_CATEGORIES]);
        expect(rowsOf(opened.e).map((r) => r.count)).toEqual([3, 2, 150, 0]);
        await s.expectGolden('categories', { masks: [CLOCK_MASK] });

        // Add "Test shelf"; a name that is taken (case and spaces don't count) is refused without a request.
        await typedText(s, 'Test shelf');
        const added = await timedTap(s, CATEGORIES.add, 'add', isEv('category_action', { action: 'add' }));
        times.addMs = added.ms;
        expect(added.e.ok).toBe(true);
        let list = await s.events.waitFor('the list with Test shelf', (e) => e.ev === 'categories' && names(e).includes('Test shelf'));
        expect(names(list)).toEqual([...SEED_CATEGORIES, 'Test shelf']);
        await typedText(s, '  reading ');
        const taken = await tapUntil(s, CATEGORIES.add, 'add refused', isEv('category_action', { action: 'add', ok: false }));
        expect(taken.e.problem).toBe('taken');

        // Rename (the row's menu, entry 1 of 4).
        await tapUntil(s, CATEGORIES.row(4), 'the row menu', (e) => e.ev === 'focus');
        await typedText(s, 'Renamed shelf');
        const renamed = await timedTap(s, MANGA.menuEntry(0, 4), 'rename', isEv('category_action', { action: 'rename' }));
        times.renameMs = renamed.ms;
        expect(renamed.e.ok).toBe(true);
        list = await s.events.waitFor('the list with Renamed shelf', (e) => e.ev === 'categories' && names(e).includes('Renamed shelf'));
        expect(names(list)).toEqual([...SEED_CATEGORIES, 'Renamed shelf']);

        // Move up: before Empty.
        await tapUntil(s, CATEGORIES.row(4), 'the row menu', (e) => e.ev === 'focus');
        const moved = await timedTap(s, MANGA.menuEntry(1, 4), 'move', isEv('category_action', { action: 'move' }));
        times.moveMs = moved.ms;
        expect(moved.e.ok).toBe(true);
        list = await s.events.waitFor('the moved list', (e) => e.ev === 'categories' && names(e)[3] === 'Renamed shelf');
        expect(names(list)).toEqual(['Reading', 'Long series', 'Big shelf', 'Renamed shelf', 'Empty']);

        // Delete: a question first (golden "dialog"), then the category goes.
        const asked = await tapUntil(s, CATEGORIES.row(3), 'the row menu', (e) => e.ev === 'focus');
        void asked;
        const dialog = await tapUntil(s, MANGA.menuEntry(3, 4), 'the delete question', (e) => e.ev === 'dialog');
        expect(String(dialog.e.text)).toContain('Renamed shelf');
        await s.expectGolden('dialog', { masks: [CLOCK_MASK] });
        const deleted = await timedTap(s, DIALOG.confirm, 'delete', isEv('category_action', { action: 'delete' }));
        times.deleteMs = deleted.ms;
        expect(deleted.e.ok).toBe(true);
        list = await s.events.waitFor('the list without it', (e) => e.ev === 'categories' && !names(e).includes('Renamed shelf'));
        expect(names(list)).toEqual([...SEED_CATEGORIES]);
        expect((await serverCategories()).map((c) => c.name)).toEqual([...SEED_CATEGORIES]);

        // Back: the library loads again (something changed).
        mark = s.events.mark();
        await s.driver.press('B');
        await s.events.waitForEvent('library', { state: 'ready' }, { from: mark, timeoutMs: 30_000 });
        await s.events.waitForEvent('idle', {}, { from: mark });

        // Alpha (Reading's first cover) -> Categories: the picker.
        await tapUntil(s, LIBRARY.categories.reading, 'Reading', isEv('library', { state: 'ready', category: reading }));
        const manga = await tapUntil(s, LIBRARY.cover(0), 'Alpha', isEv('screen', { name: 'Manga' }));
        expect(manga.e.params).toMatchObject({ id: alpha, title: 'Alpha' });
        await s.events.waitForEvent('manga', { state: 'ready', id: alpha }, { timeoutMs: 30_000 });
        const picker = await timedTap(s, MANGA.categories, 'the picker', isEv('category_picker', { state: 'ready' }));
        times.pickerMs = picker.ms;
        expect(rowsOf(picker.e).map((r) => [r.name, r.checked])).toEqual([
            ['Reading', true], ['Long series', false], ['Big shelf', false], ['Empty', false],
        ]);
        const ticked = await tapUntil(s, PICKER.row(1), 'Long series ticked',
            (e) => e.ev === 'category_picker' && rowsOf(e)[1]?.checked === true);
        void ticked;
        await s.expectGolden('picker', { masks: [CLOCK_MASK] });
        mark = s.events.mark();
        started = Date.now();
        await s.driver.press('X');
        const saved = await s.events.waitForEvent('category_picker', { state: 'saved' }, { from: mark, timeoutMs: 30_000 });
        times.saveMs = Date.now() - started;
        expect((saved.saved as number[]).slice().sort((a, b) => a - b)).toEqual([reading, longSeries].sort((a, b) => a - b));
        await s.events.waitForEvent('screen', { name: 'Manga' }, { from: mark });
        await s.events.waitForEvent('idle', {}, { from: mark });
        expect(await mangaCategoryIds(alpha)).toEqual([reading, longSeries].sort((a, b) => a - b));

        // Back to the library: Alpha is still in Reading, and the Long series tab (loaded again: its members changed) has it.
        mark = s.events.mark();
        await s.driver.press('B');
        await s.events.waitForEvent('idle', {}, { from: mark });
        const longTab = await tapUntil(s, LIBRARY.categories.longSeries, 'Long series',
            isEv('library', { state: 'ready', category: longSeries }));
        expect(longTab.e).toMatchObject({ items: 3, total: 3 });  // the seed's 2 + Alpha

        if (onSwitch) await switchButtons(s, alpha, reading);

        // 10 rounds Library -> Categories -> back: the memory stays flat.
        const before = await memNow(s);
        for (let i = 0; i < ROUNDS; i++) {
            await tapUntil(s, LIBRARY.edit, `round ${i + 1}: the categories list`, isEv('categories', { state: 'ready' }));
            mark = s.events.mark();
            await s.driver.press('B');
            await s.events.waitForEvent('idle', {}, { from: mark });
        }
        const after = await memNow(s);
        const growth = after - before;
        console.log(`[library-tools] ${s.driver.name}: ${JSON.stringify(times)} ms; ${ROUNDS} rounds ${(before / MiB).toFixed(1)} -> ${(after / MiB).toFixed(1)} MiB`);
        expect(growth, `memory growth over ${ROUNDS} Categories rounds (bytes)`).toBeLessThan(LIMITS.roundsBytes);
        if (onSwitch) {
            expect(times.searchMs, 'search (ms)').toBeLessThanOrEqual(LIMITS.searchMs);
            expect(times.pickerMs, 'picker open (ms)').toBeLessThanOrEqual(LIMITS.pickerMs);
            for (const key of ['addMs', 'renameMs', 'moveMs', 'deleteMs', 'saveMs']) {
                expect(times[key], `${key} (ms)`).toBeLessThanOrEqual(LIMITS.actionMs);
            }
        }

        s.expectNoErrorEvents();
        await s.driver.quit();
    } finally {
        await restoreSeed();
    }
});

/**
 * The buttons desktop borealis can't press, so the Switch only. Starts on the Library's Long series tab (the tab has the
 * focus): DOWN to Alpha's cover; Y opens its menu, the 2nd entry (Categories...) the picker (Reading and Long series
 * ticked); untick Long series, X saves and the picker closes: Alpha leaves the tab, the server has Reading only.
 * MINUS -> Search... (3rd of 5) searches, B ends the search.
 */
async function switchButtons(s: Scenario, alpha: number, reading: number): Promise<void> {
    // After a touch the first D-pad press only brings the focus frame back (borealis leaves touch mode): press until a
    // cover has the focus.
    let mark = s.events.mark();
    const onCover = () => String(s.events.events.filter((e) => e.ev === 'focus').at(-1)?.view ?? '').includes('cover-0');
    for (let i = 0; i < 3 && !onCover(); i++) {
        mark = s.events.mark();
        await s.driver.press('DOWN');
        await s.events.waitForEvent('focus', {}, { from: mark, timeoutMs: 3_000 }).catch(() => undefined);
    }
    expect(onCover(), 'DOWN: Alpha\'s cover has the focus').toBe(true);
    await s.events.waitForEvent('idle', {}, { from: mark });
    mark = s.events.mark();
    await s.driver.press('Y');
    await s.events.waitFor('Y: the cover menu has the focus', (e) => e.ev === 'focus' && !String(e.view).includes('CoverCell'), { from: mark });
    await s.events.waitForEvent('idle', {}, { from: mark });
    const picker = await tapUntil(s, MANGA.menuEntry(1, 2), 'the picker from the cover menu', isEv('screen', { name: 'CategoryPicker' }));
    expect(picker.e.params).toMatchObject({ id: alpha });
    const ready = await s.events.waitForEvent('category_picker', { state: 'ready' }, { from: s.events.events.indexOf(picker.e) });
    expect(rowsOf(ready).map((r) => r.checked)).toEqual([true, true, false, false]);
    await tapUntil(s, PICKER.row(1), 'Long series unticked', (e) => e.ev === 'category_picker' && rowsOf(e)[1]?.checked === false);
    mark = s.events.mark();
    await s.driver.press('X');
    await s.events.waitForEvent('category_picker', { state: 'saved' }, { from: mark, timeoutMs: 30_000 });
    await s.events.waitFor('Alpha leaves the Long series tab',
        (e) => e.ev === 'library' && e.category !== -1 && e.items === 2 && e.total === 2, { from: mark });
    await s.events.waitForEvent('idle', {}, { from: mark });
    expect(await mangaCategoryIds(alpha)).toEqual([reading]);

    mark = s.events.mark();
    await s.driver.press('MINUS');
    await s.events.waitForEvent('idle', {}, { from: mark });
    await typedText(s, 'alp');
    await tapUntil(s, MANGA.menuEntry(2, 5), 'MINUS -> Search...', isEv('library', { search: 'alp', state: 'ready' }));
    mark = s.events.mark();
    await s.driver.press('B');
    await s.events.waitForEvent('library', { search: null, state: 'ready' }, { from: mark });
    await s.events.waitForEvent('idle', {}, { from: mark });
}
