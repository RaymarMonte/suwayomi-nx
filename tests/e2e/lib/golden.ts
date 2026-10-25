// Golden screenshots (docs/TESTING.md "Golden screenshots"): compare a screenshot with a stored PNG using pixelmatch.
// Goldens live in tests/e2e/goldens/<driver>/<scenario>/<step>.png. Masks paint the same rectangles magenta in both pictures
// first, so a clock or an animation can't fail the compare. The stored golden has the masks painted in, so anyone looking at
// it sees what is ignored. On a mismatch, <step>.actual.png and <step>.diff.png go to the scenario's output folder.
// Updating goldens (only when a UI change is intended): `tools/e2e.sh <driver> <scenario> --update-goldens`.

import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import pixelmatch from 'pixelmatch';
import sharp from 'sharp';

export const GOLDENS_DIR = fileURLToPath(new URL('../goldens/', import.meta.url));

export interface MaskRect {
    x: number;
    y: number;
    w: number;
    h: number;
    why?: string; // what it hides, e.g. "clock"
}

export interface GoldenOptions {
    driver: string; // "desktop" or "switch"
    scenario: string;
    step: string; // file name without .png
    outDir: string; // where actual/diff pictures go on a mismatch
    masks?: MaskRect[];
    /** pixelmatch's per-pixel colour threshold, 0 (exact) to 1. Default 0.1. */
    threshold?: number;
    /** Share of pixels allowed to differ, in percent. Default 0.1 (= 921 pixels at 1280×720). */
    maxDiffPercent?: number;
    /** Write the golden instead of comparing. Default: the SNX_UPDATE_GOLDENS=1 environment variable. */
    update?: boolean;
    goldensDir?: string; // default tests/e2e/goldens/ (tests use a temporary folder)
}

export interface GoldenResult {
    pass: boolean;
    updated: boolean; // true when the golden was (re)written instead of compared
    diffPixels: number;
    diffPercent: number;
    message: string;
    goldenPath: string;
    actualPath?: string;
    diffPath?: string;
}

const MASK_COLOUR = [255, 0, 255, 255] as const;

interface Rgba {
    width: number;
    height: number;
    data: Buffer;
}

/** Decodes any picture sharp understands (PNG, JPEG, ...) into RGBA bytes. */
export async function decodeRgba(image: Buffer): Promise<Rgba> {
    const { data, info } = await sharp(image).ensureAlpha().raw().toBuffer({ resolveWithObject: true });
    return { width: info.width, height: info.height, data };
}

function paintMasks(img: Rgba, masks: MaskRect[]): void {
    for (const m of masks) {
        const x0 = Math.max(0, Math.floor(m.x));
        const y0 = Math.max(0, Math.floor(m.y));
        const x1 = Math.min(img.width, Math.ceil(m.x + m.w));
        const y1 = Math.min(img.height, Math.ceil(m.y + m.h));
        for (let y = y0; y < y1; y++) {
            for (let x = x0; x < x1; x++) {
                img.data.set(MASK_COLOUR, (y * img.width + x) * 4);
            }
        }
    }
}

function encodePng(img: Rgba): Promise<Buffer> {
    return sharp(img.data, { raw: { width: img.width, height: img.height, channels: 4 } }).png().toBuffer();
}

export async function compareGolden(image: Buffer, opts: GoldenOptions): Promise<GoldenResult> {
    const goldenPath = join(opts.goldensDir ?? GOLDENS_DIR, opts.driver, opts.scenario, `${opts.step}.png`);
    const masks = opts.masks ?? [];
    const update = opts.update ?? process.env.SNX_UPDATE_GOLDENS === '1';
    const threshold = opts.threshold ?? 0.1;
    const maxDiffPercent = opts.maxDiffPercent ?? 0.1;

    const actual = await decodeRgba(image);
    paintMasks(actual, masks);

    if (update) {
        mkdirSync(dirname(goldenPath), { recursive: true });
        writeFileSync(goldenPath, await encodePng(actual));
        return { pass: true, updated: true, diffPixels: 0, diffPercent: 0, goldenPath, message: `golden written: ${goldenPath}` };
    }

    const fail = async (message: string, diff?: Rgba, diffPixels = 0, diffPercent = 100): Promise<GoldenResult> => {
        mkdirSync(opts.outDir, { recursive: true });
        const actualPath = join(opts.outDir, `${opts.step}.actual.png`);
        writeFileSync(actualPath, await encodePng(actual));
        let diffPath: string | undefined;
        if (diff) {
            diffPath = join(opts.outDir, `${opts.step}.diff.png`);
            writeFileSync(diffPath, await encodePng(diff));
        }
        const files = diffPath ? `actual: ${actualPath}, diff: ${diffPath}` : `actual: ${actualPath}`;
        return { pass: false, updated: false, diffPixels, diffPercent, goldenPath, actualPath, diffPath, message: `${message} (${files})` };
    };

    if (!existsSync(goldenPath)) {
        return fail(`no golden yet at ${goldenPath}. If this screen is right, run with --update-goldens and look at the new picture`);
    }
    const golden = await decodeRgba(readFileSync(goldenPath));
    paintMasks(golden, masks);
    if (golden.width !== actual.width || golden.height !== actual.height) {
        return fail(`size differs: golden ${golden.width}×${golden.height}, screenshot ${actual.width}×${actual.height}`);
    }

    const diff: Rgba = { width: actual.width, height: actual.height, data: Buffer.alloc(actual.data.length) };
    const diffPixels = pixelmatch(golden.data, actual.data, diff.data, actual.width, actual.height, { threshold });
    const diffPercent = (diffPixels / (actual.width * actual.height)) * 100;
    const summary = `${diffPixels} pixels differ (${diffPercent.toFixed(3)} %, allowed ${maxDiffPercent} %)`;
    if (diffPercent > maxDiffPercent) {
        return fail(`screenshot doesn't match golden ${goldenPath}: ${summary}`, diff, diffPixels, diffPercent);
    }
    return { pass: true, updated: false, diffPixels, diffPercent, goldenPath, message: summary };
}

/** compareGolden, but throws with the message when it doesn't match (for scenarios). */
export async function expectGolden(image: Buffer, opts: GoldenOptions): Promise<GoldenResult> {
    const result = await compareGolden(image, opts);
    if (!result.pass) throw new Error(result.message);
    return result;
}
