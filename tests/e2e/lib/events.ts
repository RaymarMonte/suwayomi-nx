// EventStream: reads the app's test events (test/events.jsonl, docs/TESTING.md "Test hooks") while the app runs, and lets a
// scenario wait for one: `await events.waitFor('idle', (e) => e.ev === 'idle')`. Tests wait for events, never sleep.
// Where the bytes come from is the driver's business (a file on the PC, sys-autopilot's /files on the Switch): see EventSource.

export interface AppEvent {
    t: number; // ms since the app started
    ev: string; // event name: app_start, screen, idle, ...
    [field: string]: unknown;
}

export interface EventSource {
    /** The events file from byte `offset` on. Empty when nothing new was written or the file doesn't exist yet. */
    read(offset: number): Promise<Buffer>;
    /**
     * The same as read(), but right now (optional). With it, mark() first takes in what the app has written so far, so an
     * event written just before an action, but not read yet, doesn't count as one that came after it (IS-005: a stale
     * "idle" let a scenario tap into a menu that had only just opened). Desktop has it (a local file); the Switch doesn't
     * (its file comes over the network), so there a mark can still be up to one poll behind.
     */
    readNow?(offset: number): Buffer;
    /** null while the app runs. Otherwise a short sentence saying why it's gone ("exited with code 1", ...). */
    appGone(): Promise<string | null>;
}

export interface EventStreamOptions {
    pollMs: number; // how often to read the file
    gonePollMs: number; // how often to ask whether the app is still there (can be slow on the Switch)
}

/** The app ended (crash, kill, quit) while a scenario was waiting for something. */
export class AppGoneError extends Error {}

/** The event didn't come in time. */
export class EventTimeoutError extends Error {}

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

export class EventStream {
    private readonly source: EventSource;
    private readonly options: EventStreamOptions;
    private readonly seen: AppEvent[] = [];
    private readonly lines: string[] = [];
    private offset = 0;
    private partial = Buffer.alloc(0); // bytes after the last newline: the app may be half-way through a line

    constructor(source: EventSource, options: EventStreamOptions) {
        this.source = source;
        this.options = options;
    }

    /** Every event read so far, oldest first. */
    get events(): readonly AppEvent[] {
        return this.seen;
    }

    /** Index of the next event. Take it before an action, then `waitFor(..., { from: mark })` only looks at what came after. */
    mark(): number {
        if (this.source.readNow) this.take(this.source.readNow(this.offset));
        return this.seen.length;
    }

    /** Reads whatever is new once. Throws if a complete line isn't a valid event (a bug in the app's event writer). */
    async poll(): Promise<void> {
        for (;;) {
            const from = this.offset;
            const chunk = await this.source.read(from);
            // A mark() or another poll read on while this one waited: its bytes are in already, so read from there again.
            if (this.offset !== from) continue;
            this.take(chunk);
            return;
        }
    }

    /** Takes in the bytes read from `offset` on: complete lines become events, a half-written one waits for the rest. */
    private take(chunk: Buffer): void {
        if (chunk.length === 0) return;
        this.offset += chunk.length;
        const data = Buffer.concat([this.partial, chunk]);
        const end = data.lastIndexOf(0x0a);
        if (end < 0) {
            this.partial = data;
            return;
        }
        this.partial = data.subarray(end + 1);
        for (const line of data.subarray(0, end).toString('utf8').split('\n')) {
            if (line.trim() === '') continue;
            this.lines.push(line);
            let event: unknown;
            try {
                event = JSON.parse(line);
            } catch {
                throw new Error(`events.jsonl has a line that isn't JSON: ${line.slice(0, 200)}`);
            }
            if (!isAppEvent(event)) throw new Error(`events.jsonl has a line without "t" and "ev": ${line.slice(0, 200)}`);
            this.seen.push(event);
        }
    }

    /**
     * Waits until an event at index >= `from` matches. Already-read events count too, so waiting for app_start after it
     * arrived returns at once. Fails fast (AppGoneError) if the app ends, and with EventTimeoutError after `timeoutMs`.
     * `what` is how the error message names the thing being waited for.
     */
    async waitFor(what: string, match: (e: AppEvent) => boolean, opts: { timeoutMs?: number; from?: number } = {}): Promise<AppEvent> {
        const timeoutMs = opts.timeoutMs ?? 15_000;
        const from = opts.from ?? 0;
        const started = Date.now();
        let lastGoneCheck = 0;
        for (;;) {
            await this.poll();
            const found = this.find(match, from);
            if (found) return found;
            const now = Date.now();
            if (now - lastGoneCheck >= this.options.gonePollMs) {
                lastGoneCheck = now;
                const gone = await this.source.appGone();
                if (gone) {
                    await this.poll(); // the event may have been written just before the end
                    const late = this.find(match, from);
                    if (late) return late;
                    throw new AppGoneError(`the app is gone (${gone}) while waiting for ${what}.\n${this.describeTail()}`);
                }
            }
            if (now - started >= timeoutMs) {
                throw new EventTimeoutError(`no ${what} within ${(timeoutMs / 1000).toFixed(1)} s.\n${this.describeTail()}`);
            }
            await sleep(this.options.pollMs);
        }
    }

    /** Waits for an event by name, optionally with some fields equal to given values: `waitForEvent('screen', { name: 'Hello' })`. */
    waitForEvent(ev: string, fields: Record<string, unknown> = {}, opts: { timeoutMs?: number; from?: number } = {}): Promise<AppEvent> {
        const what = Object.keys(fields).length ? `"${ev}" event with ${JSON.stringify(fields)}` : `"${ev}" event`;
        return this.waitFor(what, (e) => e.ev === ev && Object.entries(fields).every(([k, v]) => e[k] === v), opts);
    }

    /**
     * Waits for the app to settle after an input: the first "idle" after both `from` and `cause` (the event the input
     * brought, if any; a screen's event always gets an "idle" after it). For a tap, take `from` with mark() once the
     * driver's touch() has returned: on desktop the app has read the release by then (IS-005), and borealis acts on the
     * release. A key acts when it goes down, so for a press give the mark from before it and the cause. An "idle" from
     * before these (the app still settling from something earlier, or between a tap and its answer) let the next tap
     * go into a menu or screen that had only just opened (IS-005).
     */
    waitForIdleAfter(from: number, cause?: AppEvent, opts: { timeoutMs?: number } = {}): Promise<AppEvent> {
        const after = cause === undefined ? from : Math.max(from, this.seen.indexOf(cause) + 1);
        return this.waitForEvent('idle', {}, { from: after, timeoutMs: opts.timeoutMs });
    }

    /** Events named "error" (TESTING.md: a scenario normally expects none). */
    errors(): AppEvent[] {
        return this.seen.filter((e) => e.ev === 'error');
    }

    /** The raw lines read so far, as the app wrote them (for the failure artefacts). */
    text(): string {
        return this.lines.map((l) => l + '\n').join('');
    }

    /** The last few lines, for error messages. */
    describeTail(n = 10): string {
        if (this.lines.length === 0) return 'No events were read at all.';
        const tail = this.lines.slice(-n);
        return `Last ${tail.length} of ${this.lines.length} events:\n  ${tail.join('\n  ')}`;
    }

    private find(match: (e: AppEvent) => boolean, from: number): AppEvent | undefined {
        for (let i = from; i < this.seen.length; i++) {
            if (match(this.seen[i])) return this.seen[i];
        }
        return undefined;
    }
}

function isAppEvent(value: unknown): value is AppEvent {
    if (typeof value !== 'object' || value === null) return false;
    const v = value as Record<string, unknown>;
    return typeof v.t === 'number' && typeof v.ev === 'string';
}
