import { describe, expect, it } from 'vitest';

import { AppGoneError, EventStream, EventTimeoutError, type EventSource } from './events.ts';

/** An events file in memory: tests append to `data` and flip `gone` like the app would. */
class FakeSource implements EventSource {
    data = '';
    gone: string | null = null;
    reads = 0;
    async read(offset: number): Promise<Buffer> {
        this.reads++;
        return Buffer.from(this.data, 'utf8').subarray(offset);
    }
    async appGone(): Promise<string | null> {
        return this.gone;
    }
}

const fast = { pollMs: 5, gonePollMs: 5 };

describe('EventStream', () => {
    it('reads complete lines and keeps a half-written one for later', async () => {
        const src = new FakeSource();
        const stream = new EventStream(src, fast);
        src.data = '{"t":0,"ev":"app_start","mode":"application"}\n{"t":5,"ev":"scr';
        await stream.poll();
        expect(stream.events.map((e) => e.ev)).toEqual(['app_start']);
        src.data += 'een","name":"Hello"}\n';
        await stream.poll();
        expect(stream.events.map((e) => e.ev)).toEqual(['app_start', 'screen']);
        expect(stream.events[1].name).toBe('Hello');
    });

    it('keeps multi-byte UTF-8 intact when a read ends in the middle of a character', async () => {
        const src = new FakeSource();
        const stream = new EventStream(src, fast);
        const full = Buffer.from('{"t":1,"ev":"screen","name":"日本語"}\n', 'utf8');
        let cut = 0;
        src.read = async (offset) => full.subarray(offset, Math.min(full.length, offset + 3 + cut++)); // dribble it out
        await stream.waitForEvent('screen', { name: '日本語' }, { timeoutMs: 2000 });
    });

    it('waitFor returns events that already arrived, and `from` skips older ones', async () => {
        const src = new FakeSource();
        const stream = new EventStream(src, fast);
        src.data = '{"t":0,"ev":"idle"}\n';
        await stream.waitForEvent('idle');
        const mark = stream.mark();
        expect(mark).toBe(1);
        setTimeout(() => (src.data += '{"t":9,"ev":"idle"}\n'), 30);
        const second = await stream.waitForEvent('idle', {}, { from: mark, timeoutMs: 2000 });
        expect(second.t).toBe(9);
    });

    it('matches fields', async () => {
        const src = new FakeSource();
        const stream = new EventStream(src, fast);
        src.data = '{"t":0,"ev":"screen","name":"Setup"}\n{"t":1,"ev":"screen","name":"Library"}\n';
        const e = await stream.waitForEvent('screen', { name: 'Library' });
        expect(e.t).toBe(1);
    });

    it('fails with a clear message, not a hang, when the app goes away', async () => {
        const src = new FakeSource();
        const stream = new EventStream(src, fast);
        src.data = '{"t":0,"ev":"app_start"}\n';
        setTimeout(() => (src.gone = 'killed by SIGKILL'), 20);
        const err = await stream.waitForEvent('idle', {}, { timeoutMs: 10_000 }).catch((e) => e);
        expect(err).toBeInstanceOf(AppGoneError);
        expect(err.message).toContain('killed by SIGKILL');
        expect(err.message).toContain('"idle" event');
        expect(err.message).toContain('app_start'); // the tail of the event log
    });

    it('still finds an event written just before the app ended', async () => {
        const src = new FakeSource();
        const stream = new EventStream(src, { pollMs: 5, gonePollMs: 0 });
        let first = true;
        src.appGone = async () => {
            if (first) {
                first = false;
                src.data = '{"t":3,"ev":"idle"}\n'; // arrives between the last read and the "gone" answer
            }
            return 'exited with code 0';
        };
        expect((await stream.waitForEvent('idle')).t).toBe(3);
    });

    it('times out with the event tail in the message', async () => {
        const src = new FakeSource();
        const stream = new EventStream(src, fast);
        src.data = '{"t":0,"ev":"app_start"}\n';
        const err = await stream.waitForEvent('screen', { name: 'Nope' }, { timeoutMs: 50 }).catch((e) => e);
        expect(err).toBeInstanceOf(EventTimeoutError);
        expect(err.message).toMatch(/no "screen" event with \{"name":"Nope"\} within 0\.1 s/);
        expect(err.message).toContain('Last 1 of 1 events');
    });

    it('rejects lines that are not events', async () => {
        const src = new FakeSource();
        const stream = new EventStream(src, fast);
        src.data = 'garbage\n';
        await expect(stream.poll()).rejects.toThrow(/isn't JSON/);
        const src2 = new FakeSource();
        const stream2 = new EventStream(src2, fast);
        src2.data = '{"ev":"idle"}\n';
        await expect(stream2.poll()).rejects.toThrow(/without "t" and "ev"/);
    });

    it('lists error events and keeps the raw text', async () => {
        const src = new FakeSource();
        const stream = new EventStream(src, fast);
        src.data = '{"t":0,"ev":"app_start"}\n{"t":1,"ev":"error","kind":"Network","msg":"timeout"}\n';
        await stream.poll();
        expect(stream.errors()).toHaveLength(1);
        expect(stream.text()).toBe(src.data);
    });
});
