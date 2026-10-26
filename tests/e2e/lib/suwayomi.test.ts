import { describe, expect, it } from 'vitest';

import { appServerUrl, gatewayFromRoute } from './suwayomi.ts';

const ROUTE = [
    'Iface\tDestination\tGateway \tFlags\tRefCnt\tUse\tMetric\tMask\t\tMTU\tWindow\tIRTT',
    'eth0\t00000000\t01401BAC\t0003\t0\t0\t0\t00000000\t0\t0\t0',
    'eth0\t00401BAC\t00000000\t0001\t0\t0\t0\t00F0FFFF\t0\t0\t0',
].join('\n');

describe('suwayomi lib', () => {
    it('reads the default gateway from /proc/net/route (little-endian hex)', () => {
        expect(gatewayFromRoute(ROUTE)).toBe('172.27.64.1');
        expect(gatewayFromRoute(ROUTE.split('\n').filter((l) => !l.includes('01401BAC')).join('\n'))).toBeNull();
        expect(gatewayFromRoute('')).toBeNull();
    });

    it('appServerUrl: SNX_APP_SERVER_URL wins, the desktop uses the harness URL', () => {
        expect(appServerUrl('switch', { SNX_APP_SERVER_URL: 'http://10.0.0.2:4568/' })).toBe('http://10.0.0.2:4568');
        expect(appServerUrl('desktop', { SNX_SERVER_URL: 'http://172.27.64.1:4568' })).toBe('http://172.27.64.1:4568');
    });
});
