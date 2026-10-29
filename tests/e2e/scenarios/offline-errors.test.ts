// offline-errors (TESTING.md scenario 8, WP-12): the test server stops and starts again while the app runs. Every screen shows
// the error with a Retry, nothing hangs or crashes, and once the server is back a Retry (or simply reading on) works:
// 1. start-up with the server stopped: the Library tab shows the error + Retry; the server starts; Retry (a tap) connects
//    and the library shows.
// 2. the reader: Alpha ch1 opens with the server running; the server stops; the prefetched pages 2-4 still show, page 5
//    shows an error (Retry by touch and by X fail again, `retries`); the server starts; X shows page 5 (colour code), and
//    page 6, which failed as a prefetch while the server was away, shows when it is turned to without another Retry.
// 3. the details screen: B back; the server stops; Refresh fails (`chapter_action` ok false), the list stays.
// 4. a manga opened while the server is away (Beta RTL): its error + Retry; the server starts; Retry shows it.
// 5. the library: the server stops; Refresh fails: the error + Retry; the server starts; Retry shows the library again.
// Every "error" event must be one of these, of kind offline (connection refused) or timeout (a firewall that drops instead).
// The test config turns the SD-card image cache off ("diskCache": false), so pages cached by other scenarios can't stand in
// for the server. Needs the test server; it starts it if it isn't running and leaves it running (auth none).

import { writeFileSync } from 'node:fs';
import { join } from 'node:path';

import { expect } from 'vitest';

import { mangaIds, resetChapters } from '../lib/chapters.ts';
import type { AppEvent } from '../lib/events.ts';
import { expectPage, openChapter, readerAfter, tap, waitForLibrary } from '../lib/reader.ts';
import { scenario, type Scenario } from '../lib/scenario.ts';
import { LIBRARY, LIBRARY_MORE, MANGA, READER, STATUS } from '../lib/screens.ts';
import { appServerUrl, optionsFromEnv, startServer, stopServer, SuwayomiClient } from '../lib/suwayomi.ts';

// The server is away: "offline" / "timeout" from the api, "network" / "timeout" from the image loader (a page).
const OFFLINE_KINDS = ['offline', 'network', 'timeout'];
// A request to a stopped server can take a while to fail on the console (a firewall that drops: 5 s per try, 3 tries).
const SLOW = { timeoutMs: 90_000 };

/** Saves a screenshot to out/<scenario>/<name>.<ext>, to look at when something fails (cheap: no golden). */
async function snapshot(s: Scenario, name: string): Promise<void> {
    const shot = await s.driver.screenshot();
    writeFileSync(join(s.out, `${name}.${s.driver.name === 'desktop' ? 'png' : 'jpg'}`), shot);
}

/** Waits for the "error" event of screen `where` after `from`, and checks it says the server is away. */
async function serverAwayError(s: Scenario, where: string, from: number): Promise<AppEvent> {
    const error = await s.events.waitForEvent('error', { where }, { from, ...SLOW });
    expect(OFFLINE_KINDS, `error kind on ${where}: ${JSON.stringify(error)}`).toContain(error.kind);
    await s.events.waitForEvent('idle', {}, { from: s.events.events.indexOf(error) });
    return error;
}

scenario(
    'offline-errors',
    async (s) => {
        const url = appServerUrl(s.driver.name);
        const config = { server: { url }, diskCache: false };
        startServer();
        const client = new SuwayomiClient(optionsFromEnv());
        const ids = await mangaIds(client);
        const alpha = ids.get('Alpha') ?? 0;
        expect(alpha, 'the test server has Alpha: tools/test-server/test-server.sh reset').toBeTruthy();
        const first = (await resetChapters(client, alpha))[0].id;

        try {
            // 1. Start-up with the server stopped.
            stopServer();
            await s.launch({ testConfig: config });
            const check = await s.events.waitForEvent('server_check', { where: 'main' }, SLOW);
            expect(check.ok).toBe(false);
            expect(OFFLINE_KINDS).toContain(check.kind);
            await serverAwayError(s, 'Library', 0);
            await snapshot(s, '1-start-offline');
            startServer();
            let mark = s.events.mark();
            await s.driver.touch(STATUS.library.x, STATUS.library.y);
            const back = await s.events.waitForEvent('server_check', { where: 'main' }, { from: mark, ...SLOW });
            expect(back.ok, JSON.stringify(back)).toBe(true);
            await waitForLibrary(s);

            // 2. The reader: Alpha ch1 (the third row, newest first), read right to left (the default: LEFT is forward).
            mark = s.events.mark();
            await s.driver.touch(LIBRARY_MORE.reading.x, LIBRARY_MORE.reading.y);
            await s.events.waitForEvent('library', { state: 'ready', items: 2 }, { from: mark });
            await s.events.waitForEvent('idle', {}, { from: mark });
            mark = s.events.mark();
            await s.driver.touch(LIBRARY.cover(0).x, LIBRARY.cover(0).y);
            await s.events.waitForEvent('manga', { state: 'ready', complete: true }, { from: mark, timeoutMs: 30_000 });
            await s.events.waitForEvent('idle', {}, { from: mark });
            await openChapter(s, 2, { shown: 'page', chapterId: first, page: 0 });

            stopServer();
            for (let page = 1; page <= 3; page++) {
                await readerAfter(s, () => s.driver.press('LEFT'), { shown: 'page', page });  // prefetched: still there
            }
            mark = s.events.mark();
            await s.driver.press('LEFT');
            const failed = await s.events.waitForEvent('reader', { shown: 'page_error', page: 4 }, { from: mark, ...SLOW });
            expect(OFFLINE_KINDS).toContain(failed.error);
            await serverAwayError(s, 'reader', mark);
            await snapshot(s, '2-reader-offline');
            const retried = await readerAfter(s, tap(s, READER.retry), { shown: 'page_error', page: 4, retries: 1 });
            expect(OFFLINE_KINDS).toContain(retried.error);
            await readerAfter(s, () => s.driver.press('X'), { shown: 'page_error', page: 4, retries: 2 });

            startServer();
            await readerAfter(s, () => s.driver.press('X'), { shown: 'page', page: 4 });
            await expectPage(s, 1, 1, 5);
            const turned = s.events.mark();
            await readerAfter(s, () => s.driver.press('LEFT'), { shown: 'page', page: 5 });
            await expectPage(s, 1, 1, 6);
            mark = s.events.mark();
            await s.driver.press('B');
            await s.events.waitForEvent('screen', { name: 'Manga' }, { from: mark });
            // Saved when B closed the reader, or already by the 5 s save before it.
            const saved = await s.events.waitForEvent('progress', { chapterId: first, page: 5 }, { from: turned, ...SLOW });
            expect(saved.ok, 'the progress saved once the server was back').toBe(true);
            await s.events.waitForEvent('idle', {}, { from: mark });

            // 3. The details screen: Refresh with the server stopped fails, the list stays.
            stopServer();
            mark = s.events.mark();
            await s.driver.touch(MANGA.refresh.x, MANGA.refresh.y);
            const refresh = await s.events.waitForEvent('chapter_action', { action: 'refresh' }, { from: mark, ...SLOW });
            expect(refresh.ok).toBe(false);
            await s.events.waitForEvent('idle', {}, { from: s.events.events.indexOf(refresh) });
            await snapshot(s, '3-manga-refresh-offline');
            mark = s.events.mark();
            await s.driver.press('B');
            await s.events.waitForEvent('screen', { name: 'Library' }, { from: mark });
            await s.events.waitForEvent('idle', {}, { from: mark });

            // 4. A manga opened while the server is away (Beta RTL, the second cover in Reading).
            mark = s.events.mark();
            await s.driver.touch(LIBRARY.cover(1).x, LIBRARY.cover(1).y);
            await serverAwayError(s, 'Manga', mark);
            await snapshot(s, '4-manga-offline');
            startServer();
            mark = s.events.mark();
            await s.driver.touch(STATUS.manga.x, STATUS.manga.y);
            const beta = await s.events.waitForEvent('manga', { state: 'ready', complete: true }, { from: mark, ...SLOW });
            expect(beta.id).toBe(ids.get('Beta RTL'));
            await s.events.waitForEvent('idle', {}, { from: mark });
            mark = s.events.mark();
            await s.driver.press('B');
            await s.events.waitForEvent('screen', { name: 'Library' }, { from: mark });
            await s.events.waitForEvent('idle', {}, { from: mark });

            // 5. The library: Refresh with the server stopped, then Retry once it is back.
            stopServer();
            mark = s.events.mark();
            await s.driver.touch(LIBRARY.refresh.x, LIBRARY.refresh.y);
            await s.events.waitForEvent('library', { state: 'failed' }, { from: mark, ...SLOW });
            await serverAwayError(s, 'Library', mark);
            await snapshot(s, '5-library-offline');
            startServer();
            mark = s.events.mark();
            await s.driver.touch(STATUS.library.x, STATUS.library.y);
            await s.events.waitForEvent('library', { state: 'ready', items: 2 }, { from: mark, ...SLOW });
            await s.events.waitForEvent('idle', {}, { from: mark });

            // Only the errors this scenario caused.
            for (const error of s.events.errors()) {
                expect(['Library', 'reader', 'Manga'], JSON.stringify(error)).toContain(error.where);
                expect(OFFLINE_KINDS, JSON.stringify(error)).toContain(error.kind);
            }
            await s.driver.quit();
        } finally {
            startServer();
            await resetChapters(client, alpha);
        }
    },
    { timeoutMs: 15 * 60_000 },
);
