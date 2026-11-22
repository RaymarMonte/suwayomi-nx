// IS-004 (D64): every full screen paints its own background. borealis's screen change fades only the new screen in, and
// while it fades the screen under it is drawn at full strength; a screen with no background of its own lets the old one
// show through and then vanish in one frame. With the theme's background on the layout's root the change is a clean
// cross-fade. The reader builds its screen in code and paints its own background (ReaderActivity::applyLook).

import { readdirSync, readFileSync } from 'node:fs';
import { join } from 'node:path';

import { describe, expect, it } from 'vitest';

const LAYOUTS = join(import.meta.dirname, '../../resources/xml/activity');
const BACKGROUND = '@theme/brls/background';

/** The root element's backgroundColor in a borealis layout, or null when it has none. */
export function rootBackground(xml: string): string | null {
    const body = xml.replace(/<!--[\s\S]*?-->/g, '').replace(/<\?[\s\S]*?\?>/g, '');
    const root = /<([A-Za-z_][\w:.-]*)([^>]*)>/.exec(body);
    if (!root) return null;
    const attr = /\sbackgroundColor\s*=\s*"([^"]*)"/.exec(root[2]);
    return attr ? attr[1] : null;
}

describe('screen backgrounds (IS-004, D64)', () => {
    it('every screen layout paints the theme background on its root', () => {
        const files = readdirSync(LAYOUTS).filter((f) => f.endsWith('.xml'));
        expect(files.length).toBeGreaterThanOrEqual(13);
        const missing = files.filter((f) => rootBackground(readFileSync(join(LAYOUTS, f), 'utf8')) !== BACKGROUND);
        expect(missing, `add backgroundColor="${BACKGROUND}" to the root of resources/xml/activity/<file> (D64)`).toEqual([]);
    });

    it('reads only the root, past comments', () => {
        expect(rootBackground(`<!-- <brls:Box backgroundColor="x"> -->\n<brls:AppletFrame\n    backgroundColor="${BACKGROUND}"\n    footerHidden="false">`))
            .toBe(BACKGROUND);
        expect(rootBackground('<brls:AppletFrame footerHidden="false">\n  <brls:Box backgroundColor="#fff"/>')).toBeNull();
        expect(rootBackground('<?xml version="1.0"?><brls:AppletFrame>')).toBeNull();
    });
});
