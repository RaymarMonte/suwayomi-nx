// The test source's content (WP-23, D55, docs/TESTING.md "Test source"): the catalogs our test extension reads, the page sets,
// the extensions in the store and the store index. tools/gen-fixtures/gen-test-source.ts writes it all into
// .test-server/web/ (served on http://127.0.0.1:4569); tests import it to know what the server should report.
//
// The Java side (tools/test-source/src/snx/testsource/) reads these catalogs. Its filter and sort rules are written twice:
// in Filters.java and in selectManga() below. Change both together (the live check tools/test-server/test-source.test.ts
// compares them).
// Every value here is fixed: same input, same bytes on every run (no clock, no randomness).

import { createHash } from 'node:crypto';
import type { SeriesDef } from './fixtures.ts';

export const WEB_PORT = 4569;
/** Where jwebserver serves .test-server/web/. Loopback on the Windows PC: only the test server reaches it, never the Switch. */
export const WEB_ORIGIN = `http://127.0.0.1:${WEB_PORT}`;
export const SITE_URL = `${WEB_ORIGIN}/site`;
export const STORE_URL = `${WEB_ORIGIN}/store`;
export const STORE_INDEX_URL = `${STORE_URL}/index.json`;
/** Manga per page in popular, latest and search (en: 25 + 25 + 10). */
export const PAGE_SIZE = 25;

export type Status = 'ongoing' | 'completed' | 'on hiatus' | 'unknown';
export type MangaType = 'Manga' | 'Manhwa' | 'Webtoon';
export const GENRES = ['Action', 'Comedy', 'Drama', 'Fantasy', 'Romance', 'Sci-Fi', 'Long Strip'] as const;
export type Genre = (typeof GENRES)[number];
export const TYPES: readonly MangaType[] = ['Manga', 'Manhwa', 'Webtoon'];

// ---- the filter list (Filters.java builds the same, in this order) ----
export const STATUS_OPTIONS = ['Any', 'Ongoing', 'Completed', 'On hiatus'] as const;
/** The Status filter's options as catalog values (index 0 "Any" = no filter). */
export const STATUS_VALUES: readonly (Status | null)[] = [null, 'ongoing', 'completed', 'on hiatus'];
export const SORTS = ['Popularity', 'Title', 'Latest chapter'] as const;
export const SORT = { popularity: 0, title: 1, latest: 2 } as const;
/** Each filter's position in the list: tests address filters by these (FilterChangeInput.position). */
export const FILTER = { header: 0, author: 1, status: 2, separator: 3, colour: 4, genres: 5, type: 6, sort: 7 } as const;
/** The GraphQL type of each filter, by position. */
export const FILTER_TYPES = ['HeaderFilter', 'TextFilter', 'SelectFilter', 'SeparatorFilter', 'CheckBoxFilter', 'GroupFilter',
    'GroupFilter', 'SortFilter'] as const;
export const FILTER_NAMES = ['Filters also work without a search text', 'Author', 'Status', '', 'In colour', 'Genres', 'Type', 'Sort by'] as const;

// ---- catalogs ----
export interface CatalogChapter {
    id: string; // the chapter's key inside its manga ("1", "2", ...): the chapter url is /chapter/<manga key>/<id>
    number: number;
    name: string;
    scanlator: string | null;
    date: number; // upload date, ms since 1970 (fixed)
    pages: string[]; // image URLs on the test site
    heldBack?: true; // shown only when control.json's extraChapters names the manga
}

export interface CatalogManga {
    key: string; // "en-01": the manga url is /manga/<key>
    title: string;
    author: string;
    artist: string;
    status: Status;
    genres: Genre[];
    type: MangaType; // not part of the genre text (a "Webtoon" tag would make the app open a strip, D49)
    colour: boolean;
    description: string;
    rank: number; // popularity, 1 = most popular
    cover: string | null; // cover image URL; null = no cover
    chapters: CatalogChapter[]; // oldest first (the source lists them newest first)
}

export interface Catalog {
    id: string; // the file is site/catalog-<id>.json
    source: string; // source name
    lang: string;
    manga: CatalogManga[];
}

/**
 * Page sets: the images chapters point to. Four manga have pages of their own (new page-code series 9-12, after the Local
 * fixtures' 1-8); everyone else shares the 2-chapter set 13, which keeps the generator fast. A shared page prints
 * "SNX SHARED CH 1/2", not the manga's own chapter number. Covers use series 14 with the cover number (see coverNumber) in
 * the code's chapter field, so every cover can be told apart. pagecode.ts holds series 1..31 and chapters 0..1023.
 */
export const PAGE_SETS = {
    comet: { code: 9, title: 'comet-courier', label: 'COMET COURIER', format: 'jpeg', width: 1000, height: 1500, chapters: 3, pages: 6,
        background: [255, 240, 220] },
    strip: { code: 10, title: 'skyline-strip', label: 'SKYLINE STRIP', format: 'webp', width: 800, height: 12000, chapters: 2, pages: 3,
        background: [225, 240, 255], segmentHeight: 1500 },
    png: { code: 11, title: 'pressed-flowers', label: 'PRESSED FLOWERS', format: 'png', width: 1000, height: 1500, chapters: 1, pages: 4,
        background: [235, 252, 235] },
    ja: { code: 12, title: 'hoshi-no-tabibito', label: 'HOSHI NO TABIBITO', format: 'jpeg', width: 1000, height: 1500, chapters: 2,
        pages: 5, background: [255, 235, 245] },
    shared: { code: 13, title: 'shared', label: 'SNX SHARED', format: 'jpeg', width: 800, height: 1200, chapters: 2, pages: 3,
        background: [242, 240, 228] },
} as const satisfies Record<string, SeriesDef>;
export const COVER_CODE = 14;
export const COVER_SIZE = { width: 600, height: 900 } as const;

/** Folder and file of one page set image under site/pages/ (no spaces: they'd need escaping in URLs). */
export function pagePath(set: SeriesDef, chapter: number, page: number): string {
    const ext = set.format === 'jpeg' ? 'jpg' : set.format;
    return `pages/${set.title}/c${chapter}/${String(page).padStart(3, '0')}.${ext}`;
}

function pageUrls(set: SeriesDef, chapter: number): string[] {
    return Array.from({ length: set.pages }, (_, i) => `${SITE_URL}/${pagePath(set, chapter, i + 1)}`);
}

/** Fixed dates: the newest chapters are spread over the days after this, one week between a manga's chapters. */
export const TEST_SOURCE_EPOCH_MS = Date.UTC(2026, 1, 1);
const DAY = 86_400_000, WEEK = 7 * DAY;

const SIX_GENRES: readonly Genre[] = GENRES.slice(0, 6);
const STATUS_CYCLE: readonly Status[] = ['ongoing', 'completed', 'on hiatus', 'unknown'];
const SCANLATORS: readonly (string | null)[] = ['SNX Scans', 'Moonlight Group', null];
const SUBTITLES = ['The Beginning', 'Old Friends', 'A Long Night', 'Crossroads', 'Homecoming'];

/** The parts of a manga that the plain rules below don't decide. */
interface Overrides {
    title?: string;
    type?: MangaType;
    colour?: boolean;
    genres?: Genre[];
    chapters?: number; // published chapters (default from the rules)
    heldBack?: boolean; // one more chapter, held back (control.json extraChapters)
    pageSet?: SeriesDef; // own pages instead of the shared set
    cover?: 'none' | 'missing';
    missingPage?: { chapter: number; page: number }; // this page's URL answers 404
    note?: string; // added to the description
}

interface CatalogPlan {
    id: string;
    source: string;
    lang: string;
    prefix: string; // manga keys are <prefix>-01, -02, ...
    titles: readonly string[];
    authors: readonly string[];
    overrides?: Record<number, Overrides>; // by manga number, 1-based
}

/**
 * The rules for manga i (0-based) of n. Popularity ranks and newest-chapter days are permutations (i * prime mod n), so popular
 * and latest give two different orders and no ties. Genres, types, statuses and colour cycle so that every filter option
 * matches some manga and not all of them (the unit tests check it).
 */
function buildCatalog(plan: CatalogPlan): Catalog {
    const n = plan.titles.length;
    const rankStep = [37, 31, 29, 23, 19, 17, 13, 11, 7, 5, 3].find((p) => gcd(p, n) === 1 && p < n) ?? 1;
    const dayStep = [23, 19, 17, 13, 11, 7, 5, 3, 2].find((p) => gcd(p, n) === 1 && p < n && p !== rankStep) ?? 1;
    const manga = plan.titles.map((baseTitle, i): CatalogManga => {
        const o = plan.overrides?.[i + 1] ?? {};
        const key = `${plan.prefix}-${String(i + 1).padStart(2, '0')}`;
        const title = o.title ?? baseTitle;
        const type: MangaType = o.type ?? (i % 5 === 3 ? 'Manhwa' : i % 5 === 4 ? 'Webtoon' : 'Manga');
        const colour = o.colour ?? (type !== 'Manga' || i % 7 === 0);
        const genres = o.genres ?? unique([SIX_GENRES[i % 6], SIX_GENRES[(i * 5 + 2) % 6], ...(i % 5 === 0 ? [SIX_GENRES[(i + 3) % 6]] : [])]);
        const status = STATUS_CYCLE[i % 4];
        const published = o.chapters ?? 1 + ((i * 7) % 12);
        const newest = TEST_SOURCE_EPOCH_MS + ((i * dayStep) % n) * DAY + i * 60_000;
        const set = o.pageSet ?? PAGE_SETS.shared;
        const chapters: CatalogChapter[] = [];
        for (let k = 1; k <= published + (o.heldBack ? 1 : 0); k++) {
            const setChapter = o.pageSet ? k : ((k - 1) % set.chapters) + 1;
            const pages = pageUrls(set, setChapter);
            if (o.missingPage?.chapter === k) pages[o.missingPage.page - 1] = `${SITE_URL}/pages/missing/${key}-c${k}-p${o.missingPage.page}.jpg`;
            chapters.push({
                id: String(k),
                number: k,
                name: k % 4 === 0 ? `Chapter ${k}: ${SUBTITLES[(i + k) % SUBTITLES.length]}` : `Chapter ${k}`,
                scanlator: SCANLATORS[k % 3],
                date: newest + (k - published) * WEEK, // the held-back chapter is a week newer than the newest
                pages,
                ...(k > published ? { heldBack: true as const } : {}),
            });
        }
        const cover = o.cover === 'none' ? null : o.cover === 'missing' ? `${SITE_URL}/covers/missing/${key}.jpg` : `${SITE_URL}/covers/${key}.jpg`;
        return {
            key,
            title,
            author: plan.authors[(i * 3) % plan.authors.length],
            artist: plan.authors[(i * 7 + 1) % plan.authors.length],
            status,
            genres,
            type,
            colour,
            description: `Synthetic test manga ${key} from the ${plan.source} source (${plan.lang}): ${type.toLowerCase()}, ` +
                `${colour ? 'in colour' : 'black and white'}, ${status}.${o.note ? ' ' + o.note : ''} Nothing here is real.`,
            rank: ((i * rankStep) % n) + 1,
            cover,
            chapters,
        };
    });
    return { id: plan.id, source: plan.source, lang: plan.lang, manga };
}

function gcd(a: number, b: number): number {
    return b === 0 ? a : gcd(b, a % b);
}

function unique<T>(list: T[]): T[] {
    return [...new Set(list)];
}

const EN_FIRST = ['Crimson', 'Silent', 'Iron', 'Paper', 'Hidden', 'Golden', 'Midnight', 'Wandering', 'Glass', 'Thunder'];
const EN_SECOND = ['Harbor', 'Garden', 'Knight', 'Lantern', 'Valley', 'Circuit'];
const EN_AUTHORS = ['Aiko Mori', 'Ben Carter', 'Chloe Martin', 'Daniel Okafor', 'Elena Rossi', 'Farid Haddad', 'Grace Lin',
    'Hugo Schmidt', 'Ines Duarte', 'Jonas Berg'];

/** A title of about 120 characters (layout tests: wrapping, ellipsis). */
export const LONG_TITLE = 'The Extraordinarily Long and Winding Chronicle of the Lighthouse Keeper Who Collected Every Lost ' +
    'Letter Washed Ashore';

/** The manga with a special job, by key (tests use these names instead of bare keys). */
export const SPECIAL = {
    ownJpeg: 'en-01', // "Comet Courier": own JPEG pages, a held-back chapter 3 (extraChapters), the server download test
    webtoon: 'en-02', // genre Long Strip, type Webtoon, 800x12000 WebP strips: D49 opens it as a strip
    ownPng: 'en-03', // own PNG pages
    longTitle: 'en-04',
    noCover: 'en-05',
    coverMissing: 'en-06', // its cover URL answers 404
    noChapters: 'en-07',
    pageMissing: 'en-08', // chapter 1 page 2 answers 404
    ownJa: 'ja-01', // own JPEG pages in the Japanese source
} as const;

export const CATALOG_EN: Catalog = buildCatalog({
    id: 'en', source: 'SNX Test', lang: 'en', prefix: 'en', authors: EN_AUTHORS,
    titles: Array.from({ length: 60 }, (_, i) => `${EN_FIRST[i % 10]} ${EN_SECOND[Math.floor(i / 10)]}`),
    overrides: {
        1: { title: 'Comet Courier', chapters: 2, heldBack: true, pageSet: PAGE_SETS.comet, note: 'Chapter 3 is held back.' },
        2: { title: 'Skyline Strip', type: 'Webtoon', colour: true, genres: ['Romance', 'Long Strip'], chapters: 2, pageSet: PAGE_SETS.strip,
            note: 'Tall strips, read as a webtoon.' },
        3: { title: 'Pressed Flowers', chapters: 1, pageSet: PAGE_SETS.png },
        4: { title: LONG_TITLE },
        5: { title: 'Blank Cover Society', cover: 'none', note: 'It has no cover.' },
        6: { title: 'Lost Cover Mystery', cover: 'missing', note: 'Its cover answers 404.' },
        7: { title: 'Coming Soon Saga', chapters: 0, note: 'It has no chapters yet.' },
        8: { title: 'Missing Page Mansion', chapters: 2, missingPage: { chapter: 1, page: 2 }, note: 'Chapter 1 page 2 answers 404.' },
        23: { title: 'The Last Comet' },
        47: { title: 'Comet Over the Bay' },
    },
});

export const CATALOG_JA: Catalog = buildCatalog({
    id: 'ja', source: 'SNX Test', lang: 'ja', prefix: 'ja',
    titles: ['星の旅人', '夜明けの剣', '青い海の物語', '猫の喫茶店', '鉄の心臓', '風の歌', '月光の図書館', '桜の約束'],
    authors: ['森 愛子', '佐藤 健', '高橋 由美', '田中 翔', '伊藤 真央'],
    overrides: { 1: { chapters: 2, pageSet: PAGE_SETS.ja } },
});

export const CATALOG_FR: Catalog = buildCatalog({
    id: 'fr', source: 'SNX Extra', lang: 'fr', prefix: 'fr',
    titles: ['Le Club Comet', "La Forêt d'Argent", 'Les Gardiens du Phare', 'Mon Voisin le Dragon', "L'Atelier des Nuages"],
    authors: ['Camille Durand', 'Hugo Lefèvre', 'Léa Moreau', 'Louis Bernard', 'Manon Petit'],
});

export const CATALOG_MATURE: Catalog = buildCatalog({
    id: 'mature-en', source: 'SNX Mature', lang: 'en', prefix: 'mx',
    titles: ['Velvet Nights', 'Scarlet Contract', 'Paper Garden'],
    // WP-14: mx-03 has SNX Test en-14's title ("Paper Garden"): the same work in two sources, for the same-title warning.
    authors: ['Nora Vance', 'Oscar Reyes', 'Paula Stein'],
});

export const CATALOGS: readonly Catalog[] = [CATALOG_EN, CATALOG_JA, CATALOG_FR, CATALOG_MATURE];

/** The cover's number (1..76, unique across catalogs): it goes into the cover's page code as the chapter. */
export function coverNumber(key: string): number {
    let first = 1;
    for (const c of CATALOGS) {
        const i = c.manga.findIndex((m) => m.key === key);
        if (i >= 0) return first + i;
        first += c.manga.length;
    }
    throw new Error(`no manga ${key}`);
}

export function findManga(key: string): CatalogManga {
    for (const c of CATALOGS) {
        const m = c.manga.find((x) => x.key === key);
        if (m) return m;
    }
    throw new Error(`no manga ${key} in the test source catalogs`);
}

// ---- the rules Filters.java applies, in TypeScript ----
export interface SearchOptions {
    query?: string;
    author?: string;
    status?: Status;
    colour?: boolean;
    genres?: Genre[]; // must have all of these (TriState include)
    notGenres?: Genre[]; // must have none of these (TriState exclude)
    types?: MangaType[]; // any of these; none = all
    sort?: { index: number; ascending: boolean };
}

/** The newest date among the chapters that always show; 0 without chapters. */
export function latestChapterDate(m: CatalogManga): number {
    return m.chapters.filter((c) => !c.heldBack).reduce((a, c) => Math.max(a, c.date), 0);
}

const lower = (s: string | undefined) => (s === undefined || s.trim() === '' ? undefined : s.trim().toLowerCase());

/** The manga a list call shows, in order (the same as Filters.select in Java). */
export function selectManga(catalog: Catalog, call: 'popular' | 'latest' | 'search', o: SearchOptions = {}): CatalogManga[] {
    if (call === 'popular') return sortManga(catalog.manga, SORT.popularity, false);
    if (call === 'latest') return sortManga(catalog.manga, SORT.latest, false);
    const q = lower(o.query), author = lower(o.author);
    const matches = catalog.manga.filter((m) =>
        (q === undefined || m.title.trim().toLowerCase().includes(q)) &&
        (author === undefined || m.author.trim().toLowerCase().includes(author)) &&
        (o.status === undefined || m.status === o.status) &&
        (!o.colour || m.colour) &&
        (o.genres ?? []).every((g) => m.genres.includes(g)) &&
        !(o.notGenres ?? []).some((g) => m.genres.includes(g)) &&
        ((o.types ?? []).length === 0 || (o.types ?? []).includes(m.type)));
    return sortManga(matches, o.sort?.index ?? SORT.popularity, o.sort?.ascending ?? false);
}

/** Sorted ascending by the sort key (ties by key), then reversed for descending. Strings compare by UTF-16 units, like Java. */
function sortManga(list: readonly CatalogManga[], sort: number, ascending: boolean): CatalogManga[] {
    const cmp = (a: string | number, b: string | number) => (a < b ? -1 : a > b ? 1 : 0);
    const key = (m: CatalogManga): string | number =>
        sort === SORT.title ? m.title.trim().toLowerCase() : sort === SORT.latest ? latestChapterDate(m) : -m.rank;
    const sorted = [...list].sort((a, b) => cmp(key(a), key(b)) || cmp(a.key, b.key));
    return ascending ? sorted : sorted.reverse();
}

/** One page (1-based) of a list, as the source sends it. */
export function listPage(list: readonly CatalogManga[], page: number): { keys: string[]; hasNextPage: boolean } {
    const from = Math.min((page - 1) * PAGE_SIZE, list.length);
    const to = Math.min(from + PAGE_SIZE, list.length);
    return { keys: list.slice(from, to).map((m) => m.key), hasNextPage: to < list.length };
}

/** The chapters the source lists for a manga, newest first (held-back ones only with extraChapters). */
export function listedChapters(m: CatalogManga, showHeldBack = false): CatalogChapter[] {
    return m.chapters.filter((c) => showHeldBack || !c.heldBack).reverse();
}

// ---- extensions and the store ----
export type ContentWarning = 'SAFE' | 'MIXED' | 'NSFW';

export interface ExtensionDef {
    pkg: string; // Android package name = the extension's id in the server
    name: string;
    lib: string; // extension lib version: the server accepts 1.3-1.6; versionName = lib + ".<code>"
    contentWarning: ContentWarning;
    versions: readonly number[]; // version codes built into the store folder
    sources: readonly { name: string; lang: string; catalog: Catalog }[];
}

// No package name may start with another's: the server finds a cached icon by prefix, so "snx.testsource" would get
// "snx.testsource.mature"'s icon (WP-14, SUWAYOMI-API.md). The Java class is <pkg>.Main.
export const EXTENSIONS = {
    test: { pkg: 'snx.testsource', name: 'SNX Test', lib: '1.5', contentWarning: 'SAFE', versions: [1, 2],
        sources: [{ name: 'SNX Test', lang: 'en', catalog: CATALOG_EN }, { name: 'SNX Test', lang: 'ja', catalog: CATALOG_JA }] },
    extra: { pkg: 'snx.testextra', name: 'SNX Extra', lib: '1.5', contentWarning: 'SAFE', versions: [1],
        sources: [{ name: 'SNX Extra', lang: 'fr', catalog: CATALOG_FR }] },
    mature: { pkg: 'snx.testmature', name: 'SNX Mature', lib: '1.5', contentWarning: 'NSFW', versions: [1],
        sources: [{ name: 'SNX Mature', lang: 'en', catalog: CATALOG_MATURE }] },
} as const satisfies Record<string, ExtensionDef>;
export const ALL_EXTENSIONS: readonly ExtensionDef[] = [EXTENSIONS.test, EXTENSIONS.extra, EXTENSIONS.mature];

export function versionName(ext: ExtensionDef, code: number): string {
    return `${ext.lib}.${code}`;
}

export function jarFile(ext: ExtensionDef, code: number): string {
    return `${ext.pkg}-v${code}.jar`;
}

/**
 * A source's id, the way Tachiyomi's HttpSource.generateId makes it: MD5 of "<name lower case>/<lang>/<versionId>", the first
 * 8 bytes as a big-endian number, top bit cleared. A decimal string (it doesn't fit a JS number; GraphQL calls it LongString).
 */
export function sourceId(name: string, lang: string, versionId = 1): string {
    const md5 = createHash('md5').update(`${name.toLowerCase()}/${lang}/${versionId}`).digest();
    return (md5.readBigUInt64BE(0) & 0x7fff_ffff_ffff_ffffn).toString();
}

/** The four source ids (checked against the formula by the unit tests). */
export const TEST_SOURCE_IDS = {
    en: '1105946461371740061', // SNX Test (EN)
    ja: '1294398940357362604', // SNX Test (JA)
    fr: '7671842670262530014', // SNX Extra (FR)
    matureEn: '6796814882383758416', // SNX Mature (EN)
} as const;

/** store/index.json. `testVersion` = which SNX Test version the store offers (2 = "an update is out"). */
export function storeIndex(testVersion = 1): object {
    const entry = (ext: ExtensionDef, code: number) => ({
        name: ext.name,
        packageName: ext.pkg,
        resources: {
            // The server takes the jar when there is a jarUrl. The APK isn't there: the address only names the download.
            apkUrl: `${STORE_URL}/apk/${ext.pkg}-v${code}.apk`,
            iconUrl: `${STORE_URL}/icon/${ext.pkg}.png`,
            jarUrl: `${STORE_URL}/${jarFile(ext, code)}`,
        },
        extensionLib: ext.lib,
        versionCode: code,
        versionName: versionName(ext, code),
        contentWarning: ext.contentWarning,
        sources: ext.sources.map((s) => ({ id: sourceId(s.name, s.lang), name: s.name, language: s.lang })),
    });
    if (!(EXTENSIONS.test.versions as readonly number[]).includes(testVersion)) throw new Error(`SNX Test has no version ${testVersion}`);
    return {
        name: 'SNX Test Store',
        badgeLabel: 'SNX',
        signingKey: 'snx-test-store-unsigned',
        contact: { website: `${WEB_ORIGIN}/`, discord: `${WEB_ORIGIN}/discord` },
        extensionList: {
            extensions: [entry(EXTENSIONS.test, testVersion), entry(EXTENSIONS.extra, 1), entry(EXTENSIONS.mature, 1)],
        },
        extensionListUrl: null,
    };
}

/** store/build.json: what test-server.ps1 build-source compiles (one jar per extension and version). */
export function buildList(): object[] {
    return ALL_EXTENSIONS.flatMap((ext) => ext.versions.map((code) => ({
        jar: jarFile(ext, code),
        package: ext.pkg,
        name: ext.name,
        versionCode: code,
        versionName: versionName(ext, code),
        contentWarning: ['SAFE', 'MIXED', 'NSFW'].indexOf(ext.contentWarning), // the manifest's tachiyomix.contentWarning: 0, 1, 2
    })));
}
