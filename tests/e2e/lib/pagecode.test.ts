import sharp from 'sharp';
import { describe, expect, it } from 'vitest';
import { SERIES } from '../../../tools/gen-fixtures/fixtures.ts';
import { Canvas } from '../../../tools/gen-fixtures/raster.ts';
import { encode, renderPage } from '../../../tools/gen-fixtures/render.ts';
import { findPageCodes, PAGE_CODE_UNITS, pageCodeCells, pageCodeRects, readPageCode, type RgbImage } from './pagecode.ts';

const series = (title: string) => {
    const s = SERIES.find((x) => x.title === title);
    if (!s) throw new Error(`no fixture series ${title}`);
    return s;
};

async function decode(file: Buffer): Promise<RgbImage> {
    const { data, info } = await sharp(file).raw().toBuffer({ resolveWithObject: true });
    return { width: info.width, height: info.height, channels: info.channels, data };
}

/** The page as the Switch would show it: scaled to fit 1280×720 with a dark background, saved as a JPEG screenshot. */
async function asScreenshot(page: Buffer, quality = 75): Promise<RgbImage> {
    const fitted = await sharp(page).resize(1280, 720, { fit: 'inside' }).toBuffer();
    const meta = await sharp(fitted).metadata();
    const shot = await sharp({ create: { width: 1280, height: 720, channels: 3, background: { r: 30, g: 30, b: 34 } } })
        .composite([{ input: fitted, left: Math.floor((1280 - (meta.width ?? 0)) / 2), top: Math.floor((720 - (meta.height ?? 0)) / 2) }])
        .jpeg({ quality, chromaSubsampling: '4:2:0' })
        .toBuffer();
    return decode(shot);
}

describe('page code layout', () => {
    it('puts the three markers in fixed cells and uses only palette colours', () => {
        const cells = pageCodeCells({ series: 1, chapter: 2, page: 5, segment: 0 });
        expect(cells).toHaveLength(16);
        expect([cells[0], cells[3], cells[12]]).toEqual([4, 2, 1]); // red, green, blue
        for (const c of cells) expect(c >= 0 && c < 8).toBe(true);
    });

    it('rejects values that do not fit', () => {
        expect(() => pageCodeCells({ series: 0, chapter: 1, page: 1, segment: 0 })).toThrow(RangeError);
        expect(() => pageCodeCells({ series: 32, chapter: 1, page: 1, segment: 0 })).toThrow(RangeError);
        expect(() => pageCodeCells({ series: 1, chapter: 1024, page: 1, segment: 0 })).toThrow(RangeError);
        expect(() => pageCodeCells({ series: 1, chapter: 1, page: 256, segment: 0 })).toThrow(RangeError);
        expect(() => pageCodeCells({ series: 1, chapter: 1, page: 1, segment: 16 })).toThrow(RangeError);
        expect(() => pageCodeCells({ series: 1, chapter: 1.5, page: 1, segment: 0 })).toThrow(RangeError);
    });
});

describe('reading page codes', () => {
    it('reads back what was drawn, for the extreme values too', () => {
        const codes = [
            { series: 1, chapter: 0, page: 0, segment: 0 },
            { series: 31, chapter: 1023, page: 255, segment: 15 },
            { series: 4, chapter: 300, page: 2, segment: 0 },
            { series: 3, chapter: 2, page: 3, segment: 7 },
        ];
        for (const code of codes) {
            const u = 4;
            const c = new Canvas(PAGE_CODE_UNITS * u + 40, PAGE_CODE_UNITS * u + 40, [250, 250, 250]);
            for (const r of pageCodeRects(code, 20, 20, u)) c.fillRect(r.x, r.y, r.w, r.h, r.color);
            expect(readPageCode({ width: c.width, height: c.height, channels: 3, data: c.data })).toEqual(code);
        }
    });

    it('reads a generated JPEG page at full size', async () => {
        const img = await decode(await encode(renderPage(series('Alpha'), 2, 5), 'jpeg'));
        expect(readPageCode(img)).toEqual({ series: 1, chapter: 2, page: 5, segment: 0 });
    });

    it('reads a page scaled down into a 1280x720 JPEG screenshot', async () => {
        const cases = [
            { title: 'Alpha', chapter: 3, page: 10 },
            { title: 'Delta Big', chapter: 300, page: 2 },
            { title: 'Beta RTL', chapter: 2, page: 7 },
        ];
        for (const c of cases) {
            const s = series(c.title);
            const shot = await asScreenshot(await encode(renderPage(s, c.chapter, c.page), s.format));
            expect(readPageCode(shot), c.title).toEqual({ series: s.code, chapter: c.chapter, page: c.page, segment: 0 });
        }
    });

    it('reads a double-page spread', async () => {
        const s = series('Beta RTL');
        const shot = await asScreenshot(await encode(renderPage(s, 1, 4), s.format));
        expect(readPageCode(shot)).toEqual({ series: 2, chapter: 1, page: 4, segment: 0 });
    });

    it('finds the segment of a tall strip that is on screen', async () => {
        const s = series('Gamma Webtoon');
        const strip = await encode(renderPage(s, 1, 2), s.format);
        // A window of the strip from y=2900, scaled to the screen width like a webtoon reader does.
        const window = await sharp(strip).extract({ left: 0, top: 2900, width: 800, height: 450 }).resize(1280).jpeg({ quality: 75 }).toBuffer();
        const found = findPageCodes(await decode(window));
        expect(found.map((f) => [f.series, f.chapter, f.page, f.segment])).toEqual([[3, 1, 2, 2]]);
    });

    it('finds nothing in noise or on a picture without a code', async () => {
        const noise = new Uint8Array(400 * 300 * 3);
        let seed = 12345;
        for (let i = 0; i < noise.length; i++) {
            seed = (Math.imul(seed, 1103515245) + 12345) >>> 0;
            noise[i] = seed >>> 24;
        }
        expect(findPageCodes({ width: 400, height: 300, channels: 3, data: noise })).toEqual([]);
        const plain = new Canvas(300, 300, [255, 0, 0]);
        expect(findPageCodes({ width: 300, height: 300, channels: 3, data: plain.data })).toEqual([]);
    });
});
