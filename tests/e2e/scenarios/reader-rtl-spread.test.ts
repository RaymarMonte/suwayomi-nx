// reader-rtl-spread (WP-11, TESTING.md): right to left on Beta RTL (Reading, 2 chapters x 8 PNG pages; chapter 1's page 4 is
// a double-page spread of 2000 x 1500). Checks, on desktop and on the Switch, with the default direction (right to left):
// - LEFT on the D-pad, a tap on the left edge and a swipe to the right all go to the next page; RIGHT and a tap on the right
//   edge go back (everything mirrored);
// - the spread is reported as one (spread: true), shown whole at 960 x 720 (golden "spread"), its colour code readable;
// - past the last page the "next chapter" screen.
// Needs the test server. The harness resets Beta RTL's chapters before and after.

import { expect } from 'vitest';

import { mangaIds, resetChapters } from '../lib/chapters.ts';
import { expectPage, openChapter, openManga, readerAfter, swipe, tap, waitForLibrary } from '../lib/reader.ts';
import { scenario } from '../lib/scenario.ts';
import { LIBRARY_MORE, READER } from '../lib/screens.ts';
import { appServerUrl, optionsFromEnv, SuwayomiClient } from '../lib/suwayomi.ts';

scenario('reader-rtl-spread', async (s) => {
    const url = appServerUrl(s.driver.name);
    const client = new SuwayomiClient(optionsFromEnv());
    const beta = (await mangaIds(client)).get('Beta RTL') ?? 0;
    expect(beta, 'the test server has Beta RTL: tools/test-server/test-server.sh reset').toBeTruthy();
    const chapters = await resetChapters(client, beta);

    try {
        await s.launch({ testConfig: { server: { url } } });
        await waitForLibrary(s);
        await openManga(s, LIBRARY_MORE.reading, 1, 2);  // "Alpha" < "Beta RTL"

        // Chapter 1 is the second row (newest first).
        let shown = await openChapter(s, 1, { shown: 'page' });
        expect(shown).toMatchObject({ chapterId: chapters[0].id, page: 0, of: 8, direction: 'rtl', spread: false });
        await expectPage(s, 2, 1, 1);

        // Forward, right to left: the D-pad's LEFT, a tap on the left edge, a swipe to the right (to page 4, the spread).
        shown = await readerAfter(s, () => s.driver.press('LEFT'), { shown: 'page', page: 1 });
        await expectPage(s, 2, 1, 2);
        shown = await readerAfter(s, tap(s, READER.left), { shown: 'page', page: 2 });
        await expectPage(s, 2, 1, 3);
        shown = await readerAfter(s, swipe(s, READER.swipeToRight), { shown: 'page', page: 3 });
        expect(shown).toMatchObject({ spread: true, srcW: 2000, srcH: 1500, w: 960, h: 720 });
        await expectPage(s, 2, 1, 4);
        await s.expectGolden('spread');

        // Back: the D-pad's RIGHT and a tap on the right edge.
        await readerAfter(s, () => s.driver.press('RIGHT'), { shown: 'page', page: 2 });
        await readerAfter(s, tap(s, READER.right), { shown: 'page', page: 1 });
        await expectPage(s, 2, 1, 2);

        // To the end: pages 3..8, then the "next chapter" screen.
        for (let page = 2; page <= 7; page++) {
            await readerAfter(s, () => s.driver.press('LEFT'), { shown: 'page', page });
        }
        await expectPage(s, 2, 1, 8);
        shown = await readerAfter(s, () => s.driver.press('LEFT'), { shown: 'end' });
        expect(shown).toMatchObject({ of: 8, read: true });

        const mark = s.events.mark();
        await s.driver.press('B');
        await s.events.waitForEvent('screen', { name: 'Manga' }, { from: mark });
        await s.events.waitForEvent('idle', {}, { from: mark });
        s.expectNoErrorEvents();
        await s.driver.quit();
    } finally {
        await resetChapters(client, beta);
    }
});
