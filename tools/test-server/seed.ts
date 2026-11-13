// Sets up the test server's library from the fixture manga in Local Source (WP-02). Safe to run again: it only adds what's
// missing. tools/test-server/test-server.sh start runs it after every start.
//   1. browse Local Source (source id "0"): the server then knows every fixture folder as a manga
//   2. load each manga's details and chapters from its folder
//   3. add them all to the library
//   4. create the categories below and put manga in them (the rest stay in the default category)
//   5. (WP-15) Epsilon Weekly last, so no older id moves: manga 157, in Reading, its two chapters read (see seedEpsilon)
//
// Usage: node tools/test-server/seed.ts   (server address/auth from SNX_* variables, see test-server.sh env)

import { fail, isMain } from '../../tests/e2e/lib/cli.ts';
import { copyEpsilonChapter, EPSILON_MANGA_ID, optionsFromEnv, SuwayomiClient, waitForServer } from '../../tests/e2e/lib/suwayomi.ts';
import { ALL_SERIES, EPSILON, EPSILON_PUBLISHED_CHAPTERS, SHELF } from '../gen-fixtures/fixtures.ts';

const LOCAL_SOURCE_ID = '0';

export const CATEGORIES: { name: string; manga: string[] }[] = [
    { name: 'Reading', manga: ['Alpha', 'Beta RTL'] },
    { name: 'Long series', manga: ['Gamma Webtoon', 'Delta Big'] },
    { name: 'Big shelf', manga: SHELF.map((s) => s.title) }, // WP-09: the library grid's scrolling and paging
    { name: 'Empty', manga: [] }, // WP-09: the library's "nothing in this category" screen
];

interface MangaRef {
    id: number;
    title: string;
    inLibrary: boolean;
}

async function browseLocalSource(client: SuwayomiClient): Promise<MangaRef[]> {
    const all: MangaRef[] = [];
    for (let page = 1; ; page++) {
        const data = await client.query<{ fetchSourceManga: { mangas: MangaRef[]; hasNextPage: boolean } }>(
            `mutation Browse($source: LongString!, $page: Int!) {
                fetchSourceManga(input: { source: $source, type: POPULAR, page: $page }) { mangas { id title inLibrary } hasNextPage }
            }`,
            { source: LOCAL_SOURCE_ID, page },
        );
        all.push(...data.fetchSourceManga.mangas);
        if (!data.fetchSourceManga.hasNextPage) return all;
    }
}

async function main(): Promise<void> {
    const client = new SuwayomiClient(optionsFromEnv());
    const about = await waitForServer(client);

    const mangas = await browseLocalSource(client);
    const byTitle = new Map(mangas.map((m) => [m.title, m]));
    const missing = ALL_SERIES.map((s) => s.title).filter((t) => !byTitle.has(t));
    if (missing.length > 0) fail(`Local Source doesn't list: ${missing.join(', ')} (did reset copy the fixtures?)`);

    const toAdd = mangas.filter((m) => !m.inLibrary);
    for (const m of toAdd) {
        await client.query(
            'mutation F($id: Int!) { fetchMangaAndChapters(input: { id: $id, fetchManga: true, fetchChapters: true }) { manga { id } } }',
            { id: m.id });
    }
    if (toAdd.length > 0) {
        await client.query('mutation L($ids: [Int!]!) { updateMangas(input: { ids: $ids, patch: { inLibrary: true } }) { mangas { id } } }',
            { ids: toAdd.map((m) => m.id) });
    }

    const existing = await client.query<{ categories: { nodes: { id: number; name: string }[] } }>('{ categories { nodes { id name } } }');
    for (const cat of CATEGORIES) {
        let id = existing.categories.nodes.find((c) => c.name === cat.name)?.id;
        if (id === undefined) {
            const created = await client.query<{ createCategory: { category: { id: number } } }>(
                'mutation N($name: String!) { createCategory(input: { name: $name }) { category { id } } }', { name: cat.name });
            id = created.createCategory.category.id;
        }
        const ids = cat.manga.map((t) => byTitle.get(t)!.id);
        if (ids.length === 0) continue;
        await client.query(
            'mutation A($ids: [Int!]!, $cat: Int!) { updateMangasCategories(input: { ids: $ids, patch: { addToCategories: [$cat] } }) { mangas { id } } }',
            { ids, cat: id });
    }
    const epsilon = await seedEpsilon(client);
    await reloadDetails(client);

    console.log(`seed: ${about.name} ${about.version}, ${mangas.length + (epsilon.added ? 1 : 0)} manga in the library ` +
        `(${toAdd.length + (epsilon.added ? 1 : 0)} new), categories: ${CATEGORIES.map((c) => c.name).join(', ')}, ` +
        `${EPSILON.title} = manga ${epsilon.id}`);
}

/**
 * Browsing Local Source again (every seed run does it) resets the status of manga that were browsed before to "unknown", which
 * would turn Alpha's recorded "ongoing" into "unknown". So the last thing the seed does is load the details of the manga that
 * have a details.json (Alpha, Epsilon Weekly) once more.
 */
async function reloadDetails(client: SuwayomiClient): Promise<void> {
    const withDetails = new Set([...ALL_SERIES, EPSILON].filter((s) => s.details).map((s) => s.title));
    const library = await client.query<{ mangas: { nodes: { id: number; title: string }[] } }>(
        '{ mangas(condition: { inLibrary: true }) { nodes { id title } } }');
    for (const m of library.mangas.nodes.filter((n) => withDetails.has(n.title))) {
        await client.query('mutation D($id: Int!) { fetchManga(input: { id: $id }) { manga { id } } }', { id: m.id });
    }
}

/**
 * Step 2 (WP-15): Epsilon Weekly, for the "library update finds a new chapter" tests. It comes after everything above, so the
 * first 156 manga and 459 chapters keep their ids (it becomes manga 157, chapters 460-461). Only chapters 1 and 2 are copied in;
 * chapter 3 is held back for addNewChapter(). It goes into Reading (after Alpha and Beta RTL), and both chapters are marked read
 * with isRead only: lastPageRead would stamp lastReadAt and the History would no longer be empty on a fresh seed.
 */
async function seedEpsilon(client: SuwayomiClient): Promise<{ id: number; added: boolean }> {
    for (let ch = 1; ch <= EPSILON_PUBLISHED_CHAPTERS; ch++) copyEpsilonChapter(ch); // does nothing for what is already there

    const mangas = await browseLocalSource(client); // the server learns the new folder here
    const epsilon = mangas.find((m) => m.title === EPSILON.title);
    if (!epsilon) fail(`Local Source doesn't list ${EPSILON.title} after copying its folder in`);
    if (epsilon.id !== EPSILON_MANGA_ID) fail(`${EPSILON.title} got manga id ${epsilon.id}, expected ${EPSILON_MANGA_ID} (is the library fresh?)`);

    const added = !epsilon.inLibrary;
    if (added) {
        await client.query(
            'mutation F($id: Int!) { fetchMangaAndChapters(input: { id: $id, fetchManga: true, fetchChapters: true }) { manga { id } } }',
            { id: epsilon.id });
        await client.query('mutation L($ids: [Int!]!) { updateMangas(input: { ids: $ids, patch: { inLibrary: true } }) { mangas { id } } }',
            { ids: [epsilon.id] });
    }
    const categories = (await client.query<{ categories: { nodes: { id: number; name: string }[] } }>('{ categories { nodes { id name } } }'))
        .categories.nodes; // asked again: on a fresh server the categories were created a moment ago
    const reading = categories.find((c) => c.name === 'Reading');
    if (!reading) fail('the Reading category is missing');
    await client.query(
        'mutation A($ids: [Int!]!, $cat: Int!) { updateMangasCategories(input: { ids: $ids, patch: { addToCategories: [$cat] } }) { mangas { id } } }',
        { ids: [epsilon.id], cat: reading.id });

    const chapters = await client.query<{ manga: { chapters: { nodes: { id: number; isRead: boolean }[] } } }>(
        'query C($id: Int!) { manga(id: $id) { chapters { nodes { id isRead } } } }', { id: epsilon.id });
    const unread = chapters.manga.chapters.nodes.filter((c) => !c.isRead).map((c) => c.id);
    if (unread.length > 0) {
        await client.query('mutation R($ids: [Int!]!) { updateChapters(input: { ids: $ids, patch: { isRead: true } }) { chapters { id } } }',
            { ids: unread });
    }
    return { id: epsilon.id, added };
}

if (isMain(import.meta.url)) await main();
