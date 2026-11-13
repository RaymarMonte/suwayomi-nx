// Checks Epsilon Weekly on a live, freshly seeded test server (WP-15): seeded state, then the new chapter appearing after a
// library update and going away again. Needs the running test server, so it only runs when asked:
//   eval "$(tools/test-server/test-server.sh env)"; SNX_LIVE_SERVER=1 npx vitest run tools/test-server/epsilon.test.ts
// It leaves the server as it found it (chapter 3 removed again).

import { describe, expect, it } from 'vitest';
import { addNewChapter, EPSILON_MANGA_ID, optionsFromEnv, removeNewChapter, SuwayomiClient } from '../../tests/e2e/lib/suwayomi.ts';

interface EpsilonState {
    manga: {
        inLibrary: boolean;
        inLibraryAt: string;
        categories: { nodes: { name: string }[] };
        chapters: { nodes: { id: number; isRead: boolean; lastReadAt: string; fetchedAt: string }[] };
    };
}

describe.runIf(process.env.SNX_LIVE_SERVER === '1')('Epsilon Weekly on the live test server', () => {
    const client = new SuwayomiClient(optionsFromEnv());
    const state = () => client.query<EpsilonState>(
        `query($id: Int!) { manga(id: $id) { inLibrary inLibraryAt categories { nodes { name } }
            chapters { nodes { id isRead lastReadAt fetchedAt } } } }`, { id: EPSILON_MANGA_ID });

    it('is seeded as manga 157 in Reading, chapters 460-461 read, with an empty History', async () => {
        await removeNewChapter(client); // safe when chapter 3 isn't there
        const { manga } = await state();
        expect(manga.inLibrary).toBe(true);
        expect(manga.categories.nodes.map((c) => c.name)).toEqual(['Reading']);
        expect(manga.chapters.nodes.map((c) => c.id)).toEqual([460, 461]);
        expect(manga.chapters.nodes.every((c) => c.isRead && c.lastReadAt === '0')).toBe(true);
    });

    it('finds chapter 462 after addNewChapter + a library update, and forgets it after removeNewChapter', async () => {
        addNewChapter();
        addNewChapter(); // twice is fine
        await client.query('mutation { updateLibrary(input: {}) { updateStatus { jobsInfo { isRunning } } } }');
        let chapters: EpsilonState['manga']['chapters']['nodes'] = [];
        let inLibraryAt = '0';
        for (let i = 0; i < 30 && chapters.length < 3; i++) {
            await new Promise((r) => setTimeout(r, 1000));
            const { manga } = await state();
            chapters = manga.chapters.nodes;
            inLibraryAt = manga.inLibraryAt;
        }
        expect(chapters.map((c) => c.id)).toEqual([460, 461, 462]);
        expect(Number(chapters[2].fetchedAt)).toBeGreaterThan(Number(inLibraryAt));
        expect(chapters[2].isRead).toBe(false);

        await removeNewChapter(client);
        await removeNewChapter(client); // twice is fine
        expect((await state()).manga.chapters.nodes.map((c) => c.id)).toEqual([460, 461]);
    }, 60_000);
});
