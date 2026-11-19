// WP-18 (D59): helpers for the switch-downloads scenario. Downloads to the Switch live in the app's data folder under
// downloads/<server key>/ (core/SwitchDownloads.hpp): server.json names the server's addresses, m<manga>/manga.json is
// the index, m<manga>/c<chapter>/0000.jpg... are the pages. The known state only ever touches the test server's folder
// (found by its server.json): on the console the owner's own downloads live next to it.
import type { Driver } from '../drivers/Driver.ts';
import { SuwayomiClient } from './suwayomi.ts';

const sameUrl = (a: string, b: string) => a.replace(/\/+$/, '').toLowerCase() === b.replace(/\/+$/, '').toLowerCase();

/** The downloads/<key> folder names whose server.json names `serverUrl` (the test server's address the app uses). */
export async function testServerFolders(driver: Driver, serverUrl: string): Promise<string[]> {
    const found: string[] = [];
    for (const key of await driver.listFiles('downloads')) {
        const text = (await driver.readFile(`downloads/${key}/server.json`)).toString('utf8');
        if (!text) continue;
        try {
            const urls = (JSON.parse(text) as { urls?: unknown }).urls;
            if (Array.isArray(urls) && urls.some((u) => typeof u === 'string' && sameUrl(u, serverUrl))) found.push(key);
        } catch {
            // a damaged server.json: not known to be the test server's, so left alone
        }
    }
    return found;
}

/**
 * Desktop: deletes the test server's downloads folders (the app must not be running). The Switch can't delete files from
 * outside the app: there the scenario deletes through the app's own tab (deleteEverythingInTab) and this does nothing.
 */
export async function removeTestServerFolders(driver: Driver, serverUrl: string): Promise<void> {
    if (driver.name !== 'desktop') return;
    for (const key of await testServerFolders(driver, serverUrl)) await driver.removeFolder(`downloads/${key}`);
}

/** The page files of a downloaded chapter, in page order ("downloads/<key>/m<manga>/c<chapter>/0000.jpg", ...). */
export async function chapterPageFiles(driver: Driver, key: string, mangaId: number, chapterId: number): Promise<string[]> {
    const dir = `downloads/${key}/m${mangaId}/c${chapterId}`;
    return (await driver.listFiles(dir)).filter((name) => /^\d{4}\.(jpg|png|webp|gif)$/.test(name)).sort().map((name) => `${dir}/${name}`);
}

/** The chapter's page addresses as the server lists them (the same call the downloader makes). */
export async function serverPages(client: SuwayomiClient, chapterId: number): Promise<string[]> {
    const answer = await client.query<{ fetchChapterPages: { pages: string[] } }>(
        'mutation P($id: Int!) { fetchChapterPages(input: { chapterId: $id }) { pages } }', { id: chapterId });
    return answer.fetchChapterPages.pages;
}

/** A page's bytes from the server, as stored (no conversion: the downloader asks for the same). */
export async function serverPageBytes(client: SuwayomiClient, page: string): Promise<Buffer> {
    const answer = await client.get(page);
    if (answer.status !== 200) throw new Error(`GET ${page}: HTTP ${answer.status}`);
    return answer.body;
}
