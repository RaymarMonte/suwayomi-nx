// Runs every query in tools/record-fixtures/queries/*.graphql against the seeded test server and saves each JSON answer
// to tests/fixtures/responses/<name>.json, so unit tests can parse real server answers offline.
//
// A query file may start with comment lines:
//   # vars: {"id": "$manga:Alpha"}   variables, as JSON. Placeholders are replaced with real values:
//                                     "$manga:<title>"            that manga's id
//                                     "$chapter:<title>:<number>" the id of that chapter
//                                     "$category:<name>"          that category's id
//                                     "$username", "$password"    the test login (tools/test-server/server.json)
//                                     "$refreshToken"             a fresh refresh token (ui_login only)
//   # auth: ui_login                 record it only while the server runs with this auth mode (default: none)
//   # anonymous                      send it without any credentials
//   # prepare: <one-line GraphQL>    run this first, as its own request, with the same variables (its answer isn't saved)
//   # document: <Name>               send the app's own src/core/graphql/<Name>.graphql (WP-04); the file holds only comments.
//                                     Its answer is what the C++ unit tests parse, so the app's documents and parsers are
//                                     tested together. Mutations here must not change what later recordings show.
// Values that change on every run (tokens) are replaced with fixed ones, so re-recording gives the same files.
// Some queries change server state (reading progress), so record on a fresh server: test-server.sh reset, then start.
//
// Usage (test server running and seeded): node tools/record-fixtures/record-fixtures.ts [name ...]

import { readdir, readFile, writeFile } from 'node:fs/promises';
import { basename } from 'node:path';
import { fileURLToPath } from 'node:url';
import { fail, isMain } from '../../tests/e2e/lib/cli.ts';
import { optionsFromEnv, SuwayomiClient, waitForServer } from '../../tests/e2e/lib/suwayomi.ts';

const QUERIES = fileURLToPath(new URL('./queries/', import.meta.url));
const APP_DOCUMENTS = fileURLToPath(new URL('../../src/core/graphql/', import.meta.url));
const RESPONSES = fileURLToPath(new URL('../../tests/fixtures/responses/', import.meta.url));

// Keys whose values differ between runs, and the fixed value written instead.
const VOLATILE: Record<string, unknown> = {
    accessToken: '<access-token>',
    refreshToken: '<refresh-token>',
};

interface QueryFile {
    name: string;
    text: string;
    vars: Record<string, unknown> | undefined;
    auth: string;
    anonymous: boolean;
    prepare: string | undefined;
}

async function loadQueries(only: string[]): Promise<QueryFile[]> {
    const files = (await readdir(QUERIES)).filter((f) => f.endsWith('.graphql')).sort();
    const out: QueryFile[] = [];
    for (const file of files) {
        const name = basename(file, '.graphql');
        if (only.length > 0 && !only.includes(name)) continue;
        const header = await readFile(QUERIES + file, 'utf8');
        const document = /^#\s*document:\s*(\S+)/m.exec(header)?.[1];
        const text = document ? await readFile(`${APP_DOCUMENTS}${document}.graphql`, 'utf8') : header;
        const vars = /^#\s*vars:\s*(.+)$/m.exec(header)?.[1];
        const auth = /^#\s*auth:\s*(\S+)/m.exec(header)?.[1] ?? 'none';
        out.push({
            name, text, vars: vars ? JSON.parse(vars) : undefined, auth,
            anonymous: /^#\s*anonymous\s*$/m.test(header),
            prepare: /^#\s*prepare:\s*(.+)$/m.exec(header)?.[1],
        });
    }
    return out;
}

interface Lookup {
    mangas: Map<string, { id: number; chapters: Map<number, number> }>;
    categories: Map<string, number>;
}

/** Ids of every library manga (with its chapters) and category, by title / name. */
async function lookupIds(client: SuwayomiClient): Promise<Lookup> {
    const data = await client.query<{
        mangas: { nodes: { id: number; title: string; chapters: { nodes: { id: number; chapterNumber: number }[] } }[] };
        categories: { nodes: { id: number; name: string }[] };
    }>('{ mangas(condition: { inLibrary: true }) { nodes { id title chapters { nodes { id chapterNumber } } } } categories { nodes { id name } } }');
    return {
        mangas: new Map(data.mangas.nodes.map((m) => [m.title, { id: m.id, chapters: new Map(m.chapters.nodes.map((c) => [c.chapterNumber, c.id])) }])),
        categories: new Map(data.categories.nodes.map((c) => [c.name, c.id])),
    };
}

async function resolve(value: unknown, ids: Lookup, client: SuwayomiClient): Promise<unknown> {
    if (typeof value === 'string') {
        const manga = /^\$manga:(.+)$/.exec(value);
        if (manga) return ids.mangas.get(manga[1])?.id ?? fail(`no manga titled "${manga[1]}" in the library`);
        const chapter = /^\$chapter:(.+):(\d+(?:\.\d+)?)$/.exec(value);
        if (chapter) {
            return ids.mangas.get(chapter[1])?.chapters.get(Number(chapter[2])) ?? fail(`no chapter ${chapter[2]} of "${chapter[1]}"`);
        }
        const category = /^\$category:(.+)$/.exec(value);
        if (category) return ids.categories.get(category[1]) ?? fail(`no category "${category[1]}"`);
        if (value === '$username') return client.options.username;
        if (value === '$password') return client.options.password;
        if (value === '$refreshToken') return (await client.login()).refreshToken;
        return value;
    }
    if (Array.isArray(value)) return Promise.all(value.map((v) => resolve(v, ids, client)));
    if (value && typeof value === 'object') {
        const entries = await Promise.all(Object.entries(value).map(async ([k, v]) => [k, await resolve(v, ids, client)] as const));
        return Object.fromEntries(entries);
    }
    return value;
}

export function normalize(value: unknown): unknown {
    if (Array.isArray(value)) return value.map(normalize);
    if (value && typeof value === 'object') {
        // Only string values: the refreshToken *mutation* field holds an object and must stay as it is.
        return Object.fromEntries(Object.entries(value).map(([k, v]) => [k, k in VOLATILE && typeof v === 'string' ? VOLATILE[k] : normalize(v)]));
    }
    return value;
}

async function main(): Promise<void> {
    const options = optionsFromEnv();
    const client = new SuwayomiClient(options);
    await waitForServer(client);
    const queries = await loadQueries(process.argv.slice(2));
    if (queries.length === 0) fail(`no queries found in ${QUERIES}`);
    const ids = await lookupIds(client);
    let saved = 0;
    for (const q of queries) {
        if (q.auth !== options.auth) {
            console.log(`skip  ${q.name} (needs auth ${q.auth}, the server runs ${options.auth})`);
            continue;
        }
        const vars = q.vars ? ((await resolve(q.vars, ids, client)) as Record<string, unknown>) : undefined;
        if (q.prepare) await client.query(q.prepare, vars);
        const res = await client.request(q.text, vars, q.anonymous);
        // A non-JSON answer (e.g. a plain 401) is saved as the HTTP status plus its text, so tests can still see it.
        const body = res.body ?? { httpStatus: res.status, text: res.text };
        await writeFile(`${RESPONSES}${q.name}.json`, JSON.stringify(normalize(body), null, 2) + '\n');
        const note = res.status !== 200 ? ` HTTP ${res.status}` : '';
        const errors = res.body?.errors?.length ? ` with ${res.body.errors.length} GraphQL error(s)` : '';
        console.log(`saved ${q.name}.json${note}${errors}`);
        saved++;
    }
    console.log(`${saved} response(s) -> tests/fixtures/responses/`);
}

if (isMain(import.meta.url)) await main();
