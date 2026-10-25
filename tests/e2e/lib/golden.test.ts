import { existsSync, mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import sharp from 'sharp';
import { afterEach, beforeEach, describe, expect, it } from 'vitest';

import { compareGolden, decodeRgba, expectGolden, type GoldenOptions } from './golden.ts';

const W = 160;
const H = 90;

/** A grey picture with optional filled rectangles. */
async function picture(rects: { x: number; y: number; w: number; h: number; rgb: [number, number, number] }[] = [], format: 'png' | 'jpeg' = 'png') {
    const data = Buffer.alloc(W * H * 3, 60);
    for (const r of rects) {
        for (let y = r.y; y < r.y + r.h; y++) {
            for (let x = r.x; x < r.x + r.w; x++) data.set(r.rgb, (y * W + x) * 3);
        }
    }
    const img = sharp(data, { raw: { width: W, height: H, channels: 3 } });
    return format === 'png' ? img.png().toBuffer() : img.jpeg({ quality: 95 }).toBuffer();
}

let dir: string;
let opts: GoldenOptions;

beforeEach(() => {
    dir = mkdtempSync(join(tmpdir(), 'snx-golden-'));
    opts = { driver: 'desktop', scenario: 'unit', step: 'hello', outDir: join(dir, 'out'), goldensDir: join(dir, 'goldens'), update: false };
});
afterEach(() => rmSync(dir, { recursive: true, force: true }));

describe('golden compare', () => {
    it('fails without a golden and says how to make one; --update-goldens writes it', async () => {
        const img = await picture();
        const missing = await compareGolden(img, opts);
        expect(missing.pass).toBe(false);
        expect(missing.message).toContain('--update-goldens');
        expect(existsSync(missing.actualPath!)).toBe(true);

        const written = await compareGolden(img, { ...opts, update: true });
        expect(written.updated).toBe(true);
        expect(existsSync(join(dir, 'goldens', 'desktop', 'unit', 'hello.png'))).toBe(true);
        expect((await compareGolden(img, opts)).pass).toBe(true);
    });

    it('passes on an identical picture, fails with a diff image on a changed one', async () => {
        await compareGolden(await picture(), { ...opts, update: true });
        const same = await compareGolden(await picture(), opts);
        expect(same).toMatchObject({ pass: true, diffPixels: 0 });

        const broken = await compareGolden(await picture([{ x: 10, y: 10, w: 20, h: 10, rgb: [255, 255, 255] }]), opts);
        expect(broken.pass).toBe(false);
        expect(broken.diffPixels).toBe(200);
        expect(broken.message).toMatch(/200 pixels differ/);
        expect(existsSync(broken.diffPath!)).toBe(true);
        expect(existsSync(broken.actualPath!)).toBe(true);
        await expect(expectGolden(await picture([{ x: 10, y: 10, w: 20, h: 10, rgb: [255, 255, 255] }]), opts)).rejects.toThrow(
            /doesn't match golden/,
        );
    });

    it('allows a few differing pixels up to maxDiffPercent', async () => {
        await compareGolden(await picture(), { ...opts, update: true });
        const fewPixels = await picture([{ x: 0, y: 0, w: 2, h: 2, rgb: [255, 255, 255] }]); // 4 of 14400 = 0.028 %
        expect((await compareGolden(fewPixels, opts)).pass).toBe(true);
        expect((await compareGolden(fewPixels, { ...opts, maxDiffPercent: 0 })).pass).toBe(false);
    });

    it('ignores masked areas, and the stored golden shows the mask', async () => {
        const mask = { x: 0, y: 70, w: 60, h: 20, why: 'clock' };
        await compareGolden(await picture([{ x: 5, y: 75, w: 30, h: 10, rgb: [255, 255, 255] }]), { ...opts, masks: [mask], update: true });
        const otherClock = await picture([{ x: 8, y: 72, w: 40, h: 12, rgb: [200, 200, 0] }]);
        expect((await compareGolden(otherClock, { ...opts, masks: [mask] })).pass).toBe(true);
        expect((await compareGolden(otherClock, opts)).pass).toBe(false); // without the mask it differs

        const stored = await decodeRgba(await sharp(join(dir, 'goldens', 'desktop', 'unit', 'hello.png')).toBuffer());
        const i = (80 * W + 10) * 4;
        expect([...stored.data.subarray(i, i + 3)]).toEqual([255, 0, 255]);
    });

    it('fails on a size mismatch', async () => {
        await compareGolden(await picture(), { ...opts, update: true });
        const bigger = await sharp({ create: { width: W + 1, height: H, channels: 3, background: { r: 60, g: 60, b: 60 } } }).png().toBuffer();
        const result = await compareGolden(bigger, opts);
        expect(result.pass).toBe(false);
        expect(result.message).toContain('size differs');
    });

    it('compares JPEG screenshots (the Switch) against a PNG golden within the threshold', async () => {
        await compareGolden(await picture([{ x: 40, y: 30, w: 50, h: 20, rgb: [25, 138, 198] }]), { ...opts, update: true });
        const jpeg = await picture([{ x: 40, y: 30, w: 50, h: 20, rgb: [25, 138, 198] }], 'jpeg');
        expect((await compareGolden(jpeg, opts)).pass).toBe(true);
    });
});
