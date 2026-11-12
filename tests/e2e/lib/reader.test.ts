// The reader steps' pure parts (WP-13b): the code nearest the screen's middle line.
import { describe, expect, it } from 'vitest';

import type { FoundPageCode } from './pagecode.ts';
import { middleCode } from './reader.ts';

const code = (segment: number, y: number): FoundPageCode =>
    ({ series: 3, chapter: 1, page: 1, segment, x: 640, y, pitch: 10 });

describe('middleCode', () => {
    it('picks the code nearest the middle line', () => {
        expect(middleCode([code(0, 100), code(1, 380), code(2, 700)], 720)?.segment).toBe(1);
        expect(middleCode([code(4, 50), code(5, 600)], 720)?.segment).toBe(5);
    });
    it('is null without codes', () => {
        expect(middleCode([], 720)).toBeNull();
    });
});
