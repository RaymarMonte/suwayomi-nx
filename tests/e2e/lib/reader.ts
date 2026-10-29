// Steps the reader scenarios share (WP-11): open a manga from a library tab, wait for what the reader shows ("reader"
// events), read the fixture page on screen from its colour code, and change a reader setting through the menu.

import sharp from 'sharp';
import { expect } from 'vitest';

import type { AppEvent } from './events.ts';
import { readPageCode, type PageCode } from './pagecode.ts';
import type { Scenario } from './scenario.ts';
import { LIBRARY, MANGA, READER } from './screens.ts';

type Point = { x: number; y: number };

/** After a launch: waits until the library shows its first category (`fields` more about it) and the app has settled. */
export async function waitForLibrary(s: Scenario, fields: Record<string, unknown> = {}): Promise<AppEvent> {
    const library = await s.events.waitForEvent('library', { state: 'ready', ...fields }, { timeoutMs: 30_000 });
    await s.events.waitForEvent('idle', {}, { from: s.events.events.indexOf(library) });
    return library;
}

/** Taps a library category tab, waits for it, then taps cover `cover` and waits until the details screen is complete. */
export async function openManga(s: Scenario, category: Point | null, cover: number, items?: number): Promise<AppEvent> {
    let mark = s.events.mark();
    if (category) {
        await s.driver.touch(category.x, category.y);
        await s.events.waitForEvent('library', items === undefined ? { state: 'ready' } : { state: 'ready', items }, { from: mark });
        await s.events.waitForEvent('idle', {}, { from: mark });
    }
    mark = s.events.mark();
    await s.driver.touch(LIBRARY.cover(cover).x, LIBRARY.cover(cover).y);
    const shown = await s.events.waitForEvent('manga', { state: 'ready', complete: true }, { from: mark, timeoutMs: 30_000 });
    await s.events.waitForEvent('idle', {}, { from: mark });
    return shown;
}

/** Does `action`, then waits for the "reader" event that matches `fields` and for the app to settle. Returns the event. */
export async function readerAfter(s: Scenario, action: () => Promise<void>, fields: Record<string, unknown>): Promise<AppEvent> {
    const mark = s.events.mark();
    await action();
    const shown = await s.events.waitForEvent('reader', fields, { from: mark, timeoutMs: 30_000 });
    await s.events.waitForEvent('idle', {}, { from: mark });
    return shown;
}

/** Opens chapter row `row` of the details screen (newest first) and waits until the reader shows something settled. */
export async function openChapter(s: Scenario, row: number, fields: Record<string, unknown> = {}): Promise<AppEvent> {
    return readerAfter(s, () => s.driver.touch(MANGA.row(row).x, MANGA.row(row).y), fields);
}

export function tap(s: Scenario, at: Point): () => Promise<void> {
    return () => s.driver.touch(at.x, at.y);
}

export function swipe(s: Scenario, move: { x1: number; y1: number; x2: number; y2: number }): () => Promise<void> {
    return () => s.driver.swipe(move.x1, move.y1, move.x2, move.y2, 250, 12);
}

/** The fixture page on screen, read from its colour code (D31), or null if none (or more than one) can be read. */
export async function pageOnScreen(s: Scenario): Promise<PageCode | null> {
    const shot = await s.driver.screenshot();
    const { data, info } = await sharp(shot).removeAlpha().raw().toBuffer({ resolveWithObject: true });
    return readPageCode({ width: info.width, height: info.height, channels: info.channels, data });
}

/** Expects fixture page `page` (1-based, as printed) of `series`' chapter `chapter` on screen. */
export async function expectPage(s: Scenario, series: number, chapter: number, page: number): Promise<void> {
    const code = await pageOnScreen(s);
    expect(code, `page code on screen (series ${series}, chapter ${chapter}, page ${page})`)
        .toMatchObject({ series, chapter, page });
}

/**
 * Changes a reader setting with the menu: opens the menu (a tap in the middle) unless it is open, taps Settings, entry `entry`
 * of the settings menu, then choice `choice` of `count` in the second menu (no second menu for the page number, entry 3).
 * Waits for the "reader" event with `fields`.
 */
export async function chooseSetting(s: Scenario, entry: number, choice: number, count: number,
    fields: Record<string, unknown>): Promise<AppEvent> {
    const last = s.events.events.filter((e) => e.ev === 'reader').at(-1);
    if (!last?.menu) {
        await readerAfter(s, tap(s, READER.middle), { menu: true });
    }
    let mark = s.events.mark();
    await s.driver.touch(READER.settings.x, READER.settings.y);
    await s.events.waitForEvent('idle', {}, { from: mark });
    const first = MANGA.menuEntry(entry, READER.settingsCount);
    if (entry === 3) {
        return readerAfter(s, () => s.driver.touch(first.x, first.y), fields);
    }
    mark = s.events.mark();
    await s.driver.touch(first.x, first.y);
    await s.events.waitForEvent('idle', {}, { from: mark });
    const second = MANGA.menuEntry(choice, count);
    return readerAfter(s, () => s.driver.touch(second.x, second.y), fields);
}

/** Median and 90th percentile of a list of numbers (page turn times). */
export function stats(values: number[]): { median: number; p90: number; max: number } {
    const sorted = [...values].sort((a, b) => a - b);
    const at = (q: number) => sorted[Math.min(sorted.length - 1, Math.floor(q * sorted.length))] ?? 0;
    return { median: at(0.5), p90: at(0.9), max: sorted.at(-1) ?? 0 };
}
