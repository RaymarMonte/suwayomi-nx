// touch-pass (WP-12, D16 "touch first"): every MVP screen used by touch alone, no button pressed until the final quit. On the
// Switch it needs handheld mode (the touch screen is off when docked).
// - Main screen: the Settings tab and back to the Library tab.
// - Library: the Reading category, Refresh, a cover.
// - Manga details (Alpha): the description opens, Sort -> oldest first, Continue opens the reader.
// - Reader (right to left, the default): the left edge and a swipe to the right go forward, the right edge back; a tap in
//   the middle opens the menu, its Close button closes the reader.
// - Back by the footer's "Back" hint: manga details -> library; the setup screen (opened from Settings' server address) ->
//   Settings.
// No "error" events. Needs the test server.

import { expect } from 'vitest';

import { mangaIds, resetChapters } from '../lib/chapters.ts';
import { expectPage, readerAfter, swipe, tap, waitForLibrary } from '../lib/reader.ts';
import { scenario, type Scenario } from '../lib/scenario.ts';
import { FOOTER, LIBRARY, LIBRARY_MORE, MANGA, READER, settingsRow, tab } from '../lib/screens.ts';
import { appServerUrl, optionsFromEnv, SuwayomiClient } from '../lib/suwayomi.ts';

/** Taps `at`, then waits for the event `ev` with `fields` and for the app to settle. */
async function touchThen(s: Scenario, at: { x: number; y: number }, ev: string, fields: Record<string, unknown>): Promise<void> {
    const mark = s.events.mark();
    await s.driver.touch(at.x, at.y);
    await s.events.waitForEvent(ev, fields, { from: mark, timeoutMs: 30_000 });
    await s.events.waitForEvent('idle', {}, { from: mark });
}

scenario('touch-pass', async (s) => {
    const url = appServerUrl(s.driver.name);
    const client = new SuwayomiClient(optionsFromEnv());
    const alpha = (await mangaIds(client)).get('Alpha') ?? 0;
    expect(alpha, 'the test server has Alpha: tools/test-server/test-server.sh reset').toBeTruthy();
    await resetChapters(client, alpha);

    try {
        await s.launch({ testConfig: { server: { url } } });
        await waitForLibrary(s);

        // Main screen tabs.
        await touchThen(s, tab('Settings'), 'screen', { name: 'Settings' });
        await touchThen(s, tab('Library'), 'screen', { name: 'Library' });

        // Library.
        await touchThen(s, LIBRARY_MORE.reading, 'library', { state: 'ready', items: 2 });
        await touchThen(s, LIBRARY.refresh, 'library', { state: 'ready', items: 2 });
        await touchThen(s, LIBRARY.cover(0), 'manga', { state: 'ready', complete: true, id: alpha });

        // Manga details.
        await touchThen(s, MANGA.description, 'manga', { description: 'open' });
        await touchThen(s, MANGA.description, 'manga', { description: 'closed' });
        const mark = s.events.mark();
        await s.driver.touch(MANGA.sort.x, MANGA.sort.y);
        await s.events.waitForEvent('idle', {}, { from: mark });
        await touchThen(s, MANGA.menuEntry(1, 2), 'manga', { sort: 'oldest', first: 1 });

        // Reader: Alpha ch1 from Continue ("Start"), right to left.
        await readerAfter(s, tap(s, MANGA.continue), { shown: 'page', page: 0 });
        await readerAfter(s, tap(s, READER.left), { shown: 'page', page: 1 });
        await readerAfter(s, swipe(s, READER.swipeToRight), { shown: 'page', page: 2 });
        await readerAfter(s, tap(s, READER.right), { shown: 'page', page: 1 });
        await expectPage(s, 1, 1, 2);
        await readerAfter(s, tap(s, READER.middle), { menu: true });
        await touchThen(s, READER.close, 'screen', { name: 'Manga' });

        // Back by the footer hint: details -> library.
        await touchThen(s, FOOTER.back, 'screen', { name: 'Library' });

        // Settings -> the setup screen (server address) -> back by the footer hint.
        await touchThen(s, tab('Settings'), 'screen', { name: 'Settings' });
        await touchThen(s, settingsRow('address', false), 'screen', { name: 'Setup' });
        await touchThen(s, FOOTER.back, 'screen', { name: 'Settings' });

        s.expectNoErrorEvents();
        await s.driver.quit();
    } finally {
        await resetChapters(client, alpha);
    }
});
