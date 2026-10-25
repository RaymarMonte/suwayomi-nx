import sharp from 'sharp';
import { describe, expect, it } from 'vitest';

import { parseRegion, regionRmse } from './screencheck.ts';

async function solid(width: number, height: number, r: number, g: number, b: number, format: 'png' | 'jpeg' = 'png') {
    const img = sharp({ create: { width, height, channels: 3, background: { r, g, b } } });
    return format === 'png' ? img.png().toBuffer() : img.jpeg().toBuffer();
}

describe('screen checks', () => {
    it('parses ImageMagick geometry', () => {
        expect(parseRegion('340x320+40+140')).toEqual({ w: 340, h: 320, x: 40, y: 140 });
        expect(() => parseRegion('340x320')).toThrow(/WxH\+X\+Y/);
        expect(() => parseRegion('0x5+1+1')).toThrow(/empty/);
    });

    it('is 0 for the same picture, 1 for black against white, in between otherwise', async () => {
        const shot = await solid(1280, 720, 255, 255, 255);
        const region = parseRegion('100x50+10+20');
        expect(await regionRmse(shot, region, await solid(100, 50, 255, 255, 255))).toBe(0);
        expect(await regionRmse(shot, region, await solid(100, 50, 0, 0, 0))).toBeCloseTo(1, 6);
        // One channel fully off out of three: sqrt(1/3).
        expect(await regionRmse(shot, region, await solid(100, 50, 0, 255, 255))).toBeCloseTo(Math.sqrt(1 / 3), 6);
    });

    it('stays far below the 0.08 threshold for a JPEG screenshot of the same screen', async () => {
        const shot = await solid(1280, 720, 30, 120, 200, 'jpeg');
        expect(await regionRmse(shot, parseRegion('260x100+30+20'), await solid(260, 100, 30, 120, 200))).toBeLessThan(0.02);
    });

    it('refuses a reference of the wrong size', async () => {
        const shot = await solid(1280, 720, 0, 0, 0);
        await expect(regionRmse(shot, parseRegion('10x10+0+0'), await solid(11, 10, 0, 0, 0))).rejects.toThrow(/11×10/);
    });
});
