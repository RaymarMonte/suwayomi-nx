// Unit tests for the test source's content (WP-23): the catalogs, the store index and the site generator. Host only, fast.
// The live check against the server is tools/test-server/test-source.test.ts.

import { mkdtemp, readFile, rm } from 'node:fs/promises';
import { readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import sharp from 'sharp';
import { describe, expect, it } from 'vitest';
import { findPageCodes } from '../../tests/e2e/lib/pagecode.ts';
import { ALL_SERIES, EXTRA_SERIES } from './fixtures.ts';
import { generateSite, testCoverBytes } from './gen-test-source.ts';
import {
    buildList, CATALOG_EN, CATALOG_FR, CATALOG_JA, CATALOG_MATURE, CATALOGS, COVER_CODE, coverNumber, findManga, GENRES, LONG_TITLE,
    latestChapterDate, listPage, PAGE_SETS, PAGE_SIZE, type SearchOptions, selectManga, sourceId, SORTS, SPECIAL, STATUS_OPTIONS, STATUS_VALUES,
    storeIndex, TEST_SOURCE_IDS, TYPES, WEB_ORIGIN, WEB_PORT,
} from './test-source.ts';

const repoFile = (rel: string) => fileURLToPath(new URL(`../../${rel}`, import.meta.url));

/** Every string in a JSON value that looks like an address. */
function addresses(value: unknown): string[] {
    if (typeof value === 'string') return /^[a-z][a-z0-9+.-]*:\/\//i.test(value) ? [value] : [];
    if (Array.isArray(value)) return value.flatMap(addresses);
    if (value && typeof value === 'object') return Object.values(value).flatMap(addresses);
    return [];
}

describe('test source catalogs', () => {
    it('have the planned sizes, unique keys and unique titles', () => {
        expect(CATALOGS.map((c) => [c.id, c.manga.length])).toEqual([['en', 60], ['ja', 8], ['fr', 5], ['mature-en', 3]]);
        const keys = CATALOGS.flatMap((c) => c.manga.map((m) => m.key));
        expect(new Set(keys).size).toBe(keys.length);
        for (const c of CATALOGS) expect(new Set(c.manga.map((m) => m.title)).size, c.id).toBe(c.manga.length);
        for (const c of CATALOGS) {
            expect(c.manga.map((m) => m.rank).sort((a, b) => a - b), `${c.id} ranks`).toEqual(c.manga.map((_, i) => i + 1));
            const dates = c.manga.map(latestChapterDate);
            expect(new Set(dates).size, `${c.id}: no two manga share a newest-chapter date`).toBe(dates.length);
        }
    });

    it('has every special manga, each the way tests expect it', () => {
        const m = (k: keyof typeof SPECIAL) => findManga(SPECIAL[k]);
        expect(m('ownJpeg').chapters.filter((c) => c.heldBack).map((c) => c.number)).toEqual([3]);
        expect(m('ownJpeg').chapters.every((c) => c.pages.every((p) => p.includes(`/pages/${PAGE_SETS.comet.title}/`)))).toBe(true);
        expect(m('webtoon')).toMatchObject({ type: 'Webtoon', colour: true });
        expect(m('webtoon').genres).toContain('Long Strip');
        expect(m('webtoon').chapters[0].pages[0]).toMatch(/\/pages\/skyline-strip\/c1\/001\.webp$/);
        expect(m('ownPng').chapters[0].pages[0]).toMatch(/\.png$/);
        expect(m('longTitle').title).toBe(LONG_TITLE);
        expect(LONG_TITLE.length).toBeGreaterThanOrEqual(110);
        expect(LONG_TITLE.length).toBeLessThanOrEqual(130);
        expect(m('noCover').cover).toBeNull();
        expect(m('coverMissing').cover).toContain('/covers/missing/');
        expect(m('noChapters').chapters).toEqual([]);
        expect(m('pageMissing').chapters[0].pages[1]).toContain('/pages/missing/');
        expect(m('ownJa').chapters[0].pages[0]).toContain(`/pages/${PAGE_SETS.ja.title}/`);
        // Only the real strip has the genre that makes the app open a strip (D49); "Webtoon" is never a genre.
        expect(CATALOGS.flatMap((c) => c.manga).filter((x) => x.genres.includes('Long Strip')).map((x) => x.key)).toEqual([SPECIAL.webtoon]);
    });

    it('puts "Comet" in 3 en titles and 1 fr title, none in ja or mature', () => {
        const comet = (c: typeof CATALOG_EN) => c.manga.filter((m) => m.title.toLowerCase().includes('comet')).length;
        expect([comet(CATALOG_EN), comet(CATALOG_JA), comet(CATALOG_FR), comet(CATALOG_MATURE)]).toEqual([3, 0, 1, 0]);
    });

    it('avoids the Local fixtures\' words in titles', () => {
        const words = ['alpha', 'beta', 'gamma', 'delta', 'epsilon', 'shelf', 'broken', 'テスト'];
        for (const m of CATALOGS.flatMap((c) => c.manga)) {
            for (const w of words) expect(m.title.toLowerCase(), m.key).not.toContain(w);
        }
        const local = new Set([...ALL_SERIES, ...EXTRA_SERIES].map((s) => s.title));
        expect(CATALOGS.flatMap((c) => c.manga).filter((m) => local.has(m.title))).toEqual([]);
    });

    it('lets every filter option match some en manga but not all', () => {
        const all = CATALOG_EN.manga.length;
        const options: [string, SearchOptions][] = [
            ...STATUS_VALUES.slice(1).map((s): [string, SearchOptions] => [`status ${s}`, { status: s! }]),
            ['colour', { colour: true }],
            ...GENRES.map((g): [string, SearchOptions] => [`genre ${g}`, { genres: [g] }]),
            ...GENRES.map((g): [string, SearchOptions] => [`not genre ${g}`, { notGenres: [g] }]),
            ...TYPES.map((t): [string, SearchOptions] => [`type ${t}`, { types: [t] }]),
            ...[...new Set(CATALOG_EN.manga.map((m) => m.author))].map((a): [string, SearchOptions] => [`author ${a}`, { author: a.split(' ')[1] }]),
        ];
        for (const [name, o] of options) {
            const n = selectManga(CATALOG_EN, 'search', o).length;
            expect(n, `${name}: some`).toBeGreaterThan(0);
            expect(n, `${name}: not all`).toBeLessThan(all);
        }
    });

    it('orders and pages the lists like the source does', () => {
        const popular = selectManga(CATALOG_EN, 'popular');
        expect(popular.map((m) => m.rank)).toEqual(popular.map((_, i) => i + 1));
        expect([1, 2, 3].map((p) => listPage(popular, p).keys.length)).toEqual([25, 25, 10]);
        expect([1, 2, 3].map((p) => listPage(popular, p).hasNextPage)).toEqual([true, true, false]);
        const latest = selectManga(CATALOG_EN, 'latest').map(latestChapterDate);
        expect(latest).toEqual([...latest].sort((a, b) => b - a));
        expect(selectManga(CATALOG_EN, 'latest').map((m) => m.key)).not.toEqual(popular.map((m) => m.key));
        const az = selectManga(CATALOG_EN, 'search', { sort: { index: 1, ascending: true } }).map((m) => m.title.toLowerCase());
        expect(az).toEqual([...az].sort());
        expect(selectManga(CATALOG_EN, 'search').map((m) => m.key)).toEqual(popular.map((m) => m.key)); // no text, no filters
    });

    it('points only at the test site: every address starts with http://127.0.0.1:4569/', () => {
        const all = [...addresses(CATALOGS), ...addresses(storeIndex(1)), ...addresses(storeIndex(2))];
        expect(all.length).toBeGreaterThan(1000);
        expect(all.filter((a) => !a.startsWith(`${WEB_ORIGIN}/`))).toEqual([]);
        const server = JSON.parse(readFileSync(repoFile('tools/test-server/server.json'), 'utf8')) as { webPort: number };
        expect(server.webPort).toBe(WEB_PORT);
    });

    it('uses page-code series the Local fixtures don\'t, and covers that tell manga apart', async () => {
        const codes = [...Object.values(PAGE_SETS).map((s) => s.code), COVER_CODE];
        expect(new Set(codes).size).toBe(codes.length);
        const local = new Set([...ALL_SERIES, ...EXTRA_SERIES].map((s) => s.code));
        for (const c of codes) expect(c > 0 && c <= 31 && !local.has(c), `code ${c}`).toBe(true);
        const m = findManga('ja-03');
        const { data, info } = await sharp(await testCoverBytes(m)).raw().toBuffer({ resolveWithObject: true });
        const found = findPageCodes({ width: info.width, height: info.height, channels: info.channels, data });
        expect(found.map(({ series, chapter, page }) => ({ series, chapter, page }))).toContainEqual({ series: COVER_CODE, chapter: coverNumber(m.key), page: 0 });
        expect(coverNumber('en-01')).toBe(1);
        expect(coverNumber('mx-03')).toBe(76);
    });
});

describe('the extensions and the store', () => {
    it('makes source ids the way Tachiyomi does (the spike\'s test vector)', () => {
        expect(sourceId('SNX Spike', 'en')).toBe('7230610560190231314');
        expect(TEST_SOURCE_IDS).toEqual({
            en: sourceId('SNX Test', 'en'), ja: sourceId('SNX Test', 'ja'), fr: sourceId('SNX Extra', 'fr'), matureEn: sourceId('SNX Mature', 'en'),
        });
    });

    it('writes an index with the fields the server reads, offering v1 normally and v2 as the update', () => {
        const index = storeIndex(1) as { extensionList: { extensions: Record<string, unknown>[] } } & Record<string, unknown>;
        expect(Object.keys(index).sort()).toEqual(['badgeLabel', 'contact', 'extensionList', 'extensionListUrl', 'name', 'signingKey']);
        expect(index.extensionListUrl).toBeNull();
        for (const e of index.extensionList.extensions) {
            expect(Object.keys(e).sort()).toEqual(['contentWarning', 'extensionLib', 'name', 'packageName', 'resources', 'sources',
                'versionCode', 'versionName']);
            expect(Object.keys(e.resources as object).sort()).toEqual(['apkUrl', 'iconUrl', 'jarUrl']);
        }
        const versions = (v: number) => (storeIndex(v) as typeof index).extensionList.extensions.map((e) => [e.packageName, e.versionCode, e.versionName]);
        expect(versions(1)).toEqual([['snx.testsource', 1, '1.5.1'], ['snx.testsource.extra', 1, '1.5.1'], ['snx.testsource.mature', 1, '1.5.1']]);
        expect(versions(2)[0]).toEqual(['snx.testsource', 2, '1.5.2']);
        expect(() => storeIndex(3)).toThrow();
        expect(index.extensionList.extensions.map((e) => e.contentWarning)).toEqual(['SAFE', 'SAFE', 'NSFW']);
        expect(buildList()).toHaveLength(4);
    });

    it('keeps the Java filter list and the TypeScript one the same', () => {
        const java = readFileSync(repoFile('tools/test-source/src/snx/testsource/Filters.java'), 'utf8');
        const array = (name: string) => {
            const m = new RegExp(`static final String\\[\\] ${name} = \\{([^}]*)\\}`).exec(java);
            if (!m) throw new Error(`Filters.java has no ${name}`);
            return [...m[1].matchAll(/"([^"]*)"/g)].map((x) => x[1]);
        };
        expect(array('STATUSES')).toEqual([...STATUS_OPTIONS]);
        expect(array('STATUS_VALUES')).toEqual(STATUS_VALUES.filter((v) => v !== null));
        expect(array('GENRES')).toEqual([...GENRES]);
        expect(array('TYPES')).toEqual([...TYPES]);
        expect(array('SORTS')).toEqual([...SORTS]);
        const source = readFileSync(repoFile('tools/test-source/src/snx/testsource/CatalogSource.java'), 'utf8');
        expect(source).toContain(`static final int PAGE_SIZE = ${PAGE_SIZE};`);
        expect(source).toContain(`"http://127.0.0.1:${WEB_PORT}/site"`);
    });
});

describe('the site generator', () => {
    it('gives the same bytes twice, and an index without a byte-order mark', async () => {
        const dir = await mkdtemp(join(tmpdir(), 'snx-test-source-'));
        try {
            const a = await generateSite(join(dir, 'a'));
            const b = await generateSite(join(dir, 'b'));
            expect(b.sums).toBe(a.sums);
            expect(a.files).toBeGreaterThan(100);
            const index = await readFile(join(dir, 'a', 'store', 'index.json'));
            expect(index[0]).toBe('{'.charCodeAt(0)); // no EF BB BF in front
            expect(JSON.parse(index.toString('utf8'))).toEqual(storeIndex(1));
            const catalog = JSON.parse(await readFile(join(dir, 'a', 'site', 'catalog-en.json'), 'utf8'));
            expect(catalog).toEqual(CATALOG_EN);
            const strip = await sharp(await readFile(join(dir, 'a', 'site', 'pages', 'skyline-strip', 'c1', '001.webp'))).metadata();
            expect(strip).toMatchObject({ format: 'webp', width: 800, height: 12000 });
        } finally {
            await rm(dir, { recursive: true, force: true });
        }
    }, 60_000);
});
