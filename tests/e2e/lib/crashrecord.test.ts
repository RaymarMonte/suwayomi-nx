import { describe, expect, it } from 'vitest';

import { isCrashFileName, parseCrashRecord } from './crashrecord.ts';

// The same layout core::crashrecord::formatCrashRecord() writes (tests/unit/test_core_safety.cpp checks that side).
const RECORD = [
    'suwayomi-nx crash record 1',
    'type: cpu_exception',
    'what: data abort at 0x0000000000000000 (null pointer?)',
    'thread: main',
    'version: 0.0.1 (test build)',
    'platform: switch',
    'mode: application',
    'build_id: 44401c13a4476f8343f5a4209d657ccd2c803b40',
    'uptime_ms: 1234',
    'module: base 0x0000000008000000 size 0x2a0000',
    'registers:',
    '  pc: BASE + 0x1a2b',
    '  lr: BASE + 0x1b00',
    'stack (return addresses, innermost first):',
    '  #0 BASE + 0x1b00',
    '  #1 0x00000000dead0000',
    'log (last 2 lines, oldest first):',
    '  12 I suwayomi-nx 0.0.1',
    '  40 W drill: null-write',
    'end of crash record',
    '',
].join('\n');

describe('crash records', () => {
    it('reads every field the drills check', () => {
        const r = parseCrashRecord(RECORD);
        expect(r).not.toBeNull();
        expect(r).toMatchObject({
            type: 'cpu_exception',
            what: 'data abort at 0x0000000000000000 (null pointer?)',
            thread: 'main',
            platform: 'switch',
            mode: 'application',
            buildId: '44401c13a4476f8343f5a4209d657ccd2c803b40',
            complete: true,
            stack: ['BASE + 0x1b00', '0x00000000dead0000'],
            log: ['12 I suwayomi-nx 0.0.1', '40 W drill: null-write'],
        });
    });

    it('a record cut short is not complete, other text is not a record', () => {
        expect(parseCrashRecord(RECORD.slice(0, 120))?.complete).toBe(false);
        expect(parseCrashRecord('hello')).toBeNull();
        expect(parseCrashRecord('')).toBeNull();
        expect(parseCrashRecord(RECORD.replace(/\n/g, '\r\n'))?.complete).toBe(true);
    });

    it('knows the file names', () => {
        expect(isCrashFileName('crash-20261025-153007-1.txt')).toBe(true);
        expect(isCrashFileName('crash-pending.tmp')).toBe(false);
        expect(isCrashFileName('app.log')).toBe(false);
    });
});
