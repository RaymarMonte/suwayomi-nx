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

/** WP-13b: the webtoon strip's spot notes of a manga's chapters (chapter meta "snx_strip_spot", "<page>:<share>"), by id. */
export async function stripSpots(client: SuwayomiClient, mangaId: number): Promise<Map<number, string>> {
    const data = await client.query<{ chapters: { nodes: { id: number; meta: { key: string; value: string }[] }[] } }>(
        `query S($m: Int!) { chapters(condition: { mangaId: $m }) { nodes { id meta { key value } } } }`, { m: mangaId });
    const spots = new Map<number, string>();
    for (const chapter of data.chapters.nodes) {
        const note = chapter.meta.find((m) => m.key === 'snx_strip_spot');
        if (note) spots.set(chapter.id, note.value);
    }
    return spots;
}

/** Deletes the spot notes of a manga's chapters (resetChapters leaves the chapters' meta alone). */
export async function clearStripSpots(client: SuwayomiClient, mangaId: number): Promise<void> {
    for (const id of (await stripSpots(client, mangaId)).keys()) {
        await client.query(`mutation D($id: Int!) { deleteChapterMeta(input: { chapterId: $id, key: "snx_strip_spot" }) {
            clientMutationId } }`, { id });
    }
}
