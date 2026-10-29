// Writes the fixture manga (fixtures.ts) as Local Source folders:
//   <out>/<Title>/details.json, cover.jpg, Chapter <n>/<001.jpg ...>
// Same bytes and same file times on every run, so the server reports the same data every time.
// Prints a digest of all files: two runs with the same digest produced identical files.
//
// Usage: node tools/gen-fixtures/gen-fixtures.ts --out <dir> [--sums <file>]
//   --out   target folder. It must be new, empty, or an earlier output of this tool (then it's replaced).
//   --sums  also write "sha256  path" lines for every file (to compare two runs file by file)

import { createHash } from 'node:crypto';
import { existsSync, readdirSync } from 'node:fs';
import { mkdir, rm, utimes, writeFile } from 'node:fs/promises';
import { availableParallelism } from 'node:os';
import { join } from 'node:path';
import { parseArgs } from 'node:util';
import { isMain } from '../../tests/e2e/lib/cli.ts';
import { ALL_SERIES, chapterFolder, chapterTime, EPOCH_MS, pageFile, seriesTime } from './fixtures.ts';
import { coverBytes, pageBytes } from './render.ts';

const MARKER = '.snx-fixtures';
const MARKER_TEXT = 'Written by tools/gen-fixtures. The tool deletes and rewrites this folder.\n';

/** Makes `out` an empty folder, refusing to delete anything this tool didn't write. */
async function prepareOut(out: string): Promise<void> {
    if (existsSync(out)) {
        const entries = readdirSync(out);
        if (entries.length > 0 && !entries.includes(MARKER)) {
            throw new Error(`${out} is not empty and wasn't written by gen-fixtures: refusing to delete it`);
        }
        await rm(out, { recursive: true, force: true });
    }
    await mkdir(out, { recursive: true });
}

/** Runs the tasks with at most `limit` in flight. */
async function inParallel(tasks: (() => Promise<void>)[], limit: number): Promise<void> {
    let next = 0;
    const worker = async () => {
        while (next < tasks.length) await tasks[next++]();
    };
    await Promise.all(Array.from({ length: Math.min(limit, tasks.length) }, worker));
}

export interface GenerateResult {
    files: number;
    bytes: number;
    digest: string;
    sums: string; // "sha256  relative/path" lines, sorted by path
}

export async function generate(out: string): Promise<GenerateResult> {
    await prepareOut(out);
    const hashes = new Map<string, string>();
    let bytes = 0;
    const put = async (rel: string, data: Buffer | string, time: Date) => {
        const buf = typeof data === 'string' ? Buffer.from(data, 'utf8') : data;
        await writeFile(join(out, rel), buf);
        await utimes(join(out, rel), time, time);
        hashes.set(rel, createHash('sha256').update(buf).digest('hex'));
        bytes += buf.length;
    };

    await put(MARKER, MARKER_TEXT, new Date(EPOCH_MS));
    for (const s of ALL_SERIES) {
        await mkdir(join(out, s.title));
        if (s.details) await put(`${s.title}/details.json`, JSON.stringify(s.details, null, 2) + '\n', seriesTime(s));
        if (s.cover) await put(`${s.title}/cover.jpg`, await coverBytes(s), seriesTime(s));
        const tasks: (() => Promise<void>)[] = [];
        for (let ch = 1; ch <= s.chapters; ch++) {
            await mkdir(join(out, s.title, chapterFolder(ch)));
            for (let p = 1; p <= s.pages; p++) {
                tasks.push(async () => put(`${s.title}/${chapterFolder(ch)}/${pageFile(p, s.format)}`, await pageBytes(s, ch, p), chapterTime(s, ch)));
            }
        }
        await inParallel(tasks, Math.min(8, availableParallelism()));
        // Folder times last: writing a file into a folder changes the folder's time.
        for (let ch = 1; ch <= s.chapters; ch++) await utimes(join(out, s.title, chapterFolder(ch)), chapterTime(s, ch), chapterTime(s, ch));
        await utimes(join(out, s.title), seriesTime(s), seriesTime(s));
    }
    await utimes(out, new Date(EPOCH_MS), new Date(EPOCH_MS));

    const paths = [...hashes.keys()].sort();
    const sums = paths.map((p) => `${hashes.get(p)}  ${p}\n`).join('');
    return { files: paths.length, bytes, digest: createHash('sha256').update(sums).digest('hex'), sums };
}

async function main(): Promise<void> {
    const { values } = parseArgs({ options: { out: { type: 'string' }, sums: { type: 'string' } } });
    if (!values.out) {
        console.error('usage: node tools/gen-fixtures/gen-fixtures.ts --out <dir> [--sums <file>]');
        process.exit(2);
    }
    const started = Date.now();
    const result = await generate(values.out);
    if (values.sums) await writeFile(values.sums, result.sums);
    const mb = (result.bytes / 1e6).toFixed(1);
    console.log(`fixtures: ${result.files} files, ${mb} MB in ${((Date.now() - started) / 1000).toFixed(1)} s -> ${values.out}`);
    console.log(`digest: ${result.digest}`);
}

if (isMain(import.meta.url)) await main();
