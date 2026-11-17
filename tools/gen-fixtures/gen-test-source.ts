// Writes the test source's web folder (WP-23, D55): the store and the test site that jwebserver serves on 127.0.0.1:4569.
//   <out>/store/index.json   the store index (offers SNX Test v1)       <out>/store/build.json  what build-source compiles
//   <out>/store/icon/<pkg>.png  extension icons                         (the jars: test-server.ps1 build-source, not here)
//   <out>/site/catalog-<id>.json  one per source                        <out>/site/covers/<key>.jpg
//   <out>/site/pages/<set>/c<chapter>/<nnn>.<ext>                       (site/control.json: written by tests, never here)
// Same bytes on every run. Node writes the JSON: a byte-order mark (PowerShell 5.1 writes one) breaks the server's index read.
// Prints a digest of all files.
//
// Usage: node tools/gen-fixtures/gen-test-source.ts --out <web dir> [--sums <file>]
//        node tools/gen-fixtures/gen-test-source.ts --reset-store <web dir>   (index back on SNX Test v1, control.json gone)

import { createHash } from 'node:crypto';
import { utimesSync, writeFileSync } from 'node:fs';
import { mkdir, rm, writeFile } from 'node:fs/promises';
import { availableParallelism } from 'node:os';
import { dirname, join } from 'node:path';
import { parseArgs } from 'node:util';
import { isMain } from '../../tests/e2e/lib/cli.ts';
import type { SeriesDef } from './fixtures.ts';
import { inParallel, prepareOut } from './gen-fixtures.ts';
import { Canvas, type Rgb } from './raster.ts';
import { drawPanel, encode, pageBytes } from './render.ts';
import { ALL_EXTENSIONS, buildList, CATALOGS, COVER_CODE, COVER_SIZE, type CatalogManga, coverNumber, PAGE_SETS, pagePath, SITE_URL,
    storeIndex } from './test-source.ts';

const MARKER = '.snx-fixtures'; // the same marker as gen-fixtures: prepareOut only deletes folders that have it
const MARKER_TEXT = 'Written by tools/gen-fixtures/gen-test-source.ts. The tool deletes and rewrites this folder.\n';

/** JSON as the server and jwebserver want it: UTF-8, no byte-order mark, one trailing newline. */
export function jsonText(value: unknown): string {
    return JSON.stringify(value, null, 2) + '\n';
}

export function coverBackground(n: number): Rgb {
    return [200 + ((n * 37) % 56), 200 + ((n * 71) % 56), 200 + ((n * 13) % 56)];
}

/** A cover: the manga's key in big letters and a page code with series 14 and the cover number as its chapter. */
export async function testCoverBytes(m: CatalogManga): Promise<Buffer> {
    const n = coverNumber(m.key);
    const { width, height } = COVER_SIZE;
    const c = new Canvas(width, height, coverBackground(n));
    drawPanel(c, 0, 0, width, height, { series: COVER_CODE, chapter: n, page: 0, segment: 0 }, [m.key.toUpperCase(), 'COVER'],
        `SNX TEST SOURCE COVER ${n}`);
    return encode(c, 'jpeg');
}

/** A 96x96 extension icon: a coloured square with the extension's initials. */
export async function iconBytes(index: number, initials: string): Promise<Buffer> {
    const colours: Rgb[] = [[90, 140, 230], [80, 180, 120], [220, 90, 110]];
    const c = new Canvas(96, 96, colours[index % colours.length]);
    c.drawText(initials, 12, 36, 4, [255, 255, 255]);
    return encode(c, 'png');
}

export interface SiteResult {
    files: number;
    bytes: number;
    digest: string;
    sums: string;
}

export async function generateSite(out: string): Promise<SiteResult> {
    await prepareOut(out);
    const hashes = new Map<string, string>();
    let bytes = 0;
    const put = async (rel: string, data: Buffer | string) => {
        const buf = typeof data === 'string' ? Buffer.from(data, 'utf8') : data;
        await mkdir(dirname(join(out, rel)), { recursive: true });
        await writeFile(join(out, rel), buf);
        hashes.set(rel, createHash('sha256').update(buf).digest('hex'));
        bytes += buf.length;
    };

    await put(MARKER, MARKER_TEXT);
    await put('store/index.json', jsonText(storeIndex(1)));
    writeStoreIndex(out, 1); // the same bytes again, with the future file date
    await put('store/build.json', jsonText(buildList()));
    for (const [i, ext] of ALL_EXTENSIONS.entries()) {
        await put(`store/icon/${ext.pkg}.png`, await iconBytes(i, ext.name.split(' ').map((w) => w[0]).join('')));
    }
    const tasks: (() => Promise<void>)[] = [];
    for (const catalog of CATALOGS) {
        await put(`site/catalog-${catalog.id}.json`, jsonText(catalog));
        for (const m of catalog.manga) {
            if (m.cover?.startsWith(`${SITE_URL}/covers/${m.key}.`)) tasks.push(async () => put(`site/covers/${m.key}.jpg`, await testCoverBytes(m)));
        }
    }
    for (const set of Object.values(PAGE_SETS) as SeriesDef[]) {
        for (let ch = 1; ch <= set.chapters; ch++) {
            for (let p = 1; p <= set.pages; p++) tasks.push(async () => put(`site/${pagePath(set, ch, p)}`, await pageBytes(set, ch, p)));
        }
    }
    await inParallel(tasks, Math.min(8, availableParallelism()));

    const paths = [...hashes.keys()].sort();
    const sums = paths.map((p) => `${hashes.get(p)}  ${p}\n`).join('');
    return { files: paths.length, bytes, digest: createHash('sha256').update(sums).digest('hex'), sums };
}

/**
 * Writes store/index.json offering SNX Test `testVersion`. Its file date goes 100 years into the future: jwebserver sends it
 * as Last-Modified, and OkHttp (the server's HTTP client) keeps a copy it may reuse without asking for 10 % of the file's
 * age; a date after "now" gives it no such time, so the server always sees the index we wrote last. Every write gets a later
 * date than the one before.
 */
export function writeStoreIndex(webDir: string, testVersion = 1): void {
    const file = join(webDir, 'store', 'index.json');
    writeFileSync(file, jsonText(storeIndex(testVersion)));
    const future = new Date(Date.now() + 100 * 365 * 86_400_000);
    utimesSync(file, future, future);
}

/** Puts the store back on SNX Test v1 and removes the switch file (what `test-server.sh reset` and resetTestSource() want). */
export async function resetStore(out: string): Promise<void> {
    writeStoreIndex(out, 1);
    await rm(join(out, 'site', 'control.json'), { force: true });
}

async function main(): Promise<void> {
    const { values } = parseArgs({ options: { out: { type: 'string' }, sums: { type: 'string' }, 'reset-store': { type: 'string' } } });
    if (values['reset-store']) {
        await resetStore(values['reset-store']);
        return;
    }
    if (!values.out) {
        console.error('usage: node tools/gen-fixtures/gen-test-source.ts --out <web dir> [--sums <file>] | --reset-store <web dir>');
        process.exit(2);
    }
    const started = Date.now();
    const result = await generateSite(values.out);
    if (values.sums) await writeFile(values.sums, result.sums);
    console.log(`test source site: ${result.files} files, ${(result.bytes / 1e6).toFixed(1)} MB in ` +
        `${((Date.now() - started) / 1000).toFixed(1)} s -> ${values.out}`);
    console.log(`digest: ${result.digest}`);
}

if (isMain(import.meta.url)) await main();
