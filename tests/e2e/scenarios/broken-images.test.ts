// broken-images (WP-11, TESTING.md): the Broken fixture (Default category; 1 chapter: page 1 a corrupt JPEG, page 2 zero
// bytes, page 3 a 1 x 1 image, pages 4-5 fine). Checks, on desktop and on the Switch:
// - page 1 shows an error with Retry (golden "broken") and an "error" event; page 2 too; page 3 (1 x 1) and pages 4-5 show;
// - broken pages never block page turns; the last page still marks the chapter read;
// - Retry (a tap, then X) tries again and shows the error again; no crash, B goes back.
// Needs the test server. The harness resets Broken's chapter before and after.

import { expect } from 'vitest';

import { mangaIds, resetChapters } from '../lib/chapters.ts';
import { expectPage, openChapter, openManga, readerAfter, tap, waitForLibrary } from '../lib/reader.ts';
import { scenario } from '../lib/scenario.ts';
import { READER } from '../lib/screens.ts';
import { appServerUrl, optionsFromEnv, SuwayomiClient } from '../lib/suwayomi.ts';

scenario('broken-images', async (s) => {
    const url = appServerUrl(s.driver.name);
    const client = new SuwayomiClient(optionsFromEnv());
    const broken = (await mangaIds(client)).get('Broken') ?? 0;
    expect(broken, 'the test server has Broken: tools/test-server/test-server.sh reset').toBeTruthy();
    await resetChapters(client, broken);

    try {
        await s.launch({ testConfig: { server: { url } } });
        // The Default category (the first tab): Broken, then 日本語タイトル テスト.
        await waitForLibrary(s, { items: 2 });
        await openManga(s, null, 0);

        let shown = await openChapter(s, 0, { shown: 'page_error' });
        expect(shown).toMatchObject({ page: 0, of: 5, error: 'corrupt', retries: 0 });
        await s.expectGolden('broken');
        expect(s.events.errors().at(-1)).toMatchObject({ where: 'reader', kind: 'corrupt' });

        // Right to left (the default): LEFT is the next page.
        shown = await readerAfter(s, () => s.driver.press('LEFT'), { page: 1 });
        expect(shown).toMatchObject({ shown: 'page_error', error: 'corrupt' });
        shown = await readerAfter(s, () => s.driver.press('LEFT'), { page: 2 });
        expect(shown).toMatchObject({ shown: 'page', srcW: 1, srcH: 1 });
        shown = await readerAfter(s, () => s.driver.press('LEFT'), { page: 3 });
        expect(shown.shown).toBe('page');
        await expectPage(s, 6, 1, 4);
        shown = await readerAfter(s, () => s.driver.press('LEFT'), { page: 4 });
        expect(shown).toMatchObject({ shown: 'page', read: true });
        await expectPage(s, 6, 1, 5);

        // Back to page 1 and Retry: by touch, then with X. It fails again, and says so again.
        for (let page = 3; page >= 0; page--) {
            await readerAfter(s, () => s.driver.press('RIGHT'), { page });
        }
        shown = await readerAfter(s, tap(s, READER.retry), { page: 0, retries: 1 });
        expect(shown).toMatchObject({ shown: 'page_error', error: 'corrupt' });
        shown = await readerAfter(s, () => s.driver.press('X'), { page: 0, retries: 2 });
        expect(shown).toMatchObject({ shown: 'page_error', error: 'corrupt' });

        const mark = s.events.mark();
        await s.driver.press('B');
        await s.events.waitForEvent('screen', { name: 'Manga' }, { from: mark });
        await s.events.waitForEvent('idle', {}, { from: mark });
        expect(s.events.errors().every((e) => e.where === 'reader'), 'only the reader reports errors').toBe(true);
        await s.driver.quit();
    } finally {
        await resetChapters(client, broken);
    }
});
