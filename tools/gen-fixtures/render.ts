// Draws one fixture page (or cover) and encodes it. Every page shows its name in big letters ("ALPHA / CH 2 / P 5/10")
// for people looking at screenshots, and the page code (tests/e2e/lib/pagecode.ts) in its top-right corner for tests.

import sharp from 'sharp';
import { PAGE_CODE_UNITS, pageCodeRects, pageCodeUnit, type PageCode } from '../../tests/e2e/lib/pagecode.ts';
import type { ImageFormat, SeriesDef } from './fixtures.ts';
import { Canvas, textHeight, textWidth, type Rgb } from './raster.ts';

const INK: Rgb = [20, 20, 20];

export function isSpread(s: SeriesDef, chapter: number, page: number): boolean {
    return (s.spreads ?? []).some((p) => p.chapter === chapter && p.page === page);
}

/** Pixel size of a page: spreads are twice as wide. */
export function pageSize(s: SeriesDef, chapter: number, page: number): { width: number; height: number } {
    return { width: isSpread(s, chapter, page) ? 2 * s.width : s.width, height: s.height };
}

function darker(c: Rgb, by: number): Rgb {
    return [Math.max(0, c[0] - by), Math.max(0, c[1] - by), Math.max(0, c[2] - by)];
}

/**
 * One framed area with the page code top right, big text lines under it and a small footer line at the bottom.
 * The text stays within textW pixels from the left (a spread keeps it on its left half).
 */
function drawPanel(c: Canvas, x: number, y: number, w: number, h: number, code: PageCode, lines: string[], footer: string,
    textW = w): void {
    const u = pageCodeUnit(w, h);
    const border = Math.max(2, Math.round(Math.min(w, h) / 150));
    const margin = 3 * u;
    c.strokeRect(x, y, w, h, border, INK);
    const side = PAGE_CODE_UNITS * u;
    for (const r of pageCodeRects(code, x + w - margin - side, y + margin, u)) c.fillRect(r.x, r.y, r.w, r.h, r.color);

    // Largest font scale at which the longest line fits (and 7 px rows stay under a tenth of the height).
    const fit = (text: string) => Math.floor((textW - 4 * margin + 1) / (6 * text.length));
    const longest = lines.reduce((a, b) => (b.length > a.length ? b : a));
    const scale = Math.max(1, Math.min(fit(longest), Math.floor((h * 0.1) / 7)));
    let ty = y + margin + side + 4 * u;
    for (const line of lines) {
        c.drawText(line, x + 2 * margin, ty, scale, INK);
        ty += textHeight(scale) + 3 * scale;
    }
    const small = Math.max(1, Math.min(Math.floor(scale / 3), fit(footer)));
    const fy = y + h - margin - textHeight(small);
    if (fy > ty && textWidth(footer, small) <= textW - 4 * margin) c.drawText(footer, x + 2 * margin, fy, small, INK);
}

function footerFor(s: SeriesDef, width: number, height: number): string {
    return `SNX FIXTURE ${s.code}  ${width}X${height} ${s.format}`;
}

/** The pixels of a normal (not broken) page. */
export function renderPage(s: SeriesDef, chapter: number, page: number): Canvas {
    const { width, height } = pageSize(s, chapter, page);
    const c = new Canvas(width, height, s.background);
    const pageLine = `P ${page}/${s.pages}`;
    if (s.segmentHeight) {
        // A tall strip: one panel per segment, each with its own page code, so whatever part is on screen can be identified.
        const segments = Math.ceil(height / s.segmentHeight);
        for (let seg = 0; seg < segments; seg++) {
            const y = seg * s.segmentHeight;
            const h = Math.min(s.segmentHeight, height - y);
            if (seg % 2 === 1) c.fillRect(0, y, width, h, darker(s.background, 14));
            drawPanel(c, 0, y, width, h, { series: s.code, chapter, page, segment: seg },
                [s.label, `CH ${chapter}`, pageLine, `SEG ${seg}/${segments - 1}`], footerFor(s, width, height));
        }
    } else {
        const lines = [s.label, `CH ${chapter}`, pageLine];
        if (isSpread(s, chapter, page)) lines.push('SPREAD');
        drawPanel(c, 0, 0, width, height, { series: s.code, chapter, page, segment: 0 }, lines, footerFor(s, width, height),
            isSpread(s, chapter, page) ? width / 2 : width);
        if (isSpread(s, chapter, page)) {
            // Dashed fold line down the middle of a double-page spread.
            const dash = Math.round(height / 60);
            for (let y = 0; y < height; y += 2 * dash) c.fillRect(width / 2 - 2, y, 4, dash, INK);
        }
    }
    return c;
}

/** cover.jpg: same style as a page, page number 0. */
export function renderCover(s: SeriesDef): Canvas {
    const width = 600, height = 900;
    const c = new Canvas(width, height, s.background);
    drawPanel(c, 0, 0, width, height, { series: s.code, chapter: 0, page: 0, segment: 0 }, [s.label, 'COVER'],
        `SNX FIXTURE ${s.code}  COVER`);
    return c;
}

export async function encode(c: Canvas, format: ImageFormat): Promise<Buffer> {
    const img = sharp(c.data, { raw: { width: c.width, height: c.height, channels: 3 } });
    switch (format) {
        case 'jpeg':
            return img.jpeg({ quality: 85, chromaSubsampling: '4:2:0', mozjpeg: false }).toBuffer();
        case 'png':
            return img.png({ compressionLevel: 6, adaptiveFiltering: false }).toBuffer();
        case 'webp':
            return img.webp({ quality: 80, effort: 4 }).toBuffer();
    }
}

/** The file bytes of a page, broken ones included. */
export async function pageBytes(s: SeriesDef, chapter: number, page: number): Promise<Buffer> {
    switch (s.broken?.[page]) {
        case 'empty':
            return Buffer.alloc(0);
        case 'tiny':
            return encode(new Canvas(1, 1, s.background), s.format);
        case 'corrupt': {
            // A real header followed by the first third of the data and then junk: decoders start, then fail.
            const good = await encode(renderPage(s, chapter, page), s.format);
            const junk = Buffer.alloc(512);
            for (let i = 0; i < junk.length; i++) junk[i] = (i * 37 + 11) & 0xff;
            return Buffer.concat([good.subarray(0, Math.floor(good.length / 3)), junk]);
        }
        default:
            return encode(renderPage(s, chapter, page), s.format);
    }
}

export async function coverBytes(s: SeriesDef): Promise<Buffer> {
    return encode(renderCover(s), 'jpeg');
}
