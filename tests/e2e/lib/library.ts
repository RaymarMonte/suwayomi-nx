// Steps on the Library tab that scenarios share (WP-09, moved here in WP-12 for the soak): an input that may get lost on the
// Switch, and flicking the cover grid until it gets somewhere.

import { ConsoleUnreachableError } from '../drivers/autopilot.ts';
import type { AppEvent } from './events.ts';
import type { Scenario } from './scenario.ts';
import { LIBRARY } from './screens.ts';

/**
 * One input inside a loop that checks the app's state before every step (walkTo, flickUntil), so an input that got lost or
 * went through twice doesn't matter. On the Switch, sys-autopilot sometimes resets the connection of an input request; the
 * driver never repeats those (a press could happen twice), but here that is safe: wait a moment and look again. Returns
 * false when the input may not have happened.
 */
export async function inputInLoop(s: Scenario, input: () => Promise<void>): Promise<boolean> {
    try {
        await input();
        return true;
    } catch (e) {
        if (!(e instanceof ConsoleUnreachableError)) throw e;
        console.warn(`[${s.name}] ${e.message}: looking again in 2 s`);
        await new Promise((r) => setTimeout(r, 2_000));
        return false;
    }
}

/** Flicks over the grid (up to scroll down, or the other way) until the latest "grid" event says `done`. */
export async function flickUntil(s: Scenario, direction: 'down' | 'up', done: (grid: AppEvent) => boolean, what: string): Promise<void> {
    const [from, to] = direction === 'down' ? [LIBRARY.flickFrom, LIBRARY.flickTo] : [LIBRARY.flickTo, LIBRARY.flickFrom];
    for (let i = 0; i < LIBRARY.maxFlicks; i++) {
        const grid = s.events.events.filter((e) => e.ev === 'grid').at(-1);
        if (grid && done(grid)) return;
        const mark = s.events.mark();
        if (!(await inputInLoop(s, () => s.driver.swipe(from.x, from.y, to.x, to.y, 120)))) continue;
        await s.events.waitForEvent('idle', {}, { from: mark }); // the flick's glide has ended
    }
    throw new Error(`${LIBRARY.maxFlicks} flicks ${direction} didn't reach ${what}`);
}
