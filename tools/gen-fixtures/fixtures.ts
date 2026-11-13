// The fixture manga set (docs/TESTING.md "Fixture manga"). tools/gen-fixtures/gen-fixtures.ts turns this into folders for
// Suwayomi's Local Source. Tests import it too, to know what the server should report.
// Adding a series: give it the next free `code` (1..31, it's printed in the page code; 8 is Epsilon Weekly) and run the generator twice to
// check the output is the same both times.

import type { Rgb } from './raster.ts';

export type ImageFormat = 'jpeg' | 'png' | 'webp';
export type BrokenKind = 'corrupt' | 'empty' | 'tiny';

/** details.json as Suwayomi's Local Source reads it (MangaDetails.kt). status: 0 unknown, 1 ongoing, 2 completed. */
export interface MangaDetails {
    title: string;
    author: string;
    artist: string;
    description: string;
    genre: string[];
    status: number;
}

export interface SeriesDef {
    code: number; // the page code's "series" field, 1..31
    title: string; // folder name in local/, which Local Source shows as the title
    label: string; // name printed on the pages (ASCII: the pixel font has no Japanese)
    format: ImageFormat;
    width: number;
    height: number;
    chapters: number;
    pages: number; // per chapter
    background: Rgb; // light colours only, so they never look like a page-code marker
    spreads?: { chapter: number; page: number }[]; // these pages are twice as wide: a double-page spread
    segmentHeight?: number; // tall strips: a page code every segmentHeight px, with the segment number in it
    details?: MangaDetails; // written as details.json
    cover?: boolean; // write cover.jpg (without one, Local Source copies the first page in as the cover)
    broken?: Record<number, BrokenKind>; // page number -> how that page is broken
}

/** Folder name of chapter n. Local Source reads the number from it ("Chapter 12" -> 12) and sorts by it. */
export function chapterFolder(chapter: number): string {
    return `Chapter ${chapter}`;
}

/** File name of page n (1-based), zero-padded so name order = page order. */
export function pageFile(page: number, format: ImageFormat): string {
    return `${String(page).padStart(3, '0')}.${format === 'jpeg' ? 'jpg' : format}`;
}

export const SERIES: readonly SeriesDef[] = [
    {
        code: 1, title: 'Alpha', label: 'ALPHA', format: 'jpeg', width: 1000, height: 1500, chapters: 3, pages: 10,
        background: [255, 244, 214], cover: true,
        details: {
            title: 'Alpha',
            author: 'SNX Fixture Author',
            artist: 'SNX Fixture Artist',
            // Long on purpose (WP-10): the details screen shows five lines of it and opens the rest with a tap.
            description: 'Fixture series for suwayomi-nx tests: 3 chapters of 10 JPEG pages, 1000x1500. Every page shows its ' +
                'series, chapter and page number in big letters, and a colour code in its top-right corner that the tests ' +
                'read back from screenshots. This description is long on purpose: the details screen shows its first ' +
                'five lines and opens the rest with a tap, so the tests can check both. It ends here.',
            genre: ['Action', 'Test'],
            status: 1,
        },
    },
    {
        code: 2, title: 'Beta RTL', label: 'BETA RTL', format: 'png', width: 1000, height: 1500, chapters: 2, pages: 8,
        background: [220, 236, 255], spreads: [{ chapter: 1, page: 4 }],
    },
    {
        code: 3, title: 'Gamma Webtoon', label: 'GAMMA', format: 'webp', width: 800, height: 12000, chapters: 2, pages: 3,
        background: [226, 247, 226], segmentHeight: 1500,
    },
    {
        code: 4, title: 'Delta Big', label: 'DELTA', format: 'jpeg', width: 600, height: 900, chapters: 300, pages: 2,
        background: [240, 230, 250],
    },
    {
        code: 5, title: '日本語タイトル テスト', label: 'NIHONGO', format: 'jpeg', width: 1000, height: 1500, chapters: 1, pages: 5,
        background: [255, 230, 230],
    },
    {
        // Page 1 is corrupt on purpose: Local Source copies it in as the cover, so the cover is broken too (fallback art).
        code: 6, title: 'Broken', label: 'BROKEN', format: 'jpeg', width: 1000, height: 1500, chapters: 1, pages: 5,
        background: [235, 235, 235], broken: { 1: 'corrupt', 2: 'empty', 3: 'tiny' },
    },
];

/** Fixed modification times, because Local Source uses them: a chapter folder's time becomes the chapter's upload date. */
export const EPOCH_MS = Date.UTC(2026, 0, 1);

export function seriesTime(s: SeriesDef): Date {
    return new Date(EPOCH_MS + s.code * 86_400_000);
}

export function chapterTime(s: SeriesDef, chapter: number): Date {
    return new Date(EPOCH_MS + s.code * 86_400_000 + chapter * 60_000);
}

/**
 * The "Big shelf" (WP-09): many small manga, so the library grid has something to scroll, page (the app asks for 60 at a
 * time) and recycle. One chapter of one page each; that page is also the cover (no cover.jpg). They all share code 7: they
 * only test the library, never the reader. The seed puts them in their own category, so Default stays small.
 */
export const SHELF_COUNT = 150;
export const SHELF_CODE = 7;
export const SHELF: readonly SeriesDef[] = Array.from({ length: SHELF_COUNT }, (_, i) => {
    const n = i + 1;
    return {
        code: SHELF_CODE,
        title: `Shelf ${String(n).padStart(3, '0')}`,
        label: `SHELF ${n}`,
        format: 'jpeg' as const,
        width: 400,
        height: 600,
        chapters: 1,
        pages: 1,
        // Light colours that differ from one cover to the next, so a screenshot shows which cover is where.
        background: [200 + ((n * 37) % 56), 200 + ((n * 71) % 56), 200 + ((n * 13) % 56)] as Rgb,
    };
});

/**
 * Epsilon Weekly (WP-15): the manga for "a library update finds a new chapter". It is NOT in ALL_SERIES, and the generator writes
 * it to a folder of its own (next to the pristine fixtures, not inside them): `reset` copies the whole pristine folder into the
 * server, and the server numbers manga in folder order, so Epsilon in there would come before Gamma and shift every later
 * manga and chapter id. The seed copies it in as a second step instead (manga 157, chapters 460-461), so every older id stays.
 * Chapters 1-2 are installed by the seed. Chapter 3 is held back: addNewChapter() in tests/e2e/lib/suwayomi.ts copies it in
 * later (the "new chapter"), removeNewChapter() takes it out again.
 */
export const EPSILON_PUBLISHED_CHAPTERS = 2;
export const EPSILON_HELD_BACK_CHAPTER = 3;
export const EPSILON: SeriesDef = {
    code: 8, title: 'Epsilon Weekly', label: 'EPSILON', format: 'jpeg', width: 1000, height: 1500,
    chapters: EPSILON_HELD_BACK_CHAPTER, pages: 3, // the generator writes all 3 chapters
    background: [255, 236, 210],
    details: {
        title: 'Epsilon Weekly',
        author: 'SNX Fixture Author',
        artist: 'SNX Fixture Artist',
        description: 'Fixture series for suwayomi-nx tests: chapters 1 and 2 are there from the start, chapter 3 is held back ' +
            'and added later, to test "a library update found a new chapter".',
        genre: ['Test'],
        status: 1,
    },
};
/** The series the generator writes into the separate "extra" folder (see EPSILON). */
export const EXTRA_SERIES: readonly SeriesDef[] = [EPSILON];

/** Everything the generator writes and the seed adds to the library. */
export const ALL_SERIES: readonly SeriesDef[] = [...SERIES, ...SHELF];
