import { mkdtemp, readdir, readFile, rm, stat } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import sharp from 'sharp';
import { describe, expect, it } from 'vitest';
import { chapterFolder, chapterTime, pageFile, SERIES } from './fixtures.ts';
import { generate } from './gen-fixtures.ts';
import { encode, pageBytes, renderPage } from './render.ts';

const series = (title: string) => SERIES.find((x) => x.title === title)!;

describe('fixture set', () => {
    it('has unique series codes in 1..31 and unique titles', () => {
        const codes = SERIES.map((s) => s.code);
        expect(new Set(codes).size).toBe(codes.length);
        for (const c of codes) expect(c >= 1 && c <= 31).toBe(true);
        expect(new Set(SERIES.map((s) => s.title)).size).toBe(SERIES.length);
    });

    it('names chapters and pages so that name order is reading order', () => {
        expect(chapterFolder(12)).toBe('Chapter 12');
        expect(pageFile(7, 'jpeg')).toBe('007.jpg');
        expect(pageFile(7, 'webp')).toBe('007.webp');
    });
});

describe('page rendering', () => {
    it('gives the same bytes every time, in every format', async () => {
        for (const title of ['Alpha', 'Beta RTL', 'Gamma Webtoon']) {
            const s = series(title);
            const a = await encode(renderPage(s, 1, 2), s.format);
            const b = await encode(renderPage(s, 1, 2), s.format);
            expect(a.equals(b), title).toBe(true);
        }
    });

    it('makes the pages the size and format the fixture list says', async () => {
        const meta = async (title: string, ch: number, p: number) => sharp(await pageBytes(series(title), ch, p)).metadata();
        expect(await meta('Alpha', 1, 1)).toMatchObject({ format: 'jpeg', width: 1000, height: 1500 });
        expect(await meta('Beta RTL', 1, 4)).toMatchObject({ format: 'png', width: 2000, height: 1500 });
        expect(await meta('Gamma Webtoon', 2, 3)).toMatchObject({ format: 'webp', width: 800, height: 12000 });
    });

    it('breaks the Broken pages the way it says', async () => {
        const s = series('Broken');
        const corrupt = await pageBytes(s, 1, 1);
        expect([...corrupt.subarray(0, 3)]).toEqual([0xff, 0xd8, 0xff]); // still looks like a JPEG
        await expect(sharp(corrupt).raw().toBuffer()).rejects.toThrow();
        expect((await pageBytes(s, 1, 2)).length).toBe(0);
        expect(await sharp(await pageBytes(s, 1, 3)).metadata()).toMatchObject({ width: 1, height: 1 });
        expect(await sharp(await pageBytes(s, 1, 4)).metadata()).toMatchObject({ width: 1000, height: 1500 });
    });
});

// The whole set takes a while, so this runs only when asked: SNX_FULL_FIXTURES=1 npm test
describe.runIf(process.env.SNX_FULL_FIXTURES === '1')('full fixture set', () => {
    it('is identical when generated twice, with the fixed file times', async () => {
        const dir = await mkdtemp(join(tmpdir(), 'snx-fixtures-'));
        try {
            const first = await generate(join(dir, 'a'));
            const second = await generate(join(dir, 'b'));
            expect(second.digest).toBe(first.digest);
            expect(second.sums).toBe(first.sums);
            const alpha = series('Alpha');
            expect((await stat(join(dir, 'a', 'Alpha', chapterFolder(2)))).mtimeMs).toBe(chapterTime(alpha, 2).getTime());
            expect((await readdir(join(dir, 'a', 'Delta Big'))).length).toBe(300);
            expect(JSON.parse(await readFile(join(dir, 'a', 'Alpha', 'details.json'), 'utf8'))).toMatchObject({ status: 1 });
        } finally {
            await rm(dir, { recursive: true, force: true });
        }
    }, 300_000);
});
