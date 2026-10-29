// The test server's chapters, read and reset through GraphQL from the harness (WP-10, WP-11): scenarios check on the server
// what the app saved there, and put the chapters they touch back the way they were.

import type { SuwayomiClient } from './suwayomi.ts';

export interface ChapterRow {
    id: number;
    sourceOrder: number;
    isRead: boolean;
    isBookmarked: boolean;
    lastPageRead: number;
}

/** A manga's chapters on the server, oldest first. */
export async function chaptersOf(client: SuwayomiClient, mangaId: number): Promise<ChapterRow[]> {
    const data = await client.query<{ chapters: { nodes: ChapterRow[] } }>(
        `query C($m: Int!) { chapters(condition: { mangaId: $m }, order: [{ by: SOURCE_ORDER }]) {
            nodes { id sourceOrder isRead isBookmarked lastPageRead } } }`, { m: mangaId });
    return data.chapters.nodes;
}

/** Every chapter unread, without bookmark, at page 0. Returns them, oldest first. */
export async function resetChapters(client: SuwayomiClient, mangaId: number): Promise<ChapterRow[]> {
    const ids = (await chaptersOf(client, mangaId)).map((c) => c.id);
    await client.query(`mutation U($ids: [Int!]!) { updateChapters(input: { ids: $ids,
        patch: { isRead: false, isBookmarked: false, lastPageRead: 0 } }) { chapters { id } } }`, { ids });
    return chaptersOf(client, mangaId);
}

/** The library's manga by title. */
export async function mangaIds(client: SuwayomiClient): Promise<Map<string, number>> {
    const data = await client.query<{ mangas: { nodes: { id: number; title: string }[] } }>(
        '{ mangas(condition: { inLibrary: true }) { nodes { id title } } }');
    return new Map(data.mangas.nodes.map((m) => [m.title, m.id]));
}
