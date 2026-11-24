// IS-008 (D66): what we draw ourselves with NanoVG takes the screen's fade. borealis fades a screen in or out (200 ms) by
// lowering its views' alpha, and its own drawings use it (View::a()). A color or image we hand to NanoVG at a fixed
// strength stays fully visible while the rest of the screen fades: the reader's page hung over the details screen without
// its background, then vanished in one frame. So in src/, every NanoVG fill or stroke color goes through the fade (a() in
// a borealis view, faded() in StripCanvas, which draws for the reader view), and an image pattern's alpha is not a number.

import { readdirSync, readFileSync, statSync } from 'node:fs';
import { join, relative } from 'node:path';

import { describe, expect, it } from 'vitest';

const ROOT = join(import.meta.dirname, '../..');
const SRC = join(ROOT, 'src');
const FADES = ['a', 'this->a', 'faded'];

/** Every .cpp / .hpp file under `dir`. */
function sources(dir: string): string[] {
    return readdirSync(dir).flatMap((name) => {
        const path = join(dir, name);
        if (statSync(path).isDirectory()) return sources(path);
        return /\.(cpp|hpp)$/.test(name) ? [path] : [];
    });
}

/** The code with comments and string contents blanked out (same length, so offsets and line numbers stay). */
function codeOnly(text: string): string {
    const blank = (m: string) => m.replace(/[^\n]/g, ' ');
    return text.replace(/\/\*[\s\S]*?\*\/|\/\/[^\n]*|"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*'/g, blank);
}

/** The top-level arguments of the call whose "(" is at `open`, trimmed. */
function callArgs(code: string, open: number): string[] {
    const args: string[] = [];
    let depth = 0;
    let start = open + 1;
    for (let i = open; i < code.length; i++) {
        const c = code[i];
        if (c === '(' || c === '[' || c === '{') depth++;
        else if (c === ')' || c === ']' || c === '}') {
            depth--;
            if (depth === 0) {
                args.push(code.slice(start, i).trim());
                return args;
            }
        } else if (c === ',' && depth === 1) {
            args.push(code.slice(start, i).trim());
            start = i + 1;
        }
    }
    return args;
}

/** "file:line: what's wrong" for each fill, stroke or image pattern in `text` that doesn't take the fade. */
export function unfaded(text: string, file = 'code'): string[] {
    const code = codeOnly(text);
    const lineOf = (at: number) => code.slice(0, at).split('\n').length;
    const found: string[] = [];
    for (const m of code.matchAll(/\bnvg(?:Fill|Stroke)(?:Color|Paint)\s*\(/g)) {
        const arg = callArgs(code, (m.index ?? 0) + m[0].length - 1)[1] ?? '';
        const head = /^([\w:>-]+)\s*\(/.exec(arg)?.[1] ?? '';
        if (!FADES.includes(head) && head !== 'nvgImagePattern') {
            found.push(`${file}:${lineOf(m.index ?? 0)}: ${m[0].replace(/\s*\($/, '')}(${arg}) without a() / faded()`);
        }
    }
    for (const m of code.matchAll(/\bnvgImagePattern\s*\(/g)) {
        const args = callArgs(code, (m.index ?? 0) + m[0].length - 1);
        const alpha = args.at(-1) ?? '';
        if (/^[-+]?[\d.]+f?$/.test(alpha)) {
            found.push(`${file}:${lineOf(m.index ?? 0)}: nvgImagePattern with a fixed alpha (${alpha}), not the fade`);
        }
    }
    return found;
}

describe('our own drawing takes the screen fade (IS-008, D66)', () => {
    it('every NanoVG fill / stroke in src/ goes through the fade', () => {
        const found = sources(SRC).flatMap((path) =>
            unfaded(readFileSync(path, 'utf8'), relative(ROOT, path).replace(/\\/g, '/')));
        expect(found, 'wrap the color in a() (a borealis view) or faded() (StripCanvas), and give an image pattern the ' +
            'view\'s getAlpha() (D66)').toEqual([]);
    });

    it('tells faded from fixed', () => {
        expect(unfaded('nvgStrokeColor(vg, a(nvgRGB(150, 150, 150)));')).toEqual([]);
        expect(unfaded('nvgFillColor(vg, faded(kPlaceholder));\nnvgFillColor(vg, this->a(c));')).toEqual([]);
        expect(unfaded('nvgFillPaint(vg, nvgImagePattern(vg, x, y, w, h, 0, tex, alpha));')).toEqual([]);
        expect(unfaded('nvgFillPaint(vg, a(nvgLinearGradient(vg, 0, 0, 1, 1, c1, c2)));')).toEqual([]);
        expect(unfaded('// nvgFillColor(vg, kPlaceholder);\nconst char* s = "nvgFillColor(vg, x)";')).toEqual([]);

        expect(unfaded('\nnvgStrokeColor(vg, nvgRGB(150, 150, 150));')).toEqual(
            ['code:2: nvgStrokeColor(nvgRGB(150, 150, 150)) without a() / faded()']);
        expect(unfaded('nvgFillColor(vg, kPlaceholder);')).toHaveLength(1);
        expect(unfaded('nvgFillPaint(vg, nvgImagePattern(vg, tx, ty, tw, th, 0,\n    textures_[i], 1.0f));')).toEqual(
            ['code:1: nvgImagePattern with a fixed alpha (1.0f), not the fade']);
        expect(unfaded('nvgFillPaint(vg, paint);')).toHaveLength(1);
    });
});
