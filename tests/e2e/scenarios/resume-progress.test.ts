// resume-progress (WP-11, TESTING.md): reading progress round-trips through the server. On Alpha (Reading), on desktop and
// on the Switch:
// - chapter 3 opens at its first page; four pages forward (right to left, the default: LEFT); B closes the reader;
// - the server has lastPageRead 4 (GraphQL from the harness), and the details screen's Continue points at it;
// - Continue opens chapter 3 at page 5 (index 4; the colour code confirms it);
// - after a restart, Continue still opens that page.
// Needs the test server. The harness resets Alpha's chapters before and after.

import { expect } from 'vitest';

import { chaptersOf, mangaIds, resetChapters } from '../lib/chapters.ts';
import type { AppEvent } from '../lib/events.ts';
import { expectPage, openChapter, openManga, readerAfter, waitForLibrary } from '../lib/reader.ts';
import { scenario, type Scenario } from '../lib/scenario.ts';
import { LIBRARY_MORE, MANGA } from '../lib/screens.ts';
import { appServerUrl, optionsFromEnv, SuwayomiClient } from '../lib/suwayomi.ts';

/** Continue on the details screen: waits for the reader at the page it opens. */
async function continueReading(s: Scenario, fields: Record<string, unknown>): Promise<AppEvent> {
    return readerAfter(s, () => s.driver.touch(MANGA.continue.x, MANGA.continue.y), fields);
}

function continuesAt(chapter: number, page: number): (e: AppEvent) => boolean {
    return (e) => {
        const point = e.continue as { kind?: string; chapter?: number; page?: number } | undefined;
        return e.ev === 'manga' && e.complete === true && point?.kind === 'continue' && point.chapter === chapter &&
            point.page === page;
    };
}

scenario('resume-progress', async (s) => {
    const url = appServerUrl(s.driver.name);
    const client = new SuwayomiClient(optionsFromEnv());
    const alpha = (await mangaIds(client)).get('Alpha') ?? 0;
    expect(alpha, 'the test server has Alpha: tools/test-server/test-server.sh reset').toBeTruthy();
    const chapters = await resetChapters(client, alpha);
    const third = chapters[2].id;

    try {
        await s.launch({ testConfig: { server: { url } } });
        await waitForLibrary(s);
        await openManga(s, LIBRARY_MORE.reading, 0, 2);

        // Chapter 3 (the first row), four pages forward.
        await openChapter(s, 0, { shown: 'page', chapterId: third, page: 0 });
        for (let page = 1; page <= 4; page++) {
            await readerAfter(s, () => s.driver.press('LEFT'), { shown: 'page', page });
        }
        await expectPage(s, 1, 3, 5);

        // B: the progress goes to the server, then the details screen loads its chapters again.
        let mark = s.events.mark();
        await s.driver.press('B');
        const saved = await s.events.waitForEvent('progress', { chapterId: third, page: 4 }, { from: mark });
        expect(saved.ok).toBe(true);
        await s.events.waitFor('Continue at chapter 3, page index 4', continuesAt(third, 4), { from: mark });
        await s.events.waitForEvent('idle', {}, { from: mark });
        expect((await chaptersOf(client, alpha))[2]).toMatchObject({ isRead: false, lastPageRead: 4 });

        // Continue opens that page.
        let shown = await continueReading(s, { shown: 'page', chapterId: third });
        expect(shown.page).toBe(4);
        await expectPage(s, 1, 3, 5);
        mark = s.events.mark();
        await s.driver.press('B');
        await s.events.waitForEvent('screen', { name: 'Manga' }, { from: mark });
        await s.events.waitForEvent('idle', {}, { from: mark });
        s.expectNoErrorEvents();
        await s.driver.quit();

        // After a restart too.
        await s.launch({ testConfig: { server: { url } } });
        await waitForLibrary(s);
        const details = await openManga(s, LIBRARY_MORE.reading, 0, 2);
        expect(continuesAt(third, 4)(details), 'Continue after a restart').toBe(true);
        shown = await continueReading(s, { shown: 'page', chapterId: third });
        expect(shown.page).toBe(4);
        await expectPage(s, 1, 3, 5);
        s.expectNoErrorEvents();
        await s.driver.quit();
    } finally {
        await resetChapters(client, alpha);
    }
});
