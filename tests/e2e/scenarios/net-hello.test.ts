// net-hello (WP-05, moved to the diagnostics screen in WP-08): the diagnostics screen asks the test server for its version
// through the app's real HTTP client (curl), then sends 20 requests at once. All of them must be answered: the 20 wait in the
// queue behind the 6 transfers that run at a time, none fails. The main screen's own server check runs as well.
// Needs the test server (tools/test-server/test-server.sh start); the Switch reaches it over the LAN (lib/suwayomi.ts appServerUrl).
// The screenshot goes to out/net-hello/ to look at (no golden: it shows the server's version, which changes with the server).

import { writeFileSync } from 'node:fs';
import { join } from 'node:path';

import { expect } from 'vitest';

import { scenario } from '../lib/scenario.ts';
import { appServerUrl } from '../lib/suwayomi.ts';

const BURST = 20;

scenario(
    'net-hello',
    async (s) => {
        const url = appServerUrl(s.driver.name);
        await s.launch({ testConfig: { server: { url }, diagnostics: { burst: BURST } } });
        await s.events.waitForEvent('screen', { name: 'Diagnostics' });
        const check = await s.events.waitForEvent('server_check', { where: 'diagnostics' }, { timeoutMs: 30_000 });
        expect(check.ok, JSON.stringify(check)).toBe(true);
        const burst = await s.events.waitForEvent('net_burst', {}, { timeoutMs: 60_000 });
        expect(burst).toMatchObject({ sent: BURST, ok: BURST });
        await s.events.waitForEvent('idle', {}, { from: s.events.events.indexOf(burst) });

        // The main screen's check + the diagnostics check + the burst, all HTTP 200.
        const graphql = s.events.events.filter((e) => e.ev === 'net' && e.url === `${url}/api/graphql`);
        expect(graphql.length).toBeGreaterThanOrEqual(BURST + 2);
        for (const e of graphql) expect(e, JSON.stringify(e)).toMatchObject({ method: 'POST', status: 200, error: '' });
        writeFileSync(join(s.out, `diagnostics.${s.driver.name === 'desktop' ? 'png' : 'jpg'}`), await s.driver.screenshot());

        s.expectNoErrorEvents();
        await s.driver.quit();
    },
    { timeoutMs: 180_000 },
);
