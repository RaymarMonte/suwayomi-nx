// smoke (TESTING.md scenario 1, extended in WP-08): with the test server set up (test config), the app starts in application
// mode, opens the main screen on the Library tab, connects to the server (server_check), settles (idle), matches the golden
// screenshot, opens the Settings tab by touch (About shows the launch mode and profile), writes no error events and quits
// cleanly with no crash. Needs the test server (tools/test-server/test-server.sh start).

import { expect } from 'vitest';

import { scenario } from '../lib/scenario.ts';
import { CLOCK_MASK, tab } from '../lib/screens.ts';
import { appServerUrl } from '../lib/suwayomi.ts';

scenario('smoke', async (s) => {
    const url = appServerUrl(s.driver.name);
    const start = await s.launch({ testConfig: { server: { url } } });
    expect(start.imageBudget, 'app_start.imageBudget').toBeGreaterThan(0);
    await s.events.waitForEvent('screen', { name: 'Library' });
    const check = await s.events.waitForEvent('server_check', { where: 'main' }, { timeoutMs: 30_000 });
    expect(check.ok, JSON.stringify(check)).toBe(true);
    await s.events.waitForEvent('idle', {}, { from: s.events.events.indexOf(check) });
    await s.expectGolden('library', { masks: [CLOCK_MASK] });

    const mark = s.events.mark();
    const settings = tab('Settings');
    await s.driver.touch(settings.x, settings.y);
    const screen = await s.events.waitForEvent('screen', { name: 'Settings' }, { from: mark });
    expect(screen.params).toMatchObject({ mode: 'application', profile: 'full', safeMode: false });
    await s.events.waitForEvent('idle', {}, { from: mark });

    s.expectNoErrorEvents();
    await s.driver.quit();
});
