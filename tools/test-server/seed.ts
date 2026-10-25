// Sets up the test server's library from the fixture manga in Local Source (WP-02). Safe to run again: it only adds what's
// missing. tools/test-server/test-server.sh start runs it after every start.
//   1. browse Local Source (source id "0"): the server then knows every fixture folder as a manga
//   2. load each manga's details and chapters from its folder
//   3. add them all to the library
//   4. create the categories below and put manga in them (the rest stay in the default category)
//
// Usage: node tools/test-server/seed.ts   (server address/auth from SNX_* variables, see test-server.sh env)

import { fail, isMain } from '../../tests/e2e/lib/cli.ts';
import { optionsFromEnv, SuwayomiClient, waitForServer } from '../../tests/e2e/lib/suwayomi.ts';
import { SERIES } from '../gen-fixtures/fixtures.ts';

const LOCAL_SOURCE_ID = '0';

export const CATEGORIES: { name: string; manga: string[] }[] = [
    { name: 'Reading', manga: ['Alpha', 'Beta RTL'] },
    { name: 'Long series', manga: ['Gamma Webtoon', 'Delta Big'] },
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
    const missing = SERIES.map((s) => s.title).filter((t) => !byTitle.has(t));
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
        await client.query(
            'mutation A($ids: [Int!]!, $cat: Int!) { updateMangasCategories(input: { ids: $ids, patch: { addToCategories: [$cat] } }) { mangas { id } } }',
            { ids, cat: id });
    }
    console.log(`seed: ${about.name} ${about.version}, ${mangas.length} manga in the library (${toAdd.length} new), ` +
        `categories: ${CATEGORIES.map((c) => c.name).join(', ')}`);
}

if (isMain(import.meta.url)) await main();
