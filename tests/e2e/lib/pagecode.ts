// The page code: a 4×4 block of coloured cells printed on every fixture page (docs/TESTING.md "Fixture manga").
// It says which (series, chapter, page, segment) a picture shows, so a test can read it from a screenshot with plain pixel
// maths: no OCR, and no model looks at the image. tools/gen-fixtures draws it with pageCodeRects(), tests read it with
// findPageCodes(). Both sides use the layout below, so they can't drift apart.
//
// Layout, in units of u pixels (u is picked per image, see pageCodeUnit()):
//   quiet zone 2u white | frame 1u black | 4×4 cells of 8u, with 2u white gutters around and between them | frame | quiet zone
//   = 48u in total. Cells are numbered row by row, 0 = top left, 15 = bottom right.
// Colours: 8 "pure" colours, palette index = (red << 2) | (green << 1) | blue, each channel 0 or 255.
// Markers: cell 0 is red, cell 3 is green, cell 12 is blue. They let the reader find the block and its size and direction.
// Data: the other 13 cells hold 3 bits each = 39 bits: series (5) chapter (10) page (8) segment (4) = 27-bit payload,
// then a 12-bit checksum of the payload. A wrong read almost never passes the checksum (1 in 4096).

export interface PageCode {
    series: number; // 1..31, which fixture series (tools/gen-fixtures/fixtures.ts)
    chapter: number; // 0..1023, chapter number as printed on the page
    page: number; // 0..255, page number as printed (1-based; 0 = cover)
    segment: number; // 0..15, which part of a tall strip (0 for normal pages)
}

export interface FoundPageCode extends PageCode {
    x: number; // centre of the red marker cell, in image pixels
    y: number;
    pitch: number; // distance between neighbouring cell centres, in image pixels
}

/** A decoded picture: rows of RGB or RGBA bytes, top to bottom (what sharp's .raw() gives). */
export interface RgbImage {
    width: number;
    height: number;
    channels: number; // 3 or 4
    data: Uint8Array;
}

export interface Rect {
    x: number;
    y: number;
    w: number;
    h: number;
    color: readonly [number, number, number];
}

export const PALETTE: readonly (readonly [number, number, number])[] = [
    [0, 0, 0], [0, 0, 255], [0, 255, 0], [0, 255, 255],
    [255, 0, 0], [255, 0, 255], [255, 255, 0], [255, 255, 255],
];
const BLACK = 0, BLUE = 1, GREEN = 2, RED = 4, WHITE = 7;

const QUIET = 2, FRAME = 1, GUTTER = 2, CELL = 8;
const PITCH = CELL + GUTTER;
/** Side of the whole block, quiet zone included, in units. */
export const PAGE_CODE_UNITS = 2 * (QUIET + FRAME) + 4 * CELL + 5 * GUTTER; // 48
const CELLS_START = QUIET + FRAME + GUTTER; // first cell's offset from the block's corner, in units

const MARKERS = new Map<number, number>([[0, RED], [3, GREEN], [12, BLUE]]);
const DATA_CELLS = [1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 13, 14, 15];
const FIELDS = [
    { name: 'series', bits: 5 },
    { name: 'chapter', bits: 10 },
    { name: 'page', bits: 8 },
    { name: 'segment', bits: 4 },
] as const;
const PAYLOAD_BITS = 27;
const CHECKSUM_BITS = 12;

/** Unit size for a picture of this size: about 1/160 of its shorter side, at least 3 px. */
export function pageCodeUnit(width: number, height: number): number {
    return Math.max(3, Math.round(Math.min(width, height) / 160));
}

function checksum(payload: number): number {
    // Knuth's multiplicative hash of the payload, top 12 bits.
    return (Math.imul(payload ^ 0x2545f49, 0x9e3779b1) >>> (32 - CHECKSUM_BITS)) & ((1 << CHECKSUM_BITS) - 1);
}

function toPayload(code: PageCode): number {
    let payload = 0;
    for (const f of FIELDS) {
        const v = code[f.name];
        if (!Number.isInteger(v) || v < 0 || v >= 1 << f.bits) throw new RangeError(`page code ${f.name} out of range: ${v}`);
        payload = payload * (1 << f.bits) + v;
    }
    if (code.series === 0) throw new RangeError('page code series must be 1..31');
    return payload;
}

function fromPayload(payload: number): PageCode {
    const code = { series: 0, chapter: 0, page: 0, segment: 0 };
    for (let i = FIELDS.length - 1; i >= 0; i--) {
        const f = FIELDS[i];
        code[f.name] = payload & ((1 << f.bits) - 1);
        payload = Math.floor(payload / (1 << f.bits));
    }
    return code;
}

/** Palette index of each of the 16 cells, row by row. */
export function pageCodeCells(code: PageCode): number[] {
    const payload = toPayload(code);
    const bits: number[] = [];
    for (let i = PAYLOAD_BITS - 1; i >= 0; i--) bits.push(Math.floor(payload / 2 ** i) & 1);
    const sum = checksum(payload);
    for (let i = CHECKSUM_BITS - 1; i >= 0; i--) bits.push((sum >> i) & 1);
    const cells = new Array<number>(16);
    for (const [cell, color] of MARKERS) cells[cell] = color;
    DATA_CELLS.forEach((cell, k) => { cells[cell] = (bits[3 * k] << 2) | (bits[3 * k + 1] << 1) | bits[3 * k + 2]; });
    return cells;
}

/** The rectangles to paint, in order, for a block whose top-left corner (quiet zone included) is at (x, y). */
export function pageCodeRects(code: PageCode, x: number, y: number, u: number): Rect[] {
    const side = PAGE_CODE_UNITS * u;
    const rects: Rect[] = [
        { x, y, w: side, h: side, color: PALETTE[WHITE] },
        { x: x + QUIET * u, y: y + QUIET * u, w: side - 2 * QUIET * u, h: side - 2 * QUIET * u, color: PALETTE[BLACK] },
        {
            x: x + (QUIET + FRAME) * u, y: y + (QUIET + FRAME) * u,
            w: side - 2 * (QUIET + FRAME) * u, h: side - 2 * (QUIET + FRAME) * u, color: PALETTE[WHITE],
        },
    ];
    pageCodeCells(code).forEach((color, i) => {
        const row = Math.floor(i / 4), col = i % 4;
        rects.push({
            x: x + (CELLS_START + col * PITCH) * u, y: y + (CELLS_START + row * PITCH) * u,
            w: CELL * u, h: CELL * u, color: PALETTE[color],
        });
    });
    return rects;
}

// ---- Reading ----

// A pixel counts as a marker colour only when every channel is clearly on (> 160) or clearly off (< 95).
// Data cells are read more loosely: the average of the cell's middle, each channel split at 128.
const ON = 160, OFF = 95;

function strictClass(r: number, g: number, b: number): number {
    const bit = (v: number) => (v > ON ? 1 : v < OFF ? 0 : -1);
    const br = bit(r), bg = bit(g), bb = bit(b);
    return br < 0 || bg < 0 || bb < 0 ? -1 : (br << 2) | (bg << 1) | bb;
}

interface Blob {
    color: number;
    count: number;
    cx: number;
    cy: number;
    w: number;
    h: number;
}

/** Connected areas (4-neighbour) of strictly red, green or blue pixels. */
function findBlobs(img: RgbImage): Blob[] {
    const { width, height, channels, data } = img;
    const n = width * height;
    const cls = new Int8Array(n);
    for (let i = 0, p = 0; i < n; i++, p += channels) {
        const c = strictClass(data[p], data[p + 1], data[p + 2]);
        cls[i] = c === RED || c === GREEN || c === BLUE ? c : -1;
    }
    const blobs: Blob[] = [];
    const queue = new Int32Array(n);
    for (let start = 0; start < n; start++) {
        const color = cls[start];
        if (color < 0) continue;
        let head = 0, tail = 0, count = 0, sx = 0, sy = 0;
        let minX = width, maxX = -1, minY = height, maxY = -1;
        queue[tail++] = start;
        cls[start] = -1;
        while (head < tail) {
            const i = queue[head++];
            const x = i % width, y = (i - x) / width;
            count++; sx += x; sy += y;
            if (x < minX) minX = x;
            if (x > maxX) maxX = x;
            if (y < minY) minY = y;
            if (y > maxY) maxY = y;
            if (x > 0 && cls[i - 1] === color) { cls[i - 1] = -1; queue[tail++] = i - 1; }
            if (x < width - 1 && cls[i + 1] === color) { cls[i + 1] = -1; queue[tail++] = i + 1; }
            if (y > 0 && cls[i - width] === color) { cls[i - width] = -1; queue[tail++] = i - width; }
            if (y < height - 1 && cls[i + width] === color) { cls[i + width] = -1; queue[tail++] = i + width; }
        }
        const w = maxX - minX + 1, h = maxY - minY + 1;
        // Keep only filled, roughly square areas of at least 3×3 px: what a cell looks like at any zoom.
        if (w >= 3 && h >= 3 && w <= 2 * h && h <= 2 * w && count >= 0.6 * w * h) {
            blobs.push({ color, count, cx: sx / count + 0.5, cy: sy / count + 0.5, w, h });
        }
    }
    return blobs;
}

function nearestBlob(blobs: Blob[], color: number, x: number, y: number, maxDist: number, size: number): Blob | undefined {
    let best: Blob | undefined;
    let bestDist = maxDist;
    for (const b of blobs) {
        if (b.color !== color) continue;
        const s = (b.w + b.h) / 2;
        if (s < 0.6 * size || s > 1.6 * size) continue;
        const d = Math.hypot(b.cx - x, b.cy - y);
        if (d < bestDist) { best = b; bestDist = d; }
    }
    return best;
}

function sampleCell(img: RgbImage, x: number, y: number, radius: number): number {
    const { width, height, channels, data } = img;
    let r = 0, g = 0, b = 0, n = 0;
    const x0 = Math.max(0, Math.round(x - radius)), x1 = Math.min(width - 1, Math.round(x + radius));
    const y0 = Math.max(0, Math.round(y - radius)), y1 = Math.min(height - 1, Math.round(y + radius));
    for (let yy = y0; yy <= y1; yy++) {
        for (let xx = x0; xx <= x1; xx++) {
            const p = (yy * width + xx) * channels;
            r += data[p]; g += data[p + 1]; b += data[p + 2]; n++;
        }
    }
    if (n === 0) return -1;
    return ((r / n >= 128 ? 1 : 0) << 2) | ((g / n >= 128 ? 1 : 0) << 1) | (b / n >= 128 ? 1 : 0);
}

/** Every page code visible in the picture, top to bottom, then left to right. Blocks that fail the checksum are skipped. */
export function findPageCodes(img: RgbImage): FoundPageCode[] {
    const blobs = findBlobs(img);
    const found: FoundPageCode[] = [];
    for (const red of blobs) {
        if (red.color !== RED) continue;
        const size = (red.w + red.h) / 2;
        const pitch = (size * PITCH) / CELL;
        const green = nearestBlob(blobs, GREEN, red.cx + 3 * pitch, red.cy, pitch / 2, size);
        const blue = nearestBlob(blobs, BLUE, red.cx, red.cy + 3 * pitch, pitch / 2, size);
        if (!green || !blue) continue;
        // One step to the next column / row. Using the markers' real centres copes with uneven scaling.
        const colX = (green.cx - red.cx) / 3, colY = (green.cy - red.cy) / 3;
        const rowX = (blue.cx - red.cx) / 3, rowY = (blue.cy - red.cy) / 3;
        const radius = Math.max(0, Math.hypot(colX, colY) * 0.2);
        const bits: number[] = [];
        let ok = true;
        for (const cell of DATA_CELLS) {
            const row = Math.floor(cell / 4), col = cell % 4;
            const c = sampleCell(img, red.cx + col * colX + row * rowX, red.cy + col * colY + row * rowY, radius);
            if (c < 0) { ok = false; break; }
            bits.push((c >> 2) & 1, (c >> 1) & 1, c & 1);
        }
        if (!ok) continue;
        let payload = 0;
        for (let i = 0; i < PAYLOAD_BITS; i++) payload = payload * 2 + bits[i];
        let sum = 0;
        for (let i = PAYLOAD_BITS; i < PAYLOAD_BITS + CHECKSUM_BITS; i++) sum = sum * 2 + bits[i];
        if (sum !== checksum(payload)) continue;
        const code = fromPayload(payload);
        if (code.series === 0) continue;
        found.push({ ...code, x: red.cx, y: red.cy, pitch: Math.hypot(colX, colY) });
    }
    return found.sort((a, b) => a.y - b.y || a.x - b.x);
}

/** The one page code in the picture, or null when there is none (or more than one different code). */
export function readPageCode(img: RgbImage): PageCode | null {
    const found = findPageCodes(img);
    if (found.length === 0) return null;
    const first = found[0];
    const same = (f: PageCode) =>
        f.series === first.series && f.chapter === first.chapter && f.page === first.page && f.segment === first.segment;
    if (!found.every(same)) return null;
    return { series: first.series, chapter: first.chapter, page: first.page, segment: first.segment };
}
