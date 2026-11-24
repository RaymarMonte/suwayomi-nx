// Helpers for the server-downloads scenario (WP-17, D58): the Suwayomi server's own download queue (downloads onto the
// server's disk), read and changed through GraphQL from the harness, so a scenario can check what the app did there and put
// everything back the way it was.

import { optionsFromEnv, SuwayomiClient } from './suwayomi.ts';
import { findSourceManga, TEST_SOURCE_IDS } from './test-source.ts';

const defaultClient = () => new SuwayomiClient(optionsFromEnv());

/** The test manga: SNX Test (EN) en-14 "Paper Garden", the newest (first in Latest), 8 chapters of 3 pages. */
export const PAPER_GARDEN_KEY = 'en-14';

export interface QueueRow {
    chapterId: number;
    sourceOrder: number;
    mangaId: number;
    state: 'QUEUED' | 'DOWNLOADING' | 'ERROR' | 'FINISHED';
    progress: number;
    tries: number;
}

export interface ServerQueue {
    state: 'STARTED' | 'STOPPED';
    rows: QueueRow[];
}

const QUEUE_FIELDS = 'state queue { position state progress tries chapter { id sourceOrder } manga { id } }';

type RawQueue = {
    state: ServerQueue['state'];
    queue: { position: number; state: QueueRow['state']; progress: number; tries: number; chapter: { id: number; sourceOrder: number }; manga: { id: number } }[];
};

function toQueue(raw: RawQueue): ServerQueue {
    const rows = [...raw.queue].sort((a, b) => a.position - b.position).map((r) => ({
        chapterId: r.chapter.id, sourceOrder: r.chapter.sourceOrder, mangaId: r.manga.id, state: r.state, progress: r.progress,
        tries: r.tries,
    }));
    return { state: raw.state, rows };
}

/** The server's queue now, in its order. A finished chapter can still be listed for a moment (FINISHED): left out here. */
export async function serverQueue(client: SuwayomiClient = defaultClient()): Promise<ServerQueue> {
    const data = await client.query<{ downloadStatus: RawQueue }>(`{ downloadStatus { ${QUEUE_FIELDS} } }`);
    const queue = toQueue(data.downloadStatus);
    return { state: queue.state, rows: queue.rows.filter((r) => r.state !== 'FINISHED') };
}

/** Empties the server's queue (failed rows too) and stops the downloader. */
export async function clearServerQueue(client: SuwayomiClient = defaultClient()): Promise<void> {
    await client.query('mutation { clearDownloader(input: {}) { downloadStatus { state } } }');
}

/**
 * Stops the server's downloader. It answers only once the running source call has ended (with the switch file's delayMs,
 * up to that long), though the state reads STOPPED at once.
 */
export async function stopOnServer(client: SuwayomiClient = defaultClient()): Promise<void> {
    await client.query('mutation { stopDownloader(input: {}) { downloadStatus { state } } }');
}

/** Queues chapters on the server (the downloader starts by itself about a second later). */
export async function enqueueOnServer(ids: number[], client: SuwayomiClient = defaultClient()): Promise<void> {
    await client.query('mutation E($ids: [Int!]!) { enqueueChapterDownloads(input: { ids: $ids }) { downloadStatus { state } } }', { ids });
}

/** Deletes chapters' downloads from the server's disk (harmless for one that isn't downloaded). */
export async function deleteServerDownloads(ids: number[], client: SuwayomiClient = defaultClient()): Promise<void> {
    if (ids.length === 0) return;
    await client.query('mutation D($ids: [Int!]!) { deleteDownloadedChapters(input: { ids: $ids }) { chapters { id } } }', { ids });
}

export interface ServerChapter {
    id: number;
    sourceOrder: number;
    name: string;
    isRead: boolean;
    isBookmarked: boolean;
    isDownloaded: boolean;
}

/** A manga's chapters on the server, oldest first. */
export async function serverChapters(mangaId: number, client: SuwayomiClient = defaultClient()): Promise<ServerChapter[]> {
    const data = await client.query<{ chapters: { nodes: ServerChapter[] } }>(
        `query C($m: Int!) { chapters(condition: { mangaId: $m }, order: [{ by: SOURCE_ORDER }]) {
            nodes { id sourceOrder name isRead isBookmarked isDownloaded } } }`, { m: mangaId });
    return data.chapters.nodes;
}

/** Which of a manga's chapters (by sourceOrder) are downloaded on the server. */
export async function downloadedOrders(mangaId: number, client: SuwayomiClient = defaultClient()): Promise<number[]> {
    return (await serverChapters(mangaId, client)).filter((c) => c.isDownloaded).map((c) => c.sourceOrder);
}

/**
 * A test-source manga's id with its chapters on the server (loaded from the source when the server has none yet), oldest
 * first.
 */
export async function sourceMangaWithChapters(key: string, client: SuwayomiClient = defaultClient()): Promise<{ id: number; chapters: ServerChapter[] }> {
    const id = await findSourceManga(TEST_SOURCE_IDS.en, key, client);
    let chapters = await serverChapters(id, client);
    if (chapters.length === 0) {
        await client.query('mutation F($id: Int!) { fetchChapters(input: { mangaId: $id }) { chapters { id } } }', { id });
        chapters = await serverChapters(id, client);
    }
    return { id, chapters };
}

/** Marks chapters on the server (read state, bookmark, page). */
export async function patchChapters(ids: number[], patch: { isRead?: boolean; isBookmarked?: boolean; lastPageRead?: number },
    client: SuwayomiClient = defaultClient()): Promise<void> {
    await client.query('mutation U($ids: [Int!]!, $patch: UpdateChapterPatchInput!) { updateChapters(input: { ids: $ids, patch: $patch }) { chapters { id } } }',
        { ids, patch });
}

/**
 * The known state for the scenario (first, last, and after a failure): an empty, stopped queue, none of `mangaIds`' chapters
 * downloaded on the server, Paper Garden's chapters unread, without bookmarks, at page 0.
 */
export async function serverDownloadsKnownState(mangaIds: number[], client: SuwayomiClient = defaultClient()): Promise<void> {
    await clearServerQueue(client);
    for (const id of mangaIds) {
        const chapters = await serverChapters(id, client);
        await deleteServerDownloads(chapters.filter((c) => c.isDownloaded).map((c) => c.id), client);
        if (chapters.length > 0) {
            await patchChapters(chapters.map((c) => c.id), { isRead: false, isBookmarked: false, lastPageRead: 0 }, client);
        }
    }
}

/** One queue change in the server's news: a chapter and what happened to it (QUEUED, DEQUEUED, POSITION, PROGRESS, ...). */
export interface QueueNewsChange {
    chapterId: number;
    type: string;
}

/**
 * IS-007: the server's own news of its queue (the GraphQL subscription `downloadStatusChanged` over a WebSocket, what the
 * web UI listens to). The server gathers queue changes and sends them out as one batch on its 1 s beat. Its Move to top
 * (`reorderChapterDownload` with `to` 0) answers only from a batch whose first change is the moved chapter (or that
 * doesn't name it): when another chapter's change waits in the same batch, say a move a moment before, the answer is
 * held 30 s and then fails (v2.4.2366). The app shows the move anyway (its check read, WP-17), but its request stays
 * open, so the app isn't idle. A scenario that moves rows one after the other waits here until the server has sent the
 * last move out. Auth none only (the e2e runs' test server).
 */
export class ServerQueueNews {
    private readonly batches: { at: number; changes: QueueNewsChange[] }[] = [];
    private closed = false;
    private readonly ws: WebSocket;

    private constructor(ws: WebSocket) {
        this.ws = ws;
    }

    /** Subscribes; resolves once the server has sent its first message (the queue as it is), so no later batch is missed. */
    static async open(options = optionsFromEnv(), timeoutMs = 10_000): Promise<ServerQueueNews> {
        if (options.auth !== 'none') throw new Error(`ServerQueueNews needs the test server without a login, not ${options.auth}`);
        const ws = new WebSocket(`${options.url.replace(/^http/, 'ws')}/api/graphql`, 'graphql-transport-ws');
        const news = new ServerQueueNews(ws);
        await new Promise<void>((resolve, reject) => {
            const timer = setTimeout(() => reject(new Error(`the server's queue news didn't start within ${timeoutMs} ms`)), timeoutMs);
            ws.onerror = () => { clearTimeout(timer); reject(new Error(`the server's queue news: no WebSocket to ${ws.url}`)); };
            ws.onopen = () => ws.send(JSON.stringify({ type: 'connection_init', payload: {} }));
            ws.onclose = () => { news.closed = true; };
            ws.onmessage = (message) => {
                const msg = JSON.parse(String(message.data)) as { type: string; payload?: unknown };
                if (msg.type === 'connection_ack') {
                    ws.send(JSON.stringify({ id: '1', type: 'subscribe', payload: { query:
                        'subscription { downloadStatusChanged(input: {}) { updates { type download { chapter { id } } } } }' } }));
                } else if (msg.type === 'next') {
                    const data = (msg.payload as { data?: { downloadStatusChanged?: { updates?: { type: string; download: { chapter: { id: number } } }[] } } }).data;
                    const updates = data?.downloadStatusChanged?.updates ?? [];
                    news.batches.push({ at: Date.now(), changes: updates.map((u) => ({ chapterId: u.download.chapter.id, type: u.type })) });
                    clearTimeout(timer);
                    resolve();
                } else if (msg.type === 'error') {
                    clearTimeout(timer);
                    reject(new Error(`the server's queue news failed: ${JSON.stringify(msg.payload).slice(0, 300)}`));
                }
            };
        });
        return news;
    }

    /** Resolves once a batch that came after `since` (a Date.now()) has `type` for `chapterId`; throws after `timeoutMs`. */
    async waitFor(what: string, chapterId: number, type: string, since: number, timeoutMs = 5_000): Promise<void> {
        const until = Date.now() + timeoutMs;
        for (;;) {
            if (this.batches.some((b) => b.at >= since && b.changes.some((c) => c.chapterId === chapterId && c.type === type))) return;
            if (this.closed) throw new Error(`the server's queue news ended before "${what}"`);
            if (Date.now() > until) {
                const last = this.batches.slice(-3).map((b) => JSON.stringify(b.changes)).join(' ');
                throw new Error(`the server didn't send "${what}" within ${timeoutMs} ms (last batches: ${last})`);
            }
            await new Promise((r) => setTimeout(r, 50));
        }
    }

    close(): void {
        this.closed = true;
        this.ws.close();
    }
}

/** Asks the server every 500 ms until `done` accepts the answer of `read`, or throws after `timeoutMs`. */
export async function untilServer<T>(what: string, read: () => Promise<T>, done: (value: T) => boolean, timeoutMs = 60_000): Promise<T> {
    const until = Date.now() + timeoutMs;
    for (;;) {
        const value = await read();
        if (done(value)) return value;
        if (Date.now() > until) throw new Error(`the server didn't get to "${what}" in ${timeoutMs} ms (last: ${JSON.stringify(value)})`);
        await new Promise((r) => setTimeout(r, 500));
    }
}
