// Screen checks (D24): before any press that could start something on the console, crop the screenshot and compare it with a
// reference crop. Same maths as ImageMagick's `compare -metric RMSE` (normalised to 0..1) that WP-00 calibrated: same screen
// measured 0.000-0.009, other screens 0.12-0.39, threshold 0.08. Regions and references: tests/e2e/device/profile.json;
// the crops themselves are console-specific and git-ignored (tests/e2e/device/local/<name>.png).

import sharp, { type Sharp } from 'sharp';

export interface Region {
    w: number;
    h: number;
    x: number;
    y: number;
}

/** "WxH+X+Y" (ImageMagick geometry, as in profile.json) → numbers. */
export function parseRegion(geometry: string): Region {
    const m = /^(\d+)x(\d+)\+(\d+)\+(\d+)$/.exec(geometry.trim());
    if (!m) throw new Error(`bad region "${geometry}", expected WxH+X+Y`);
    const [w, h, x, y] = m.slice(1).map(Number);
    if (w === 0 || h === 0) throw new Error(`empty region "${geometry}"`);
    return { w, h, x, y };
}

async function rgb(image: Sharp): Promise<{ data: Buffer; width: number; height: number }> {
    const { data, info } = await image.removeAlpha().toColourspace('srgb').raw().toBuffer({ resolveWithObject: true });
    if (info.channels !== 3) throw new Error(`expected 3 colour channels, got ${info.channels}`);
    return { data, width: info.width, height: info.height };
}

/** Root-mean-square difference (0 = identical, 1 = opposite) between a region of the screenshot and the reference picture. */
export async function regionRmse(screenshot: Buffer, region: Region, reference: Buffer): Promise<number> {
    const crop = await rgb(sharp(screenshot).extract({ left: region.x, top: region.y, width: region.w, height: region.h }));
    const ref = await rgb(sharp(reference));
    if (ref.width !== region.w || ref.height !== region.h) {
        throw new Error(`reference is ${ref.width}×${ref.height} but the region is ${region.w}×${region.h}`);
    }
    let sum = 0;
    for (let i = 0; i < crop.data.length; i++) {
        const d = crop.data[i] - ref.data[i];
        sum += d * d;
    }
    return Math.sqrt(sum / crop.data.length) / 255;
}
