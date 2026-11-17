// Checks the test source (WP-23, D55) on a live, freshly reset and seeded test server: the store and the three extensions,
// browsing, every filter type, details / chapters / pages through the server, a server download, the switch file, an update
// to v2 and back, installing and removing extensions, removing and re-adding the store. Needs the running test server, so it
// only runs when asked:
//   tools/test-server/test-server.sh reset && tools/test-server/test-server.sh start [--auth ui_login]
//   eval "$(tools/test-server/test-server.sh env)"; SNX_LIVE_SERVER=1 npx vitest run tools/test-server/test-source.test.ts
// It leaves the test source as the seed left it (resetTestSource). Manga and chapter rows it made by browsing stay: their ids
// were never fixed (D55). Run it again after bumping the server version (server.json): reset rebuilds the extension.

import sharp from 'sharp';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { findPageCodes, type PageCode } from '../../tests/e2e/lib/pagecode.ts';
import { optionsFromEnv, SuwayomiClient } from '../../tests/e2e/lib/suwayomi.ts';
import {
    addTestStore, clearTestSourceControl, EXTENSIONS, fetchTestExtensions, findSourceManga, hasTestStore, installExtension,
    offerTestSourceUpdate, removeTestStore, resetTestSource, setTestSourceControl, STORE_INDEX_URL, TEST_SOURCE_IDS,
    testExtension, uninstallExtension, updateExtension, withdrawTestSourceUpdate,
} from '../../tests/e2e/lib/test-source.ts';
import {
    CATALOG_EN, CATALOG_FR, CATALOG_JA, CATALOG_MATURE, COVER_CODE, coverNumber, FILTER, FILTER_NAMES, FILTER_TYPES, findManga,
    GENRES, listedChapters, listPage, PAGE_SETS, type SearchOptions, selectManga, SORT, SORTS, STATUS_OPTIONS, TYPES,
} from '../gen-fixtures/test-source.ts';

type FilterChange = Record<string, unknown>;

describe.runIf(process.env.SNX_LIVE_SERVER === '1')('the test source on the live test server', () => {
    const client = new SuwayomiClient(optionsFromEnv());
    const keyOf = (url: string) => url.replace(/^\/manga\//, '');

    async function browse(source: string, type: 'POPULAR' | 'LATEST' | 'SEARCH', page: number, query?: string, filters?: FilterChange[]) {
        const data = await client.query<{ fetchSourceManga: { hasNextPage: boolean; mangas: { id: number; url: string; title: string }[] } }>(
            `mutation B($source: LongString!, $type: FetchSourceMangaType!, $page: Int!, $query: String, $filters: [FilterChangeInput!]) {
                fetchSourceManga(input: { source: $source, type: $type, page: $page, query: $query, filters: $filters }) {
                    hasNextPage mangas { id url title } } }`,
            { source, type, page, query, filters });
        return { keys: data.fetchSourceManga.mangas.map((m) => keyOf(m.url)), mangas: data.fetchSourceManga.mangas,
            hasNextPage: data.fetchSourceManga.hasNextPage };
    }

    /** Every page of a search, as manga keys. */
    async function searchAll(source: string, query: string | undefined, filters: FilterChange[]): Promise<string[]> {
        const keys: string[] = [];
        for (let page = 1; ; page++) {
            const r = await browse(source, 'SEARCH', page, query, filters);
            keys.push(...r.keys);
            if (!r.hasNextPage) return keys;
        }
    }

    const predicted = (o: SearchOptions, catalog = CATALOG_EN) => selectManga(catalog, 'search', o).map((m) => m.key);

    async function mangaWithChapters(id: number) {
        await client.query('mutation F($id: Int!) { fetchMangaAndChapters(input: { id: $id, fetchManga: true, fetchChapters: true }) { manga { id } } }',
            { id });
        const data = await client.query<{ manga: { title: string; author: string; artist: string; description: string; genre: string[];
            status: string; initialized: boolean; chapters: { nodes: { id: number; name: string; chapterNumber: number; scanlator: string | null;
            uploadDate: string; url: string; isDownloaded: boolean }[] } } }>(
            `query M($id: Int!) { manga(id: $id) { title author artist description genre status initialized
                chapters { nodes { id name chapterNumber scanlator uploadDate url isDownloaded } } } }`,
            { id });
        data.manga.chapters.nodes.sort((a, b) => b.chapterNumber - a.chapterNumber); // newest first, like the source lists them
        return data.manga;
    }

    async function imageCodes(path: string): Promise<{ status: number; contentType: string; codes: PageCode[] }> {
        const res = await client.get(path);
        if (res.status !== 200) return { status: res.status, contentType: res.contentType, codes: [] };
        const { data, info } = await sharp(res.body).raw().toBuffer({ resolveWithObject: true });
        const codes = findPageCodes({ width: info.width, height: info.height, channels: info.channels, data })
            .map(({ series, chapter, page, segment }) => ({ series, chapter, page, segment }));
        return { status: res.status, contentType: res.contentType, codes };
    }

    beforeAll(async () => {
        await resetTestSource(client);
    }, 120_000);

    afterAll(async () => {
        await resetTestSource(client);
    }, 120_000);

    it('has the store, SNX Test v1 installed, SNX Extra and SNX Mature (NSFW) not installed', async () => {
        expect(await hasTestStore(client)).toBe(true);
        const byPkg = new Map((await fetchTestExtensions(client)).map((e) => [e.pkgName, e]));
        expect(byPkg.get(EXTENSIONS.test.pkg)).toMatchObject({ name: 'SNX Test', isInstalled: true, hasUpdate: false, versionCode: 1,
            versionName: '1.5.1', contentWarning: 'SAFE', storeIndexUrl: STORE_INDEX_URL });
        expect(byPkg.get(EXTENSIONS.extra.pkg)).toMatchObject({ name: 'SNX Extra', lang: 'fr', isInstalled: false, contentWarning: 'SAFE' });
        expect(byPkg.get(EXTENSIONS.mature.pkg)).toMatchObject({ name: 'SNX Mature', isInstalled: false, contentWarning: 'NSFW' });
    });

    it('lists the en and ja sources with every filter type at its default', async () => {
        const data = await client.query<{ sources: { nodes: { id: string; name: string; lang: string; displayName: string; supportsLatest: boolean }[] } }>(
            '{ sources { nodes { id name lang displayName supportsLatest } } }');
        const ours = data.sources.nodes.filter((s) => s.name.startsWith('SNX '));
        expect(ours).toEqual([
            { id: TEST_SOURCE_IDS.en, name: 'SNX Test', lang: 'en', displayName: 'SNX Test (EN)', supportsLatest: true },
            { id: TEST_SOURCE_IDS.ja, name: 'SNX Test', lang: 'ja', displayName: 'SNX Test (JA)', supportsLatest: true },
        ]);
        for (const id of [TEST_SOURCE_IDS.en, TEST_SOURCE_IDS.ja]) {
            const { source } = await client.query<{ source: { filters: Record<string, unknown>[] } }>(
                `query S($id: LongString!) { source(id: $id) { filters { __typename
                    ... on HeaderFilter { name } ... on SeparatorFilter { name }
                    ... on TextFilter { name text: default } ... on SelectFilter { name values selected: default }
                    ... on CheckBoxFilter { name checked: default }
                    ... on GroupFilter { name filters { __typename ... on TriStateFilter { name tri: default } ... on CheckBoxFilter { name checked: default } } }
                    ... on SortFilter { name values sort: default { index ascending } } } } }`, { id });
            const f = source.filters;
            expect(f.map((x) => x.__typename)).toEqual([...FILTER_TYPES]);
            expect(f.map((x) => x.name)).toEqual([...FILTER_NAMES]);
            expect(f[FILTER.author]).toMatchObject({ text: '' });
            expect(f[FILTER.status]).toMatchObject({ values: [...STATUS_OPTIONS], selected: 0 });
            expect(f[FILTER.colour]).toMatchObject({ checked: false });
            expect(f[FILTER.genres].filters).toEqual(GENRES.map((name) => ({ __typename: 'TriStateFilter', name, tri: 'IGNORE' })));
            expect(f[FILTER.type].filters).toEqual(TYPES.map((name) => ({ __typename: 'CheckBoxFilter', name, checked: false })));
            expect(f[FILTER.sort]).toMatchObject({ values: [...SORTS], sort: { index: SORT.popularity, ascending: false } });
        }
    });

    it('pages popular 25 / 25 / 10 in popularity order, and latest by newest chapter', async () => {
        const popular = selectManga(CATALOG_EN, 'popular');
        for (const page of [1, 2, 3]) {
            const r = await browse(TEST_SOURCE_IDS.en, 'POPULAR', page);
            expect({ keys: r.keys, hasNextPage: r.hasNextPage }, `popular page ${page}`).toEqual(listPage(popular, page));
        }
        expect((await browse(TEST_SOURCE_IDS.en, 'POPULAR', 3)).keys).toHaveLength(10);
        const latest = await browse(TEST_SOURCE_IDS.en, 'LATEST', 1);
        expect(latest.keys).toEqual(listPage(selectManga(CATALOG_EN, 'latest'), 1).keys);
        const ja = await browse(TEST_SOURCE_IDS.ja, 'POPULAR', 1);
        expect(ja).toMatchObject({ keys: listPage(selectManga(CATALOG_JA, 'popular'), 1).keys, hasNextPage: false });
        expect(ja.mangas[0].title).toBe(findManga(ja.keys[0]).title);
    }, 60_000);

    it('searches by text and narrows by each filter type the way the catalog predicts', async () => {
        const all = CATALOG_EN.manga.length;
        const cases: { name: string; query?: string; filters: FilterChange[]; o: SearchOptions }[] = [
            { name: 'text', query: 'comet', filters: [], o: { query: 'comet' } },
            { name: 'Text (author)', filters: [{ position: FILTER.author, textState: 'MORI' }], o: { author: 'mori' } },
            { name: 'Select (status)', filters: [{ position: FILTER.status, selectState: 2 }], o: { status: 'completed' } },
            { name: 'CheckBox (colour)', filters: [{ position: FILTER.colour, checkBoxState: true }], o: { colour: true } },
            { name: 'TriState include', filters: [{ position: FILTER.genres, groupChange: { position: GENRES.indexOf('Comedy'), triState: 'INCLUDE' } }],
                o: { genres: ['Comedy'] } },
            { name: 'TriState exclude', filters: [{ position: FILTER.genres, groupChange: { position: GENRES.indexOf('Action'), triState: 'EXCLUDE' } }],
                o: { notGenres: ['Action'] } },
            { name: 'Group of CheckBoxes (type)', filters: [{ position: FILTER.type, groupChange: { position: TYPES.indexOf('Manhwa'), checkBoxState: true } },
                { position: FILTER.type, groupChange: { position: TYPES.indexOf('Webtoon'), checkBoxState: true } }], o: { types: ['Manhwa', 'Webtoon'] } },
            { name: 'Sort (title, ascending)', filters: [{ position: FILTER.sort, sortState: { index: SORT.title, ascending: true } }],
                o: { sort: { index: SORT.title, ascending: true } } },
            { name: 'Sort (latest, descending)', filters: [{ position: FILTER.sort, sortState: { index: SORT.latest, ascending: false } }],
                o: { sort: { index: SORT.latest, ascending: false } } },
            { name: 'text + filters together', query: 'e', filters: [{ position: FILTER.colour, checkBoxState: true },
                { position: FILTER.genres, groupChange: { position: GENRES.indexOf('Drama'), triState: 'INCLUDE' } }],
                o: { query: 'e', colour: true, genres: ['Drama'] } },
        ];
        for (const c of cases) {
            const want = predicted(c.o);
            if (!c.name.startsWith('Sort')) {
                expect(want.length, `${c.name}: some match`).toBeGreaterThan(0);
                expect(want.length, `${c.name}: not all match`).toBeLessThan(all);
            }
            expect(await searchAll(TEST_SOURCE_IDS.en, c.query, c.filters), c.name).toEqual(want);
        }
    }, 120_000);

    it('serves details, chapters (newest first), a page and the cover through the server', async () => {
        const m = findManga('en-01');
        const id = await findSourceManga(TEST_SOURCE_IDS.en, m.key, client);
        const manga = await mangaWithChapters(id);
        expect(manga).toMatchObject({ title: m.title, author: m.author, artist: m.artist, description: m.description, genre: m.genres,
            status: 'ONGOING', initialized: true });
        const want = listedChapters(m);
        expect(manga.chapters.nodes.map((c) => ({ name: c.name, chapterNumber: c.chapterNumber, scanlator: c.scanlator, uploadDate: c.uploadDate,
            url: c.url }))).toEqual(want.map((c) => ({ name: c.name, chapterNumber: c.number, scanlator: c.scanlator, uploadDate: String(c.date),
            url: `/chapter/${m.key}/${c.id}` })));

        const first = manga.chapters.nodes.find((c) => c.chapterNumber === 1)!;
        const pages = await client.query<{ fetchChapterPages: { pages: string[] } }>(
            'mutation P($id: Int!) { fetchChapterPages(input: { chapterId: $id }) { pages } }', { id: first.id });
        expect(pages.fetchChapterPages.pages).toHaveLength(PAGE_SETS.comet.pages);
        const page = await imageCodes(pages.fetchChapterPages.pages[0]);
        // The server names the type from the file ending: "image/jpg".
        expect(page).toMatchObject({ status: 200, contentType: expect.stringMatching(/^image\/jpe?g/) });
        expect(page.codes).toContainEqual({ series: PAGE_SETS.comet.code, chapter: 1, page: 1, segment: 0 });

        const cover = await imageCodes(`/api/v1/manga/${id}/thumbnail`);
        expect(cover.status).toBe(200);
        expect(cover.codes).toContainEqual({ series: COVER_CODE, chapter: coverNumber(m.key), page: 0, segment: 0 });
        expect((await client.get(`/api/v1/extension/icon/${EXTENSIONS.test.pkg}`)).status).toBe(200);
    }, 60_000);

    it('lets the server download a chapter (and deletes it again)', async () => {
        const id = await findSourceManga(TEST_SOURCE_IDS.en, 'en-01', client);
        const chapter = (await mangaWithChapters(id)).chapters.nodes.find((c) => c.chapterNumber === 2)!;
        await client.query('mutation E($ids: [Int!]!) { enqueueChapterDownloads(input: { ids: $ids }) { downloadStatus { state } } }', { ids: [chapter.id] });
        let downloaded = false;
        for (let i = 0; i < 60 && !downloaded; i++) {
            await new Promise((r) => setTimeout(r, 1000));
            downloaded = (await client.query<{ chapter: { isDownloaded: boolean } }>('query C($id: Int!) { chapter(id: $id) { isDownloaded } }',
                { id: chapter.id })).chapter.isDownloaded;
        }
        expect(downloaded).toBe(true);
        await client.query('mutation D($ids: [Int!]!) { deleteDownloadedChapters(input: { ids: $ids }) { chapters { id } } }', { ids: [chapter.id] });
        expect((await client.query<{ chapter: { isDownloaded: boolean } }>('query C($id: Int!) { chapter(id: $id) { isDownloaded } }',
            { id: chapter.id })).chapter.isDownloaded).toBe(false);
    }, 90_000);

    it('obeys the switch file: a failing call, a delay, a held-back chapter, a failing image', async () => {
        try {
            setTestSourceControl({ fail: [{ lang: 'ja', call: 'search' }] });
            await expect(browse(TEST_SOURCE_IDS.ja, 'SEARCH', 1, 'a')).rejects.toThrow(/SNX test failure: search/);
            expect((await browse(TEST_SOURCE_IDS.ja, 'POPULAR', 1)).keys.length).toBe(CATALOG_JA.manga.length);
            expect((await browse(TEST_SOURCE_IDS.en, 'SEARCH', 1, 'comet')).keys).toEqual(predicted({ query: 'comet' }));

            setTestSourceControl({ delayMs: 2000 });
            const started = Date.now();
            await browse(TEST_SOURCE_IDS.en, 'POPULAR', 2);
            // Java's sleep on Windows can wake a few ms early, and WSL's clock isn't Windows' clock: allow a little.
            expect(Date.now() - started).toBeGreaterThanOrEqual(1900);

            const comet = findManga('en-01');
            const id = await findSourceManga(TEST_SOURCE_IDS.en, comet.key, client);
            setTestSourceControl({ extraChapters: [comet.key] });
            expect((await mangaWithChapters(id)).chapters.nodes.map((c) => c.name)).toEqual(listedChapters(comet, true).map((c) => c.name));
            clearTestSourceControl();
            expect((await mangaWithChapters(id)).chapters.nodes.map((c) => c.name)).toEqual(listedChapters(comet).map((c) => c.name));

            // The server keeps page images it served once, so an image failure needs its page cache emptied first.
            await client.query('mutation { clearCachedImages(input: { cachedPages: true }) { cachedPages } }');
            const png = findManga('en-03');
            const pngId = await findSourceManga(TEST_SOURCE_IDS.en, png.key, client);
            const chapter = (await mangaWithChapters(pngId)).chapters.nodes[0];
            const pages = (await client.query<{ fetchChapterPages: { pages: string[] } }>(
                'mutation P($id: Int!) { fetchChapterPages(input: { chapterId: $id }) { pages } }', { id: chapter.id })).fetchChapterPages.pages;
            setTestSourceControl({ fail: [{ lang: 'en', call: 'image' }] });
            expect((await client.get(pages[1])).status).not.toBe(200);
            clearTestSourceControl();
            const ok = await imageCodes(pages[1]);
            expect(ok).toMatchObject({ status: 200, contentType: expect.stringContaining('image/png') });
            expect(ok.codes).toContainEqual({ series: PAGE_SETS.png.code, chapter: 1, page: 2, segment: 0 });
        } finally {
            clearTestSourceControl();
        }
    }, 90_000);

    it('offers an update to v2, installs it, and goes back to v1', async () => {
        await offerTestSourceUpdate(client);
        expect(await testExtension(EXTENSIONS.test.pkg, client)).toMatchObject({ isInstalled: true, hasUpdate: true, versionCode: 1 });
        await updateExtension(EXTENSIONS.test.pkg, client);
        expect(await testExtension(EXTENSIONS.test.pkg, client)).toMatchObject({ isInstalled: true, hasUpdate: false, versionCode: 2,
            versionName: '1.5.2' });
        expect((await browse(TEST_SOURCE_IDS.en, 'POPULAR', 1)).keys).toEqual(listPage(selectManga(CATALOG_EN, 'popular'), 1).keys);
        await withdrawTestSourceUpdate(client);
        expect(await testExtension(EXTENSIONS.test.pkg, client)).toMatchObject({ isInstalled: true, hasUpdate: false, versionCode: 1,
            versionName: '1.5.1' });
        expect((await browse(TEST_SOURCE_IDS.ja, 'POPULAR', 1)).keys.length).toBe(CATALOG_JA.manga.length);
    }, 90_000);

    it('installs SNX Extra (fr): "Comet" is in two sources, not in the third; then uninstalls it', async () => {
        await installExtension(EXTENSIONS.extra.pkg, client);
        try {
            expect(await testExtension(EXTENSIONS.extra.pkg, client)).toMatchObject({ isInstalled: true });
            const hits = new Map<string, string[]>();
            for (const [lang, id] of [['en', TEST_SOURCE_IDS.en], ['ja', TEST_SOURCE_IDS.ja], ['fr', TEST_SOURCE_IDS.fr]] as const) {
                hits.set(lang, (await browse(id, 'SEARCH', 1, 'Comet')).keys);
            }
            expect(hits.get('en')).toEqual(predicted({ query: 'Comet' }));
            expect(hits.get('en')).toHaveLength(3);
            expect(hits.get('ja')).toEqual([]);
            expect(hits.get('fr')).toEqual(predicted({ query: 'Comet' }, CATALOG_FR));
            expect(hits.get('fr')).toHaveLength(1);
        } finally {
            await uninstallExtension(EXTENSIONS.extra.pkg, client);
        }
        expect(await testExtension(EXTENSIONS.extra.pkg, client)).toMatchObject({ isInstalled: false });
        const sources = await client.query<{ sources: { nodes: { id: string }[] } }>('{ sources { nodes { id } } }');
        expect(sources.sources.nodes.map((s) => s.id)).not.toContain(TEST_SOURCE_IDS.fr);
    }, 90_000);

    it('marks SNX Mature NSFW once installed too', async () => {
        await installExtension(EXTENSIONS.mature.pkg, client);
        try {
            const { source } = await client.query<{ source: { contentWarning: string; displayName: string } }>(
                'query S($id: LongString!) { source(id: $id) { contentWarning displayName } }', { id: TEST_SOURCE_IDS.matureEn });
            expect(source).toEqual({ contentWarning: 'NSFW', displayName: 'SNX Mature (EN)' });
            expect((await browse(TEST_SOURCE_IDS.matureEn, 'POPULAR', 1)).keys).toEqual(selectManga(CATALOG_MATURE, 'popular').map((m) => m.key));
        } finally {
            await uninstallExtension(EXTENSIONS.mature.pkg, client);
        }
    }, 90_000);

    it('removes the store (its extensions that aren\'t installed go) and adds it back', async () => {
        await removeTestStore(client);
        expect(await hasTestStore(client)).toBe(false);
        const without = (await fetchTestExtensions(client)).map((e) => e.pkgName);
        expect(without).not.toContain(EXTENSIONS.extra.pkg);
        expect(await addTestStore(client)).toBe(true);
        expect((await fetchTestExtensions(client)).map((e) => e.pkgName).sort())
            .toEqual([EXTENSIONS.test.pkg, EXTENSIONS.extra.pkg, EXTENSIONS.mature.pkg].sort());
    }, 60_000);
});
