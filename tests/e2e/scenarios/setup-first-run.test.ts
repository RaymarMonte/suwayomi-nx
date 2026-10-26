// setup-first-run (TESTING.md scenario 2, WP-08): no config.json -> the setup screen -> the test server's address -> Continue
// -> the main screen's Library tab, connected. config.json is saved; a restart then skips setup and lands on the main screen.
// Desktop: taps the address field and types the address (touch first, D16). The Switch can't type in a test, so the test
// config fills the field in ("showSetup") and the scenario taps Continue. Needs the test server.

import { expect } from 'vitest';

import { scenario } from '../lib/scenario.ts';
import { KEYBOARD_OK, SETUP } from '../lib/screens.ts';
import { appServerUrl } from '../lib/suwayomi.ts';

scenario('setup-first-run', async (s) => {
    const url = appServerUrl(s.driver.name);
    const desktop = s.driver.name === 'desktop';
    await s.launch(desktop ? {} : { testConfig: { server: { url }, showSetup: true } });
    const setup = await s.events.waitForEvent('screen', { name: 'Setup' });
    expect(setup.params).toMatchObject({ firstRun: true, url: desktop ? '' : url });
    let mark = s.events.mark();
    await s.events.waitForEvent('idle', {}, { from: s.events.events.indexOf(setup) });

    if (desktop) {
        mark = s.events.mark();
        await s.driver.touch(SETUP.url.x, SETUP.url.y); // opens the keyboard dialog
        await s.events.waitForEvent('idle', {}, { from: mark });
        await s.driver.typeText(url);
        mark = s.events.mark();
        await s.driver.touch(KEYBOARD_OK.x, KEYBOARD_OK.y);
        // Taps are ignored while the dialog closes: wait until the focus is back on the field and nothing moves.
        await s.events.waitFor('focus on the address field', (e) => e.ev === 'focus' && String(e.view).includes('setup/url'), {
            from: mark,
        });
        await s.events.waitForEvent('idle', {}, { from: mark });
    }
    mark = s.events.mark();
    await s.driver.touch(SETUP.continue.x, SETUP.continue.y); // tests the connection, then saves and closes
    const tested = await s.events.waitForEvent('server_check', { where: 'setup' }, { from: mark, timeoutMs: 30_000 });
    expect(tested.ok, JSON.stringify(tested)).toBe(true);
    await s.events.waitForEvent('screen', { name: 'Library' }, { from: mark });
    const connected = await s.events.waitForEvent('server_check', { where: 'main' }, { from: mark, timeoutMs: 30_000 });
    expect(connected.ok).toBe(true);
    const saved = await s.events.waitForEvent('config_saved', {}, { from: mark, timeoutMs: 15_000 });
    expect(saved).toMatchObject({ server: url, auth: 'none' });
    s.expectNoErrorEvents();
    await s.driver.quit();

    // The restart: no test config, the saved config.json -> straight to the main screen.
    await s.launch({ keepConfig: true });
    await s.events.waitForEvent('screen', { name: 'Library' });
    const again = await s.events.waitForEvent('server_check', { where: 'main' }, { timeoutMs: 30_000 });
    expect(again.ok).toBe(true);
    expect(s.events.events.some((e) => e.ev === 'screen' && e.name === 'Setup'), 'setup must not show again').toBe(false);
    s.expectNoErrorEvents();
    await s.driver.quit();
});
