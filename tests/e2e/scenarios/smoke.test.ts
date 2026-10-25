// smoke (TESTING.md scenario 1): the app starts in application mode, shows its first screen, settles (idle), writes no error
// events, matches the golden screenshot, and quits cleanly with no crash. WP-03: the first screen is still the Hello screen;
// WP-08/WP-09 change it to Setup/Library.

import { expect } from 'vitest';

import { scenario } from '../lib/scenario.ts';

// The footer clock (bottom left) shows the time with seconds. On the Switch the same corner also has battery and Wi-Fi.
const CLOCK = { x: 0, y: 650, w: 420, h: 70, why: 'clock, battery, Wi-Fi' };

scenario('smoke', async (s) => {
    await s.launch();
    const screen = await s.events.waitForEvent('screen', { name: 'Hello' });
    const idle = await s.events.waitForEvent('idle', {}, { from: s.events.events.indexOf(screen) });
    expect(idle.t).toBeGreaterThanOrEqual(screen.t);

    await s.expectGolden('hello', { masks: [CLOCK] });
    s.expectNoErrorEvents();
    await s.driver.quit();
});
