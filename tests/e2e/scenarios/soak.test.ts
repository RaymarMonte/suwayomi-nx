// soak (TESTING.md scenario 10, WP-12): one long session, then the numbers WP-12 tunes the runtime profile from.
// - Library: the Big shelf, flicked to the end and back (pass 1); then Delta Big (Long series) opens.
// - Reader: Start opens Delta Big at chapter 1; SNX_SOAK_TURNS (default 500) turns forward with LEFT (right to left, the
//   default) through its 2-page chapters: page 1, page 2, the "next chapter" screen, the next chapter, ... (~170 chapters).
// - B, B back to the library: the Big shelf flicked to the end and back again (pass 2).
// Asserts: no "error" and no "mem_pressure" events; every "mem" sample stays within the profile's budgets above the first
// one (image + cover + texture caches, plus 64 MiB for everything else); the app quits cleanly and (Switch) leaves no
// crash report or fatal file (the driver checks).
// Writes out/soak/summary.json: memory (first sample, after 100 turns, peak, last), turn times (median, p90, max) and the
// chapters read, for tuning makeRuntimeProfile (DECISIONS.md). TESTING.md calls it a device scenario; on desktop it runs the
// same, but only the console's numbers count for tuning.
// Needs the test server. Resets Delta Big's chapters before and after.

import { writeFileSync } from 'node:fs';
import { join } from 'node:path';

import { expect } from 'vitest';

import { mangaIds, resetChapters } from '../lib/chapters.ts';
import { EventTimeoutError, type AppEvent } from '../lib/events.ts';
import { flickUntil, inputInLoop } from '../lib/library.ts';
import { readerAfter, stats, tap, waitForLibrary } from '../lib/reader.ts';
import { scenario, type Scenario } from '../lib/scenario.ts';
import { LIBRARY, LIBRARY_MORE, MANGA } from '../lib/screens.ts';
import { appServerUrl, optionsFromEnv, SuwayomiClient } from '../lib/suwayomi.ts';

const MiB = 1024 * 1024;
const TURNS = Number(process.env.SNX_SOAK_TURNS ?? 500);
const OTHER_MEMORY = 64 * MiB; // everything that isn't a cache: fonts, the UI, curl, threads

/** Taps a library category tab and waits until it shows (`items` manga, if given) and the app has settled. */
async function openCategory(s: Scenario, at: { x: number; y: number }, items?: number): Promise<void> {
    const mark = s.events.mark();
    await s.driver.touch(at.x, at.y);
    await s.events.waitForEvent('library', items === undefined ? { state: 'ready' } : { state: 'ready', items }, { from: mark, timeoutMs: 30_000 });
    await s.events.waitForEvent('idle', {}, { from: mark });
}

/** The Big shelf: flicked to its end (150 manga, all pages loaded) and back to the top. The second time it is loaded already. */
async function shelfPass(s: Scenario): Promise<void> {
    await openCategory(s, LIBRARY.categories.bigShelf);
    await flickUntil(s, 'down', (g) => Number(g.last) === 150, 'the end of the Big shelf');
    await flickUntil(s, 'up', (g) => Number(g.first) === 0, 'the top of the Big shelf');
}

/**
 * One turn forward: LEFT, then the "reader" event of the new position (another chapter or page than `from`) and idle. A
 * press that sys-autopilot may have lost (inputInLoop) is pressed again only if the reader didn't move.
 */
async function turnForward(s: Scenario, from: AppEvent): Promise<AppEvent> {
    const mark = s.events.mark();
    const moved = (e: AppEvent) => e.ev === 'reader' && (e.chapterId !== from.chapterId || e.page !== from.page);
    for (let tries = 0; tries < 3; tries++) {
        const pressed = await inputInLoop(s, () => s.driver.press('LEFT'));
        try {
            const shown = await s.events.waitFor('the reader at its next position', moved, { from: mark, timeoutMs: pressed ? 30_000 : 3_000 });
            await s.events.waitForEvent('idle', {}, { from: s.events.events.indexOf(shown) });
            return shown;
        } catch (e) {
            if (pressed || !(e instanceof EventTimeoutError)) throw e;
        }
    }
    throw new Error('three presses of LEFT were lost');
}

function memUsed(e: AppEvent): number {
    return Number(e.used);
}

scenario(
    'soak',
    async (s) => {
        const url = appServerUrl(s.driver.name);
        const client = new SuwayomiClient(optionsFromEnv());
        const delta = (await mangaIds(client)).get('Delta Big') ?? 0;
        expect(delta, 'the test server has Delta Big: tools/test-server/test-server.sh reset').toBeTruthy();
        await resetChapters(client, delta);

        try {
            const start = await s.launch({ testConfig: { server: { url } } });
            await waitForLibrary(s);
            await shelfPass(s);

            // Delta Big: the first cover of Long series; Start opens chapter 1.
            await openCategory(s, LIBRARY_MORE.longSeries, 2);
            let mark = s.events.mark();
            await s.driver.touch(LIBRARY.cover(0).x, LIBRARY.cover(0).y);
            const details = await s.events.waitForEvent('manga', { state: 'ready', complete: true }, { from: mark, timeoutMs: 60_000 });
            expect(details.id).toBe(delta);
            await s.events.waitForEvent('idle', {}, { from: mark });
            let at = await readerAfter(s, tap(s, MANGA.continue), { shown: 'page', page: 0 });

            const chapters = new Set<number>([Number(at.chapterId)]);
            const turnTimes: number[] = [];
            let memAfterWarmUp: number | undefined;
            for (let turn = 1; turn <= TURNS; turn++) {
                at = await turnForward(s, at);
                chapters.add(Number(at.chapterId));
                if (typeof at.turnMs === 'number' && at.ready === true) turnTimes.push(at.turnMs);
                if (turn === Math.min(100, TURNS)) memAfterWarmUp = s.events.events.filter((e) => e.ev === 'mem').map(memUsed).at(-1);
                if (turn % 50 === 0) console.log(`[soak] ${turn} turns, chapter ${at.chapter}, page ${at.page}`);
            }

            mark = s.events.mark();
            await s.driver.press('B');
            await s.events.waitForEvent('screen', { name: 'Manga' }, { from: mark });
            await s.events.waitForEvent('idle', {}, { from: mark, timeoutMs: 60_000 });
            mark = s.events.mark();
            await s.driver.press('B');
            await s.events.waitForEvent('screen', { name: 'Library' }, { from: mark });
            await s.events.waitForEvent('idle', {}, { from: mark });
            await shelfPass(s);

            // The numbers.
            const mem = s.events.events.filter((e) => e.ev === 'mem').map(memUsed);
            const budgets = Number(start.imageBudget) + Number(start.coverBudget) + Number(start.textureBudget);
            const summary = {
                driver: s.driver.name,
                turns: TURNS,
                chapters: chapters.size,
                turnMs: stats(turnTimes),
                turnsTimed: turnTimes.length,
                memMiB: {
                    total: Number(start.memTotal) / MiB,
                    first: (mem[0] ?? 0) / MiB,
                    afterWarmUp: (memAfterWarmUp ?? 0) / MiB,
                    peak: Math.max(...mem) / MiB,
                    last: (mem.at(-1) ?? 0) / MiB,
                    budgets: budgets / MiB,
                },
                profile: {
                    imageBudget: start.imageBudget, coverBudget: start.coverBudget, textureBudget: start.textureBudget,
                    maxDecode: start.maxDecode, httpConcurrency: start.httpConcurrency, decodeThreads: start.decodeThreads,
                },
            };
            writeFileSync(join(s.out, 'summary.json'), JSON.stringify(summary, null, 2));
            console.log(`[soak] ${JSON.stringify(summary)}`);

            expect(chapters.size, 'chapters read').toBeGreaterThan(Math.floor(TURNS / 3) - 2);
            expect(mem.length, 'mem samples').toBeGreaterThan(1);
            expect(Math.max(...mem) - mem[0], 'memory growth over the session (bytes)').toBeLessThanOrEqual(budgets + OTHER_MEMORY);
            expect(s.events.events.filter((e) => e.ev === 'mem_pressure'), 'mem_pressure events').toEqual([]);
            s.expectNoErrorEvents();
            await s.driver.quit();
        } finally {
            await resetChapters(client, delta);
        }
    },
    { timeoutMs: 60 * 60_000 },
);
