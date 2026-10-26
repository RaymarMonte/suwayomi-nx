// image-bench (WP-07): the image pipeline's benchmark. The diagnostics screen (WP-08; was the Hello screen) loads Alpha ch 1 p 1 (JPEG 1000x1500) and Gamma
// Webtoon ch 1 p 1 (WebP 800x12000) from the test server through the app's real ImageLoader, REPEAT rounds each, every round
// as a page (box 2560x1440) and as a webtoon strip (box 2560x0), with the memory cache emptied before every load: each load
// decodes, and after the first one the bytes come from the disk cache (the SD card on the Switch). The numbers come from the
// app's "image" events and go to out/image-bench/summary.json (STATUS.md records the Switch's). Needs the test server.

import { writeFileSync } from 'node:fs';
import { join } from 'node:path';

import { expect } from 'vitest';

import type { AppEvent } from '../lib/events.ts';
import { scenario } from '../lib/scenario.ts';
import { appServerUrl, optionsFromEnv, SuwayomiClient } from '../lib/suwayomi.ts';

const REPEAT = 5;

/** The first page of a fixture manga's first chapter, as a path below the server URL. */
async function firstPagePath(client: SuwayomiClient, title: string): Promise<string> {
    const manga = await client.query<{ mangas: { nodes: { id: number }[] } }>(
        'query M($t: String!) { mangas(condition: { title: $t }) { nodes { id } } }',
        { t: title },
    );
    const mangaId = manga.mangas.nodes[0]?.id;
    if (mangaId === undefined) throw new Error(`the test server has no manga "${title}": run test-server.sh reset`);
    const chapters = await client.query<{ chapters: { nodes: { id: number }[] } }>(
        'query C($m: Int!) { chapters(condition: { mangaId: $m }, order: [{ by: SOURCE_ORDER, byType: ASC }], first: 1) { nodes { id } } }',
        { m: mangaId },
    );
    const chapterId = chapters.chapters.nodes[0]?.id;
    if (chapterId === undefined) throw new Error(`"${title}" has no chapters`);
    const pages = await client.query<{ fetchChapterPages: { pages: string[] } }>(
        'mutation P($c: Int!) { fetchChapterPages(input: { chapterId: $c }) { pages } }',
        { c: chapterId },
    );
    return pages.fetchChapterPages.pages[0];
}

interface Stats {
    loads: number;
    decodeMs: { min: number; median: number; max: number };
    fetchMs: { median: number };
    size: string;
    sources: Record<string, number>;
}

function stats(events: AppEvent[]): Stats {
    const decode = events.map((e) => Number(e.decodeMs)).sort((a, b) => a - b);
    const fetch = events.map((e) => Number(e.fetchMs)).sort((a, b) => a - b);
    const sources: Record<string, number> = {};
    for (const e of events) sources[String(e.source)] = (sources[String(e.source)] ?? 0) + 1;
    return {
        loads: events.length,
        decodeMs: { min: decode[0], median: decode[Math.floor(decode.length / 2)], max: decode[decode.length - 1] },
        fetchMs: { median: fetch[Math.floor(fetch.length / 2)] },
        size: `${String(events[0]?.w)}x${String(events[0]?.h)}`,
        sources,
    };
}

scenario(
    'image-bench',
    async (s) => {
        const client = new SuwayomiClient(optionsFromEnv());
        const paths = [await firstPagePath(client, 'Alpha'), await firstPagePath(client, 'Gamma Webtoon')];
        const url = appServerUrl(s.driver.name);
        await s.launch({ testConfig: { server: { url }, imageBench: { paths, repeat: REPEAT } } });
        const summary = await s.events.waitForEvent('image_bench', {}, { timeoutMs: 240_000 });
        expect(summary).toMatchObject({ loads: paths.length * REPEAT * 2, ok: paths.length * REPEAT * 2, failed: 0 });

        const images = s.events.events.filter((e) => e.ev === 'image' && e.source !== 'memory');
        const jpeg = images.filter((e) => e.url === `${url}${paths[0]}`);
        const webp = images.filter((e) => e.url === `${url}${paths[1]}`);
        for (const e of [...jpeg, ...webp]) expect(e.error, JSON.stringify(e)).toBe('');
        // The diagnostics screen loads each image as a page (box 2560x1440), then as a strip (box 2560x0), REPEAT times in a row.
        const pageBox = (list: AppEvent[]) => list.filter((_, i) => i % 2 === 0);
        const stripBox = (list: AppEvent[]) => list.filter((_, i) => i % 2 === 1);
        const result = {
            driver: s.driver.name,
            jpegPage: stats(pageBox(jpeg)),    // 1000x1500 (960x1440 is within the 15 % shrink slack)
            jpegStrip: stats(stripBox(jpeg)),  // the same, width-limited
            webpPage: stats(pageBox(webp)),    // the 800x12000 strip squeezed into a page box: 96x1440
            webpStrip: stats(stripBox(webp)),  // 800x12000, 6 tiles
            peakMemUsed: Math.max(...images.map((e) => Number(e.memUsed))),
            memUsedBefore: summary.memUsedBefore,
            memUsedAfter: summary.memUsedAfter,
            wallMs: summary.ms,
        };
        writeFileSync(join(s.out, 'summary.json'), `${JSON.stringify(result, null, 2)}\n`);
        console.log(`image-bench (${s.driver.name}): ${JSON.stringify(result)}`);
        expect(result.webpStrip.loads).toBe(REPEAT);
        expect(result.webpStrip.size).toBe('800x12000');
        expect(result.jpegPage.loads).toBe(REPEAT);

        s.expectNoErrorEvents();
        await s.driver.quit();
    },
    { timeoutMs: 360_000 },
);
