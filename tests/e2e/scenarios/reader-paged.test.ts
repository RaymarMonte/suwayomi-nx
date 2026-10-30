// reader-paged (WP-11, TESTING.md): the paged reader on Alpha (Reading, 3 chapters x 10 JPEG pages). Checks, on desktop and
// on the Switch:
// - chapter 1 opens at its first page (golden "page"); the menu (golden "menu"); reading direction left to right through
//   the settings menu;
// - pages 2..10 by D-pad, by taps on the right edge (one slides a little: still a tap) and by swipes to the left; the colour
//   code on screen confirms each page;
// - the last page marks chapter 1 read on the server (GraphQL from the harness);
// - past the last page the "next chapter" screen (golden "end"), then chapter 2 opens at once (its first pages were fetched
//   ahead); back: the "previous chapter" screen, then chapter 1's last page;
// - fit width: the page is as wide as the screen;
// - 100 page turns: memory stays flat (the "reader" events' memUsed), the turn times (turnMs of prefetched pages; the
//   console must stay under 150 ms, D17);
// - B closes the reader and the details screen shows the new progress; a restart keeps Alpha's direction and fit.
// Needs the test server. The harness resets Alpha's chapters before and after.

import { expect } from 'vitest';

import { chaptersOf, mangaIds, resetChapters } from '../lib/chapters.ts';
import { chooseSetting, expectPage, openChapter, openManga, readerAfter, slidTap, stats, swipe, tap, waitForLibrary }
    from '../lib/reader.ts';
import { driverName, scenario } from '../lib/scenario.ts';
import { LIBRARY_MORE, READER } from '../lib/screens.ts';
import { appServerUrl, optionsFromEnv, SuwayomiClient } from '../lib/suwayomi.ts';

const MiB = 1024 * 1024;

scenario('reader-paged', async (s) => {
    const url = appServerUrl(s.driver.name);
    const client = new SuwayomiClient(optionsFromEnv());
    const alpha = (await mangaIds(client)).get('Alpha') ?? 0;
    expect(alpha, 'the test server has Alpha: tools/test-server/test-server.sh reset').toBeTruthy();
    const chapters = await resetChapters(client, alpha);
    const ch = (n: number) => chapters[n - 1].id;

    try {
        await s.launch({ testConfig: { server: { url } } });
        await waitForLibrary(s);
        await openManga(s, LIBRARY_MORE.reading, 0, 2);

        // Chapter 1 (the third row, newest first) opens at its first page, fit to the screen, right to left (the default).
        let shown = await openChapter(s, 2, { shown: 'page' });
        expect(shown).toMatchObject({ chapterId: ch(1), chapter: 1, page: 0, of: 10, direction: 'rtl', fit: 'screen',
            spread: false, w: 480, h: 720, menu: false, pageNumber: true, read: false });
        await expectPage(s, 1, 1, 1);
        await s.expectGolden('page');

        // The menu, then reading direction left to right (settings entry 0, choice 0 of 2).
        await readerAfter(s, tap(s, READER.middle), { menu: true });
        await s.expectGolden('menu');
        await chooseSetting(s, 0, 0, 2, { direction: 'ltr', menu: true });
        await readerAfter(s, tap(s, READER.middle), { menu: false });

        // Pages 2..10: three by the D-pad, three by taps on the right edge (one that slides a little, like a quick finger),
        // three by swipes to the left.
        const turns = [
            ...Array.from({ length: 3 }, () => () => s.driver.press('RIGHT')),
            ...Array.from({ length: 2 }, () => tap(s, READER.right)),
            slidTap(s, READER.right),
            ...Array.from({ length: 3 }, () => swipe(s, READER.swipeToLeft)),
        ];
        const beforeTurns = s.events.mark();
        for (let page = 1; page <= 9; page++) {
            shown = await readerAfter(s, turns[page - 1], { shown: 'page', page });
            expect(shown.chapterId).toBe(ch(1));
            await expectPage(s, 1, 1, page + 1);
        }
        expect(shown.read, 'the last page marks the chapter read').toBe(true);
        const saved = await s.events.waitForEvent('progress', { chapterId: ch(1), read: true }, { from: beforeTurns });
        expect(saved).toMatchObject({ page: 9, ok: true });
        expect((await chaptersOf(client, alpha))[0]).toMatchObject({ isRead: true, lastPageRead: 9 });

        // Past the last page: the "next chapter" screen, then chapter 2 at once (fetched ahead near the end of chapter 1).
        shown = await readerAfter(s, () => s.driver.press('RIGHT'), { shown: 'end' });
        expect(shown).toMatchObject({ chapterId: ch(1), page: 10, of: 10 });
        await s.expectGolden('end');
        shown = await readerAfter(s, () => s.driver.press('RIGHT'), { shown: 'page', chapterId: ch(2) });
        expect(shown).toMatchObject({ page: 0, of: 10, ready: true });
        await expectPage(s, 1, 2, 1);

        // Back: the "previous chapter" screen, then chapter 1 at its last page.
        await readerAfter(s, () => s.driver.press('LEFT'), { shown: 'start', chapterId: ch(2) });
        shown = await readerAfter(s, () => s.driver.press('LEFT'), { shown: 'page', chapterId: ch(1) });
        expect(shown.page).toBe(9);
        await expectPage(s, 1, 1, 10);

        // Fit width (settings entry 1, choice 1 of 3): as wide as the screen, from its top (the colour code is there).
        const beforeFit = s.events.mark();
        shown = await chooseSetting(s, 1, 1, 3, { fit: 'width', shown: 'page' });
        expect(shown).toMatchObject({ w: 1280, h: 1920, page: 9 });
        await readerAfter(s, tap(s, READER.middle), { menu: false });
        await expectPage(s, 1, 1, 10);
        // config.json is saved 2 s after the change, on the main thread: a short key press during that frame can be missed.
        await s.events.waitForEvent('config_saved', {}, { from: beforeFit });

        // 100 page turns back and forth through chapter 1: memory stays flat, turns of prefetched pages are quick.
        const memory: number[] = [];
        const quick: number[] = [];
        let page = 9;
        let direction = -1;
        for (let i = 0; i < 100; i++) {
            if (page === 0) direction = 1;
            if (page === 9) direction = -1;
            page += direction;
            const key = direction > 0 ? 'RIGHT' : 'LEFT';
            shown = await readerAfter(s, () => s.driver.press(key), { shown: 'page', page, chapterId: ch(1) });
            memory.push(Number(shown.memUsed));
            if (shown.ready) quick.push(Number(shown.turnMs));
        }
        const growth = Math.max(...memory.slice(20)) - memory[20];
        const times = stats(quick);
        console.log(`[reader-paged] ${quick.length} prefetched turns: median ${times.median} ms, p90 ${times.p90} ms, ` +
            `max ${times.max} ms; memory after turn 20: ${(memory[20] / MiB).toFixed(1)} MiB, growth ` +
            `${(growth / MiB).toFixed(1)} MiB`);
        expect(growth, 'memory growth over turns 20..100').toBeLessThan(16 * MiB);
        expect(quick.length, 'turns to a page that was ready').toBeGreaterThan(90);
        // D17 / WP-11 acceptance: < 150 ms on the console at stock clocks. The desktop (software rendering) only gets a
        // loose bound.
        expect(times.p90, 'page turn time (90th percentile)').toBeLessThan(s.driver.name === 'switch' ? 150 : 1000);

        // B closes the reader; the details screen loads its chapters again (chapter 1 read: 2 unread left).
        const mark = s.events.mark();
        await s.driver.press('B');
        await s.events.waitForEvent('screen', { name: 'Manga' }, { from: mark });
        await s.events.waitForEvent('manga', { state: 'ready', complete: true, unread: 2 }, { from: mark });
        await s.events.waitForEvent('idle', {}, { from: mark });
        s.expectNoErrorEvents();
        await s.driver.quit();

        // A restart keeps Alpha's own direction and fit (config.json). The library comes back on the Reading tab (WP-09).
        await s.launch({ testConfig: { server: { url } }, keepConfig: true });
        await waitForLibrary(s, { items: 2 });
        await openManga(s, null, 0);
        shown = await openChapter(s, 2, { shown: 'page' });
        expect(shown).toMatchObject({ chapterId: ch(1), page: 0, direction: 'ltr', fit: 'width' });
        s.expectNoErrorEvents();
        await s.driver.quit();
    } finally {
        await resetChapters(client, alpha);
    }
}, { timeoutMs: driverName() === 'switch' ? 15 * 60_000 : 4 * 60_000 });
