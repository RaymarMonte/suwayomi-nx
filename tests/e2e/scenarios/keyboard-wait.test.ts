// keyboard-wait (WP-12 regression, Switch only): the system keyboard blocks the app's main loop for as long as it is open
// (swkbdShow), and the crash watchdog used to take that for a hang: 10 s into typing a server address the app ended with a
// watchdog crash record (the owner's first real use, 2026-10-30). Now a text field tells the watchdog it is waiting.
// - The setup screen opens with the test server's address filled in ("showSetup"); a tap on the address opens the keyboard.
// - The keyboard stays open for 15 s (longer than the watchdog's 10 s), then + confirms the address as it is.
// - Continue connects and the library shows: the app survived. It quits cleanly, and logs/ has no crash record.
// The desktop's keyboard is a borealis dialog that doesn't block, so this runs on the console only. Needs the test server.

import { expect } from 'vitest';

import { waitForLibrary } from '../lib/reader.ts';
import { scenario } from '../lib/scenario.ts';
import { SETUP } from '../lib/screens.ts';
import { appServerUrl } from '../lib/suwayomi.ts';

const KEYBOARD_OPEN_MS = 15_000;

scenario(
    'keyboard-wait',
    async (s) => {
        const url = appServerUrl(s.driver.name);
        await s.launch({ testConfig: { server: { url }, showSetup: true } });
        await s.events.waitForEvent('screen', { name: 'Setup' }, { timeoutMs: 30_000 });
        await s.events.waitForEvent('idle');

        await s.driver.touch(SETUP.url.x, SETUP.url.y); // opens the system keyboard
        // The time passing is what this tests: the main loop stands still while the keyboard is open.
        await new Promise((r) => setTimeout(r, KEYBOARD_OPEN_MS));
        const mark = s.events.mark();
        await s.driver.press('PLUS'); // the keyboard's OK
        // The main loop runs again once the keyboard has closed: a "busy" event (the 15 s it stood still), then "idle".
        const busy = await s.events.waitForEvent('busy', {}, { from: mark });
        expect(Number(busy.ms), 'the main loop stood still while the keyboard was open (ms)').toBeGreaterThan(KEYBOARD_OPEN_MS - 2_000);
        await s.events.waitForEvent('idle', {}, { from: s.events.events.indexOf(busy) });

        await s.driver.touch(SETUP.continue.x, SETUP.continue.y); // tests the connection, then saves and opens the library
        await waitForLibrary(s);
        // Records are crash-<time>.txt; crash-pending.tmp is the handler's file kept ready during every run.
        const crashes = (await s.driver.listFiles('logs')).filter((name) => /^crash-.*\.txt$/.test(name));
        expect(crashes, 'crash records in logs/').toEqual([]);
        s.expectNoErrorEvents();
        await s.driver.quit();
    },
    { drivers: ['switch'] },
);
