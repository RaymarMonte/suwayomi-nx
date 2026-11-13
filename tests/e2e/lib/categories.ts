// The test server's categories for library-tools (WP-24): the seed (tools/fixtures seed.ts) has Default plus Reading,
// Long series, Big shelf and Empty, in that order, and Alpha is in Reading only. The scenario adds, renames, moves and
// deletes categories and changes Alpha's; restoreSeed() puts all of that back (also after a failed run), straight over
// GraphQL, so the scenarios after it see the seed again.

import { optionsFromEnv, SuwayomiClient } from './suwayomi.ts';

export const SEED_CATEGORIES = ['Reading', 'Long series', 'Big shelf', 'Empty'] as const;

export interface ServerCategory {
    id: number;
    name: string;
    order: number;
    default: boolean;
}

/** Every category but Default, in the server's order. */
export async function serverCategories(client = new SuwayomiClient(optionsFromEnv())): Promise<ServerCategory[]> {
    const r = await client.query<{ categories: { nodes: ServerCategory[] } }>('{ categories { nodes { id name order default } } }');
    return r.categories.nodes.filter((c) => !c.default && c.id !== 0).sort((a, b) => a.order - b.order);
}

/** The id of the library manga called `title` (the seed's titles are unique). */
export async function libraryMangaId(title: string, client = new SuwayomiClient(optionsFromEnv())): Promise<number> {
    const r = await client.query<{ mangas: { nodes: { id: number }[] } }>(
        'query M($t: String!) { mangas(condition: { inLibrary: true, title: $t }) { nodes { id } } }', { t: title });
    const found = r.mangas.nodes[0];
    if (!found) throw new Error(`the test server's library has no "${title}": run tools/test-server/test-server.sh reset`);
    return found.id;
}

/** The categories (ids, Default left out) manga `id` is in. */
export async function mangaCategoryIds(id: number, client = new SuwayomiClient(optionsFromEnv())): Promise<number[]> {
    const r = await client.query<{ manga: { categories: { nodes: { id: number }[] } } }>(
        'query M($id: Int!) { manga(id: $id) { categories { nodes { id } } } }', { id });
    return r.manga.categories.nodes.map((c) => c.id).filter((c) => c !== 0).sort((a, b) => a - b);
}

/** Puts the seed's categories (names, order) and Alpha's membership back. */
export async function restoreSeed(client = new SuwayomiClient(optionsFromEnv())): Promise<void> {
    for (const c of await serverCategories(client)) {
        if (!(SEED_CATEGORIES as readonly string[]).includes(c.name)) {
            await client.query('mutation D($id: Int!) { deleteCategory(input: { categoryId: $id }) { category { id } } }', { id: c.id });
        }
    }
    let now = await serverCategories(client);
    for (const name of SEED_CATEGORIES) {
        if (!now.some((c) => c.name === name)) {
            await client.query('mutation C($n: String!) { createCategory(input: { name: $n }) { category { id } } }', { n: name });
        }
    }
    now = await serverCategories(client);
    // One move per category, in order (each move shifts the others, so no "already there" check on stale orders).
    for (const [i, name] of SEED_CATEGORIES.entries()) {
        const c = now.find((x) => x.name === name);
        if (c) {
            await client.query('mutation O($id: Int!, $p: Int!) { updateCategoryOrder(input: { id: $id, position: $p }) { categories { id } } }',
                { id: c.id, p: i + 1 });
        }
    }
    now = await serverCategories(client);
    const reading = now.find((c) => c.name === 'Reading');
    if (!reading) throw new Error('restoreSeed: no Reading category');
    const alpha = await libraryMangaId('Alpha', client);
    const has = await mangaCategoryIds(alpha, client);
    const remove = has.filter((id) => id !== reading.id);
    const add = has.includes(reading.id) ? [] : [reading.id];
    if (remove.length > 0 || add.length > 0) {
        await client.query(
            'mutation U($id: Int!, $a: [Int!]!, $r: [Int!]!) { updateMangaCategories(input: { id: $id, patch: { addToCategories: $a, removeFromCategories: $r } }) { manga { id } } }',
            { id: alpha, a: add, r: remove });
    }
}
