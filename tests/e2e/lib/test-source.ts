// Helpers for tests that use the test source (WP-23, D55, docs/TESTING.md "Test source"): our own extension "SNX Test"
// (en + ja), "SNX Extra" (fr) and "SNX Mature" (en, NSFW) in a store on the PC (http://127.0.0.1:4569/store/index.json).
// The seed adds the store and installs SNX Test v1. Tests change the switch file (failures, delays, held-back chapters) and
// the store (an update) through these helpers, and put everything back with resetTestSource().
//
// The test site lives in <repo>/.test-server/web/ (served by jwebserver on the Windows side, loopback only), so these
// helpers write its files directly; WSL can't reach 127.0.0.1:4569 on Windows, and doesn't need to: covers and pages come
// through the server.

import { renameSync, rmSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { jsonText, writeStoreIndex } from '../../../tools/gen-fixtures/gen-test-source.ts';
import { ALL_EXTENSIONS, EXTENSIONS, STORE_INDEX_URL, TEST_SOURCE_IDS } from '../../../tools/gen-fixtures/test-source.ts';
import { optionsFromEnv, SuwayomiClient } from './suwayomi.ts';

export { EXTENSIONS, STORE_INDEX_URL, TEST_SOURCE_IDS };

/** The test site's folder (git-ignored, written by tools/gen-fixtures/gen-test-source.ts). */
export const WEB_DIR = fileURLToPath(new URL('../../../.test-server/web/', import.meta.url));
const CONTROL_FILE = join(WEB_DIR, 'site', 'control.json');

const defaultClient = () => new SuwayomiClient(optionsFromEnv());

// ---- the switch file ----

/** What the switch file can say (the Java side: tools/test-source/src/snx/testsource/Control.java). */
export interface TestSourceControl {
    /** Calls that throw "SNX test failure: <call>". lang: en, ja, fr or *; call: popular, latest, search, details, chapters,
     * pages, image (page images), cover or *. */
    fail?: { lang: string; call: string }[];
    /** Wait this long before every call (0..10 000 ms). */
    delayMs?: number;
    /** Manga keys whose held-back chapter shows (en-01 "Comet Courier" has one: chapter 3). */
    extraChapters?: string[];
}

/** Writes the switch file. The source reads it on every call, so it takes effect at once. */
export function setTestSourceControl(control: TestSourceControl): void {
    writeAtomically(CONTROL_FILE, jsonText(control));
}

/** Removes the switch file: everything normal again. Safe when there is none. */
export function clearTestSourceControl(): void {
    rmSync(CONTROL_FILE, { force: true });
}

/**
 * WP-18c: empties the server's own page cache (SUWAYOMI-API.md: the server keeps every page it served once), so pages come
 * from the test source again, with the switch file's delays and failures. Before any step that needs slow or failing
 * pages: then no scenario depends on a freshly reset server (the integration test and other scenarios read pages too).
 */
export async function clearServerPageCache(client: SuwayomiClient = defaultClient()): Promise<void> {
    await client.query('mutation { clearCachedImages(input: { cachedPages: true }) { cachedPages } }');
}

/** Writes a temporary file and renames it over the target, so the source never reads half a file. Retries a busy file. */
function writeAtomically(path: string, text: string): void {
    const tmp = `${path}.tmp`;
    writeFileSync(tmp, text);
    for (let attempt = 1; ; attempt++) {
        try {
            renameSync(tmp, path);
            return;
        } catch (e) {
            // On Windows a file that jwebserver is sending at this moment can't be replaced; it's free again a moment later.
            if (attempt >= 20) throw e;
            Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 50);
        }
    }
}

// ---- the store and the extensions ----

export interface ExtensionState {
    pkgName: string;
    name: string;
    lang: string;
    isInstalled: boolean;
    hasUpdate: boolean;
    versionCode: number;
    versionName: string;
    contentWarning: 'SAFE' | 'MIXED' | 'NSFW';
    storeIndexUrl: string | null;
}
const EXTENSION_FIELDS = 'pkgName name lang isInstalled hasUpdate versionCode versionName contentWarning storeIndexUrl';
const OUR_PACKAGES = new Set<string>(ALL_EXTENSIONS.map((e) => e.pkg));

/** Is our store in the server's list? */
export async function hasTestStore(client: SuwayomiClient = defaultClient()): Promise<boolean> {
    const data = await client.query<{ extensionStores: { nodes: { indexUrl: string }[] } }>('{ extensionStores { nodes { indexUrl } } }');
    return data.extensionStores.nodes.some((s) => s.indexUrl === STORE_INDEX_URL);
}

/** Adds our store if the server doesn't have it. Returns true when it was added now. */
export async function addTestStore(client: SuwayomiClient = defaultClient()): Promise<boolean> {
    if (await hasTestStore(client)) return false;
    await client.query('mutation A($url: String!) { addExtensionStore(input: { indexUrl: $url }) { extensionStore { indexUrl } } }',
        { url: STORE_INDEX_URL });
    return true;
}

export async function removeTestStore(client: SuwayomiClient = defaultClient()): Promise<void> {
    if (!(await hasTestStore(client))) return;
    await client.query('mutation R($url: String!) { removeExtensionStore(input: { indexUrl: $url }) { extensionStore { indexUrl } } }',
        { url: STORE_INDEX_URL });
}

/** Asks the server to read the stores again and returns our three extensions (as the server sees them now). */
export async function fetchTestExtensions(client: SuwayomiClient = defaultClient()): Promise<ExtensionState[]> {
    const data = await client.query<{ fetchExtensions: { extensions: ExtensionState[] } }>(
        `mutation { fetchExtensions(input: {}) { extensions { ${EXTENSION_FIELDS} } } }`);
    return data.fetchExtensions.extensions.filter((e) => OUR_PACKAGES.has(e.pkgName));
}

/** One of our extensions after a fresh store read; throws when the server doesn't list it. */
export async function testExtension(pkg: string, client: SuwayomiClient = defaultClient()): Promise<ExtensionState> {
    const ext = (await fetchTestExtensions(client)).find((e) => e.pkgName === pkg);
    if (!ext) throw new Error(`the server doesn't list the extension ${pkg} (is the store added? is the test site running?)`);
    return ext;
}

async function patchExtension(client: SuwayomiClient, pkg: string, patch: { install?: true; uninstall?: true; update?: true }): Promise<void> {
    await client.query(
        `mutation U($id: String!, $patch: UpdateExtensionPatchInput!) { updateExtension(input: { id: $id, patch: $patch }) { extension { pkgName } } }`,
        { id: pkg, patch });
}

/**
 * Installs one of our extensions (the version the store offers now) and checks that it worked. The store is read first:
 * right after an uninstall the server only knows what to download after a fresh store read; without one, the install
 * quietly does nothing (seen on v2.4.2366).
 */
export async function installExtension(pkg: string, client: SuwayomiClient = defaultClient()): Promise<void> {
    await fetchTestExtensions(client);
    await patchExtension(client, pkg, { install: true });
    if (!(await testExtension(pkg, client)).isInstalled) throw new Error(`installing ${pkg} didn't work (see the server log)`);
}

export async function uninstallExtension(pkg: string, client: SuwayomiClient = defaultClient()): Promise<void> {
    await patchExtension(client, pkg, { uninstall: true });
}

/** Updates one of our extensions to the version the store offers (offerTestSourceUpdate first). */
export async function updateExtension(pkg: string, client: SuwayomiClient = defaultClient()): Promise<void> {
    await patchExtension(client, pkg, { update: true });
}

/**
 * The seed's part (tools/test-server/seed.ts, every start): our store in the server's list and SNX Test installed. It doesn't
 * browse, so no manga or chapter rows are made and the seed's ids stay fixed (D55). Safe to run again.
 */
export async function seedTestSource(client: SuwayomiClient = defaultClient()): Promise<{ storeAdded: boolean; installed: boolean }> {
    const storeAdded = await addTestStore(client);
    const test = await testExtension(EXTENSIONS.test.pkg, client);
    if (test.isInstalled) return { storeAdded, installed: false };
    await installExtension(EXTENSIONS.test.pkg, client);
    return { storeAdded, installed: true };
}

/** The store offers SNX Test v2 (an update). The server notices at its next store read, which this does too. */
export async function offerTestSourceUpdate(client: SuwayomiClient = defaultClient()): Promise<void> {
    writeStoreIndex(WEB_DIR, 2);
    await fetchTestExtensions(client);
}

/**
 * The store offers SNX Test v1 again. If v2 is installed, it goes back to v1: uninstall, then install (the server can't
 * "update" to an older version).
 */
export async function withdrawTestSourceUpdate(client: SuwayomiClient = defaultClient()): Promise<void> {
    writeStoreIndex(WEB_DIR, 1);
    const test = await testExtension(EXTENSIONS.test.pkg, client);
    if (test.isInstalled && test.versionCode !== 1) {
        await uninstallExtension(EXTENSIONS.test.pkg, client);
        await installExtension(EXTENSIONS.test.pkg, client);
    }
}

/**
 * Everything as the seed left it: no switch file, the store listed and offering v1, SNX Test v1 installed, SNX Extra and
 * SNX Mature not installed. (Manga and chapter rows that tests made by browsing stay: their ids are never fixed.)
 */
export async function resetTestSource(client: SuwayomiClient = defaultClient()): Promise<void> {
    clearTestSourceControl();
    writeStoreIndex(WEB_DIR, 1);
    await addTestStore(client);
    for (const ext of await fetchTestExtensions(client)) {
        if (ext.pkgName === EXTENSIONS.test.pkg) {
            if (ext.isInstalled && ext.versionCode === 1) continue;
            if (ext.isInstalled) await uninstallExtension(ext.pkgName, client);
            await installExtension(ext.pkgName, client);
        } else if (ext.isInstalled) {
            await uninstallExtension(ext.pkgName, client);
        }
    }
}

// ---- manga ----

/** The url the source gives a manga (what the server stores in `url`). */
export function sourceMangaUrl(key: string): string {
    return `/manga/${key}`;
}

/**
 * A test-source manga's server id, by its url. Ids after the seed's aren't fixed (D55: the server makes a row when a manga is
 * first browsed), so tests look them up. If the server hasn't seen the manga yet, this browses the source's popular list
 * until it shows up (that makes the row).
 */
export async function findSourceManga(sourceId: string, key: string, client: SuwayomiClient = defaultClient()): Promise<number> {
    const url = sourceMangaUrl(key);
    const known = await client.query<{ mangas: { nodes: { id: number }[] } }>(
        'query M($source: LongString!, $url: String!) { mangas(condition: { sourceId: $source, url: $url }) { nodes { id } } }',
        { source: sourceId, url });
    if (known.mangas.nodes.length > 0) return known.mangas.nodes[0].id;
    for (let page = 1; ; page++) {
        const data = await client.query<{ fetchSourceManga: { mangas: { id: number; url: string }[]; hasNextPage: boolean } }>(
            `mutation B($source: LongString!, $page: Int!) {
                fetchSourceManga(input: { source: $source, type: POPULAR, page: $page }) { mangas { id url } hasNextPage } }`,
            { source: sourceId, page });
        const hit = data.fetchSourceManga.mangas.find((m) => m.url === url);
        if (hit) return hit.id;
        if (!data.fetchSourceManga.hasNextPage) throw new Error(`source ${sourceId} doesn't list ${key}`);
    }
}

/**
 * WP-14 (D56): every test-source manga out of the library, with its categories cleared (a removed manga keeps its old
 * categories on the server, SUWAYOMI-API.md), so a browse scenario starts and ends in a known state. The seed's library
 * (Local's 157 manga) isn't touched. Returns how many it took out.
 */
export async function removeTestSourceMangaFromLibrary(client: SuwayomiClient = defaultClient()): Promise<number> {
    const sources = Object.values(TEST_SOURCE_IDS);
    const data = await client.query<{ mangas: { nodes: { id: number; inLibrary: boolean; sourceId: string; categories: { totalCount: number } }[] } }>(
        `query T($sources: [LongString!]!) {
            mangas(filter: { sourceId: { in: $sources } }) { nodes { id inLibrary sourceId categories { totalCount } } } }`,
        { sources });
    let removed = 0;
    for (const manga of data.mangas.nodes) {
        if (manga.categories.totalCount > 0) {
            await client.query(
                `mutation C($id: Int!) { updateMangaCategories(input: { id: $id, patch: { clearCategories: true } }) { manga { id } } }`,
                { id: manga.id });
        }
        if (manga.inLibrary) {
            await client.query(
                `mutation R($id: Int!) { updateManga(input: { id: $id, patch: { inLibrary: false } }) { manga { id } } }`,
                { id: manga.id });
            removed++;
        }
    }
    return removed;
}
