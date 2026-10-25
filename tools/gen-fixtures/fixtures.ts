// The fixture manga set (docs/TESTING.md "Fixture manga"). tools/gen-fixtures/gen-fixtures.ts turns this into folders for
// Suwayomi's Local Source. Tests import it too, to know what the server should report.
// Adding a series: give it the next free `code` (1..31, it's printed in the page code) and run the generator twice to
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
            description: 'Fixture series for suwayomi-nx tests: 3 chapters of 10 JPEG pages, 1000x1500.',
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
