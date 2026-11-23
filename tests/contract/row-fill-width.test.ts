// IS-006 (D65): a view that fills the rest of a row (grow) and shows text (a Label, or a box with a Label in it) starts at
// width 0. With width "auto" it starts at its content's width, and borealis measures an EMPTY label as wide as all the room
// it's offered: the manga screen's chapter count was empty while the chapters loaded and squeezed Sort / Show / Refresh
// into the corner. A long text did the same. With width="0" the view takes only the room its row's other views leave (a
// long text wraps or ends in "..."), and when everything fits the layout is the same as before.
// Not covered: growing Buttons (fixed words that share a row on purpose, e.g. setup's three) and boxes of Buttons.

import { readdirSync, readFileSync } from 'node:fs';
import { join, relative } from 'node:path';

import { describe, expect, it } from 'vitest';

const XML_ROOT = join(import.meta.dirname, '../../resources/xml');

interface Tag {
    name: string;
    attrs: Record<string, string>;
    line: number;
    showsText: boolean; // a Label, or has one inside
}

function attributes(text: string): Record<string, string> {
    const attrs: Record<string, string> = {};
    for (const m of text.matchAll(/([\w:.-]+)\s*=\s*"([^"]*)"/g)) attrs[m[1]] = m[2];
    return attrs;
}

/** Does this element lay its children out in a row? (A borealis Box is a row unless axis="column".) */
function isRow(tag: Tag): boolean {
    return tag.name === 'brls:Box' && (tag.attrs.axis ?? 'row') === 'row';
}

/**
 * The views in a borealis layout that grow inside a row and show text, but start at their content's width (no width, or
 * "auto"): "<id or tag> (line N)" each, in the order their elements end.
 */
export function autoWidthFillers(xml: string): string[] {
    // Comments become blank lines of the same count, so line numbers stay right.
    const body = xml.replace(/<!--[\s\S]*?-->|<\?[\s\S]*?\?>/g, (c) => c.replace(/[^\n]/g, ''));
    const found: string[] = [];
    const stack: Tag[] = [];
    const finish = (tag: Tag) => {
        const parent = stack.at(-1);
        if (parent && tag.showsText) parent.showsText = true;
        const grow = Number(tag.attrs.grow ?? 0);
        const width = tag.attrs.width ?? 'auto';
        if (parent && isRow(parent) && grow > 0 && tag.showsText && !/^\d+(\.\d+)?$/.test(width)) {
            found.push(`${tag.attrs.id ?? tag.name} (line ${tag.line})`);
        }
    };
    for (const m of body.matchAll(/<(\/?)([\w:.-]+)([^>]*?)(\/?)>/g)) {
        const [, closing, name, rest, selfClosing] = m;
        if (closing) {
            const tag = stack.pop();
            if (tag) finish(tag);
            continue;
        }
        const tag: Tag = {
            name, attrs: attributes(rest), line: body.slice(0, m.index).split('\n').length, showsText: name === 'brls:Label',
        };
        if (selfClosing) finish(tag);
        else stack.push(tag);
    }
    return found;
}

function layouts(dir: string): string[] {
    return readdirSync(dir, { withFileTypes: true }).flatMap((e) =>
        e.isDirectory() ? layouts(join(dir, e.name)) : e.name.endsWith('.xml') ? [join(dir, e.name)] : []);
}

describe('row fill width (IS-006, D65)', () => {
    it('every view that grows inside a row starts at width 0', () => {
        const files = layouts(XML_ROOT);
        expect(files.length).toBeGreaterThanOrEqual(16);
        const offenders = files.flatMap((f) =>
            autoWidthFillers(readFileSync(f, 'utf8')).map((v) => `${relative(XML_ROOT, f)}: ${v}`));
        expect(offenders, 'give these width="0" (keep grow): an "auto" width starts at the content, and an empty or long '
            + 'label then squeezes the row\'s buttons (D65)').toEqual([]);
    });

    it('looks only at views that show text and grow in a row', () => {
        const xml = [
            '<brls:Box axis="column">',
            '  <!-- <brls:Label grow="1.0" /> -->',
            '  <brls:Box id="body" width="auto" grow="1.0" axis="column"><brls:Label id="inColumn" grow="1.0" /></brls:Box>',
            '  <brls:Box id="bar" axis="row">',
            '    <brls:Label id="fill" width="auto" grow="1.0" />',
            '    <brls:Label id="none" grow="1.0" />',
            '    <brls:Label id="ok" width="0" grow="1.0" />',
            '    <brls:Button id="button" grow="1.0" text="Fixed words" />',
            '    <brls:Box id="buttons" grow="1.0"><brls:Button id="b1" /></brls:Box>',
            '  </brls:Box>',
            '  <brls:Box id="plain">',
            '    <brls:Box id="inner" grow="1"><brls:Label id="deep" grow="1.0" width="auto" /></brls:Box>',
            '  </brls:Box>',
            '</brls:Box>',
        ].join('\n');
        expect(autoWidthFillers(xml)).toEqual(['fill (line 5)', 'none (line 6)', 'deep (line 12)', 'inner (line 12)']);
    });
});
