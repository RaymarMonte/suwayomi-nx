// Played touches (WP-13, TESTING.md "Drivers"): the drivers touch with one finger, so a pinch or a double tap goes through
// the test build's touch channel instead. The harness writes test/commands/<n>.json (n = 1, 2, ... in every launch), the app
// plays its frames through the same hit test and gesture recognizers as real fingers, deletes the file and writes
// {"ev":"command","id":n,"ok":...}. Positions are landscape screen units (1280 x 720), like the drivers' touch().

import type { AppEvent, EventStream } from './events.ts';
import type { Scenario } from './scenario.ts';

export type Point = { x: number; y: number };
export type Finger = { id: number; x: number; y: number };
/** From `ms` after the command starts, these fingers are down here; a finger missing from the next frame lifts. */
export type TouchFrame = { ms: number; fingers: Finger[] };

/** The frame interval the builders use (one frame at 60 fps). */
export const FRAME_MS = 16;

const round = (v: number) => Math.round(v * 10) / 10;

function finger(id: number, p: Point): Finger {
    return { id, x: round(p.x), y: round(p.y) };
}

/**
 * Two fingers side by side around `center`, `from` units apart, moving apart (or together) to `to` units over `ms`, then held
 * there for two more frames before they lift. The midpoint stays put, so only the zoom changes: zoom × to / from.
 * `map` turns the points first (portraitPoint for the portrait reader: then `center` and the distances are reader units).
 */
export function pinchFrames(center: Point, from: number, to: number, ms = 400,
    map: (p: Point) => Point = (p) => p): TouchFrame[] {
    const steps = Math.max(1, Math.round(ms / FRAME_MS));
    const frames: TouchFrame[] = [];
    const at = (distance: number, t: number): TouchFrame => ({
        ms: Math.round(t),
        fingers: [
            finger(0, map({ x: center.x - distance / 2, y: center.y })),
            finger(1, map({ x: center.x + distance / 2, y: center.y })),
        ],
    });
    for (let i = 0; i <= steps; i++) {
        frames.push(at(from + ((to - from) * i) / steps, (ms * i) / steps));
    }
    frames.push(at(to, ms + FRAME_MS), at(to, ms + 2 * FRAME_MS));
    return frames;
}

/**
 * Two quick taps on one spot: each finger down for 50 ms, 80 ms apart (the reader takes two taps within 250 ms and 40 units
 * of each other for a double tap). `map` as in pinchFrames.
 */
export function doubleTapFrames(at: Point, map: (p: Point) => Point = (p) => p): TouchFrame[] {
    const p = map(at);
    return [
        { ms: 0, fingers: [finger(0, p)] },
        { ms: 50, fingers: [] },
        { ms: 130, fingers: [finger(0, p)] },
        { ms: 180, fingers: [] },
    ];
}

/** One finger from `from` to `to` over `ms` (a drag), held at the end for a frame. `map` as in pinchFrames. */
export function dragFrames(from: Point, to: Point, ms = 250, map: (p: Point) => Point = (p) => p): TouchFrame[] {
    const steps = Math.max(1, Math.round(ms / FRAME_MS));
    const frames: TouchFrame[] = [];
    for (let i = 0; i <= steps; i++) {
        const p = { x: from.x + ((to.x - from.x) * i) / steps, y: from.y + ((to.y - from.y) * i) / steps };
        frames.push({ ms: Math.round((ms * i) / steps), fingers: [finger(0, map(p))] });
    }
    frames.push({ ms: ms + FRAME_MS, fingers: [finger(0, map(to))] });
    return frames;
}

// The next command number per launch (every launch has its own event stream).
const nextCommand = new WeakMap<EventStream, number>();

/** The command file for `frames` (what the app reads). */
export function touchesCommand(frames: TouchFrame[]): string {
    return JSON.stringify({ cmd: 'touches', frames });
}

/** Plays `frames` in the running app and waits until it has (its "command" event, which must say ok). */
export async function playTouches(s: Scenario, frames: TouchFrame[]): Promise<AppEvent> {
    const events = s.events;
    const id = nextCommand.get(events) ?? 1;
    nextCommand.set(events, id + 1);
    const mark = events.mark();
    await s.driver.writeFile(`test/commands/${id}.json`, touchesCommand(frames));
    const last = frames.at(-1)?.ms ?? 0;
    const done = await events.waitForEvent('command', { id }, { from: mark, timeoutMs: 10_000 + last });
    if (done.ok !== true) {
        throw new Error(`touch command ${id} failed: ${String(done.error ?? 'no reason given')}`);
    }
    return done;
}

/** Actions for readerAfter(): a played pinch, double tap or drag. */
export function pinch(s: Scenario, center: Point, from: number, to: number, map?: (p: Point) => Point): () => Promise<void> {
    return async () => { await playTouches(s, pinchFrames(center, from, to, 400, map)); };
}

export function doubleTap(s: Scenario, at: Point, map?: (p: Point) => Point): () => Promise<void> {
    return async () => { await playTouches(s, doubleTapFrames(at, map)); };
}

export function drag(s: Scenario, from: Point, to: Point, map?: (p: Point) => Point): () => Promise<void> {
    return async () => { await playTouches(s, dragFrames(from, to, 250, map)); };
}
