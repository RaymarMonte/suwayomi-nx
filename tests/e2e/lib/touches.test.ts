import { describe, expect, it } from 'vitest';

import { EventStream, type EventSource } from './events.ts';
import type { Scenario } from './scenario.ts';
import { portraitPoint } from './screens.ts';
import { doubleTapFrames, dragFrames, FRAME_MS, pinchFrames, playTouches, touchesCommand, type TouchFrame } from './touches.ts';

const distance = (f: TouchFrame) => Math.hypot(f.fingers[1].x - f.fingers[0].x, f.fingers[1].y - f.fingers[0].y);
const midpoint = (f: TouchFrame) => ({ x: (f.fingers[0].x + f.fingers[1].x) / 2, y: (f.fingers[0].y + f.fingers[1].y) / 2 });

describe('pinchFrames', () => {
    it('spreads two fingers from one distance to the other around a fixed midpoint, then holds', () => {
        const frames = pinchFrames({ x: 640, y: 360 }, 100, 300, 400);
        expect(frames[0].ms).toBe(0);
        expect(distance(frames[0])).toBeCloseTo(100, 5);
        const end = frames.length - 3;
        expect(frames[end].ms).toBe(400);
        expect(distance(frames[end])).toBeCloseTo(300, 5);
        expect(frames.slice(end).map(distance)).toEqual([300, 300, 300]);
        expect(frames.at(-1)?.ms).toBe(400 + 2 * FRAME_MS);
        for (const f of frames) {
            expect(f.fingers.map((g) => g.id)).toEqual([0, 1]);
            expect(midpoint(f)).toEqual({ x: 640, y: 360 });
        }
        for (let i = 1; i < frames.length; i++) {
            expect(frames[i].ms).toBeGreaterThan(frames[i - 1].ms);
            expect(frames[i].ms - frames[i - 1].ms).toBeLessThanOrEqual(FRAME_MS + 1);
        }
    });

    it('pinches together too, and turns its points for portrait', () => {
        const frames = pinchFrames({ x: 360, y: 640 }, 300, 100, 200, (p) => portraitPoint('portrait-right', p));
        expect(distance(frames[0])).toBeCloseTo(300, 5);
        expect(distance(frames.at(-1)!)).toBeCloseTo(100, 5);
        // Turned right, the reader's (360, 640) is the landscape screen's middle; the fingers lie along its height.
        expect(midpoint(frames[0])).toEqual({ x: 640, y: 360 });
        expect(frames[0].fingers[0].x).toBe(frames[0].fingers[1].x);
    });
});

describe('doubleTapFrames', () => {
    it('is two short taps on one spot, the second within 250 ms of the first lifting', () => {
        const frames = doubleTapFrames({ x: 640, y: 200 });
        expect(frames.map((f) => f.fingers.length)).toEqual([1, 0, 1, 0]);
        expect(frames[0].fingers[0]).toEqual({ id: 0, x: 640, y: 200 });
        expect(frames[2].fingers[0]).toEqual({ id: 0, x: 640, y: 200 });
        expect(frames[2].ms - frames[1].ms).toBeLessThan(250);
        expect(frames[3].ms - frames[2].ms).toBeLessThan(150);  // a tap, not a hold
    });
});

describe('dragFrames', () => {
    it('moves one finger in even steps and holds it at the end', () => {
        const frames = dragFrames({ x: 300, y: 380 }, { x: 900, y: 380 }, 240);
        expect(frames[0].fingers).toEqual([{ id: 0, x: 300, y: 380 }]);
        expect(frames.at(-1)?.fingers).toEqual([{ id: 0, x: 900, y: 380 }]);
        expect(frames.at(-2)?.fingers).toEqual([{ id: 0, x: 900, y: 380 }]);
        expect(frames.every((f) => f.fingers.length === 1)).toBe(true);
    });
});

describe('playTouches', () => {
    /** An events file in memory and a driver whose writeFile answers like the app does. */
    function fakeScenario(answer: (id: number) => Record<string, unknown>) {
        let data = '';
        const source: EventSource = {
            read: async (offset: number) => Buffer.from(data, 'utf8').subarray(offset),
            appGone: async () => null,
        };
        const stream = new EventStream(source, { pollMs: 5, gonePollMs: 5 });
        const written: { path: string; data: string }[] = [];
        const driver = {
            writeFile: async (path: string, contents: string) => {
                written.push({ path, data: contents });
                const id = Number(/(\d+)\.json$/.exec(path)?.[1]);
                data += JSON.stringify({ t: written.length, ev: 'command', id, ...answer(id) }) + '\n';
            },
        };
        const s = { driver, events: stream } as unknown as Scenario;
        return { s, written };
    }

    it('numbers the commands of a launch from 1 and waits for each one to be played', async () => {
        const { s, written } = fakeScenario(() => ({ ok: true }));
        const frames = doubleTapFrames({ x: 10, y: 20 });
        expect((await playTouches(s, frames)).id).toBe(1);
        expect((await playTouches(s, frames)).id).toBe(2);
        expect(written.map((w) => w.path)).toEqual(['test/commands/1.json', 'test/commands/2.json']);
        expect(JSON.parse(written[0].data)).toEqual({ cmd: 'touches', frames });
        expect(written[0].data).toBe(touchesCommand(frames));
    });

    it('fails when the app says the command failed', async () => {
        const { s } = fakeScenario(() => ({ ok: false, error: 'unknown command' }));
        await expect(playTouches(s, [])).rejects.toThrow(/unknown command/);
    });
});
