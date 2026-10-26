// net-hello (WP-05): the Hello screen asks the test server for its version through the app's real HTTP client (curl), then sends
// 20 requests at once. All 21 must be answered: the 20 wait in the queue behind the 6 transfers that run at a time, none fails.
// Needs the test server (tools/test-server/test-server.sh start); the Switch reaches it over the LAN (lib/suwayomi.ts appServerUrl).
// The screenshot goes to out/net-hello/ to look at (no golden: it shows the server's version, which changes with the server).

import { writeFileSync } from 'node:fs';
import { join } from 'node:path';

import { expect } from 'vitest';

import { scenario } from '../lib/scenario.ts';
import { appServerUrl } from '../lib/suwayomi.ts';

const REQUESTS = 21; // the version check, then 20 at once

scenario(
    'net-hello',
    async (s) => {
        const url = appServerUrl(s.driver.name);
        await s.launch({ testConfig: { server: { url } } });
        await s.events.waitForEvent('screen', { name: 'Hello' });

        let from = 0;
        for (let i = 0; i < REQUESTS; i++) {
            const done = await s.events.waitFor('net', (e) => e.ev === 'net' && e.url === `${url}/api/graphql`, {
                from,
                timeoutMs: 30_000,
            });
            expect(done, `request ${i + 1} of ${REQUESTS}`).toMatchObject({ method: 'POST', status: 200, error: '' });
            from = s.events.events.indexOf(done) + 1;
        }
        await s.events.waitForEvent('idle', {}, { from });
        writeFileSync(join(s.out, `hello-server.${s.driver.name === 'desktop' ? 'png' : 'jpg'}`), await s.driver.screenshot());

        s.expectNoErrorEvents();
        await s.driver.quit();
    },
    { timeoutMs: 180_000 },
);
