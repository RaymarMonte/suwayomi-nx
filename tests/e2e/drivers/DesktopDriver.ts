// DesktopDriver (docs/TESTING.md "Drivers"): runs the desktop build of the app on a virtual X screen (Xvfb, 1280×720) and drives
// it with xdotool. `visible: true` uses the WSLg screen instead, so the owner can watch.
//
// Things learned in WP-00/WP-01 that this relies on:
// - The app starts in its build folder (borealis loads ./resources) and keeps its data in ./data/ there (D10).
// - GLFW prefers Wayland and finds WSLg's socket through XDG_RUNTIME_DIR even without WAYLAND_DISPLAY, which would open the
//   window on the owner's screen. So the app gets an empty runtime folder and XDG_SESSION_TYPE=x11, and falls back to X11.
// - borealis reads the keyboard and mouse once a frame, and only while its window has the focus. So every key and click is
//   held for 100 ms, and the window gets the focus first.
// - IS-005: when the app is busy for one long frame, two changes can reach it in that one frame: a press and its release
//   (the press is never seen), or a release and the next press (it looks like one long hold, so no new press). So every
//   key or button change waits until the app has read it, and only then the next one goes out (inputSeen). The proof is
//   borealis's debug log ("Key: … / Code: 263 / Action: 1"; the app runs with -d, its stdout line by line). Mouse
//   buttons aren't logged, so each one is followed by a key nothing reacts to (MARKER_KEY): X hands the app its input in
//   the order it was sent, so once the marker's line is there, the button has been read too.
// - borealis's desktop keyboard map has only: Enter = A, Escape = B, arrow keys = D-pad, middle mouse button = X. The other
//   buttons (Y, L, R, ZL, ZR, PLUS, MINUS, sticks) can't be pressed on desktop yet; press() says so. The app quits on SIGTERM
//   (WP-03), which replaces PLUS here.

import { execFile, spawn, type ChildProcess } from 'node:child_process';
import { closeSync, existsSync, fstatSync, mkdtempSync, openSync, readSync, rmSync } from 'node:fs';
import { mkdir, open, readdir, readFile, rm, writeFile, type FileHandle } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { promisify } from 'node:util';

import { EventStream, type EventSource } from '../lib/events.ts';
import { ALBUM_HEAP_MB, NotSupportedError, startEventFor, type Button, type Driver, type LaunchOptions } from './Driver.ts';
import { CONFIG_FILES } from './ownerConfig.ts';

const run = promisify(execFile);
const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

/** X key names for the buttons desktop borealis understands (glfw_input.cpp). */
const KEYS: Partial<Record<Button, string>> = { A: 'Return', B: 'Escape', UP: 'Up', DOWN: 'Down', LEFT: 'Left', RIGHT: 'Right' };
const MIDDLE_MOUSE_BUTTONS: Partial<Record<Button, true>> = { X: true };
/**
 * A key that nothing in borealis or the app reacts to: no button, no character, no shortcut (glfw_input.cpp,
 * Application's watched keys). Sent right after a mouse button change, its log line shows that the app read the button.
 */
const MARKER_KEY = 'Pause';
/** GLFW's numbers for the keys above: borealis logs each key GLFW hands it as "Key: … / Code: <number> / Action: 1|0". */
const GLFW_CODES: Record<string, number> = { Return: 257, Escape: 256, Right: 262, Left: 263, Down: 264, Up: 265, Pause: 284 };
/** How long an input waits for the app to read it. Longer means the app is stuck or its window lost the focus. */
const SEEN_TIMEOUT_MS = 5_000;

export interface DesktopDriverOptions {
    out: string; // the scenario's output folder: the app's stdout goes there
    visible?: boolean; // WSLg window instead of Xvfb
}

/** Checks that `path` stays inside the data folder: relative, no "..". */
function dataPath(root: string, path: string): string {
    if (path.startsWith('/') || path.split(/[\\/]/).some((s) => s === '..')) throw new Error(`not a path inside the data folder: ${path}`);
    return join(root, path);
}

export class DesktopDriver implements Driver {
    readonly name = 'desktop' as const;
    private readonly out: string;
    private readonly visible: boolean;
    private buildDir = '';
    private display = '';
    private xvfb: ChildProcess | null = null;
    private app: ChildProcess | null = null;
    private appExit: string | null = null; // "exited with code 0", "killed by SIGSEGV", ... once it ended
    private appExited: Promise<void> = Promise.resolve();
    private stdoutFd: number | null = null; // the app's stdout and stderr go here (every launch appends to the same file)
    private stdoutRead: FileHandle | null = null; // the same file, read for borealis's key log lines (inputSeen)
    private runtimeDir = '';
    private windowId = '';
    private stream: EventStream | null = null;

    constructor(options: DesktopDriverOptions) {
        this.out = options.out;
        this.visible = options.visible ?? false;
    }

    private get dataDir(): string {
        return join(this.buildDir, 'data');
    }

    async deploy(buildPath: string): Promise<void> {
        if (!existsSync(join(buildPath, 'suwayomi-nx')) || !existsSync(join(buildPath, 'resources'))) {
            throw new Error(`no desktop build in ${buildPath} (tools/build.sh desktop --test-hooks makes one)`);
        }
        this.buildDir = buildPath;
    }

    async launch(opts: LaunchOptions = {}): Promise<void> {
        if (!this.buildDir) throw new Error('deploy() first: the driver needs the build folder');
        if (this.app && this.appExit === null) throw new Error('the app is already running: quit() first');
        await this.startDisplay();

        await rm(join(this.dataDir, 'test', 'events.jsonl'), { force: true });
        if (opts.testConfig) await this.writeFile('test/config.json', JSON.stringify(opts.testConfig, null, 2));
        else await rm(join(this.dataDir, 'test', 'config.json'), { force: true });
        if (!opts.keepConfig) {
            // config.json and the waiting progress saves (WP-15b): no scenario inherits another's
            for (const name of CONFIG_FILES) await rm(join(this.dataDir, name), { force: true });
        }

        const args = ['-d', '--log-file'];
        if (opts.mode === 'applet') args.push('--simulate-applet-mode');
        const memLimitMb = opts.memLimitMb ?? (opts.mode === 'applet' ? ALBUM_HEAP_MB : undefined); // WP-21: the Album's heap
        if (memLimitMb) args.push(`--mem-limit=${memLimitMb}`);
        args.push(...(opts.args ?? []));

        this.runtimeDir = mkdtempSync(join(tmpdir(), 'snx-runtime-'));
        const env: NodeJS.ProcessEnv = { ...process.env, DISPLAY: this.display, XDG_RUNTIME_DIR: this.runtimeDir, XDG_SESSION_TYPE: 'x11' };
        delete env.WAYLAND_DISPLAY;
        await mkdir(this.out, { recursive: true });
        if (this.stdoutFd === null) this.stdoutFd = openSync(join(this.out, 'stdout.txt'), 'a');
        if (this.stdoutRead === null) this.stdoutRead = await open(join(this.out, 'stdout.txt'), 'r');
        // stdbuf -oL: stdout line by line. borealis flushes its log only on Windows, so into a file it would come in 4 KiB
        // pieces, and inputSeen needs each key's line at once. stdbuf runs the app in its own place (same process id).
        const app = spawn('stdbuf', ['-oL', './suwayomi-nx', ...args], { cwd: this.buildDir, env, stdio: ['ignore', this.stdoutFd, this.stdoutFd] });
        this.app = app;
        this.appExit = null;
        this.appExited = new Promise((resolve) => {
            app.on('exit', (code, signal) => {
                this.appExit = signal ? `killed by ${signal}` : `exited with code ${code}`;
                resolve();
            });
            app.on('error', (e) => {
                this.appExit = `could not start: ${e.message}`;
                resolve();
            });
        });

        const source: EventSource = {
            read: (offset) => this.readFile('test/events.jsonl', offset),
            readNow: (offset) => this.readFileNow('test/events.jsonl', offset),
            appGone: async () => this.appExit,
        };
        this.stream = new EventStream(source, { pollMs: 100, gonePollMs: 100 });
        await this.stream.waitForEvent(startEventFor(opts), {}, { timeoutMs: 20_000 });

        // The window: found by the app's process id, then given the focus (borealis ignores input without it).
        // A crash drill can end the app before its window turns up: then there's no window to find, and launch() is done
        // anyway (the scenario sees the end with waitForExit(); any input would fail with "the app is gone").
        const search = this.xdotool(['search', '--sync', '--onlyvisible', '--pid', String(app.pid)], 20_000);
        search.catch(() => undefined); // if the app wins the race below, nobody waits for this any more
        const found = await Promise.race([search, this.appExited.then(() => null)]);
        if (found === null) return;
        this.windowId = found.trim().split('\n')[0];
        if (!this.windowId) throw new Error('the app has no window');
        await this.xdotool(['windowfocus', '--sync', this.windowId], 5_000);
    }

    async quit(): Promise<void> {
        if (!this.app) throw new Error('the app was never started');
        if (this.appExit === null) {
            this.app.kill('SIGTERM');
            const ended = await Promise.race([this.appExited.then(() => true), sleep(10_000).then(() => false)]);
            if (!ended) {
                this.app.kill('SIGKILL');
                await this.appExited;
                throw new Error('the app did not quit within 10 s of SIGTERM, so it was killed');
            }
        }
        if (this.appExit !== 'exited with code 0') throw new Error(`the app did not end cleanly: it ${this.appExit}`);
    }

    /** Types into borealis's text dialog (GLFW reads the characters), one key every 30 ms so no frame misses one. */
    async typeText(text: string): Promise<void> {
        await this.xdotool(['type', '--delay', '30', '--', text], 5_000 + text.length * 100);
    }

    async press(btn: Button, ms = 100): Promise<void> {
        const key = KEYS[btn];
        if (key) {
            await this.inputSeen(['keydown', key], GLFW_CODES[key], [1]);
            await sleep(ms);
            await this.inputSeen(['keyup', key], GLFW_CODES[key], [0]);
        } else if (MIDDLE_MOUSE_BUTTONS[btn]) {
            await this.buttonSeen(['mousedown', '2']);
            await sleep(ms);
            await this.buttonSeen(['mouseup', '2']);
        } else {
            throw new NotSupportedError(`button ${btn} can't be pressed on desktop (borealis maps only A, B, X and the D-pad there)`);
        }
    }

    async sequence(btns: Button[]): Promise<void> {
        for (const b of btns) {
            await this.press(b);
            await sleep(150);
        }
    }

    async stick(): Promise<void> {
        throw new NotSupportedError('sticks are not available on desktop (no virtual gamepad yet)');
    }

    async touch(x: number, y: number, ms = 100): Promise<void> {
        await this.buttonSeen(['mousemove', '--window', this.windowId, String(Math.round(x)), String(Math.round(y)), 'mousedown', '1']);
        await sleep(ms);
        await this.buttonSeen(['mouseup', '1']);
    }

    async swipe(x1: number, y1: number, x2: number, y2: number, ms: number, steps?: number): Promise<void> {
        const n = Math.max(1, steps ?? Math.round(ms / 16));
        await this.buttonSeen(['mousemove', '--window', this.windowId, String(Math.round(x1)), String(Math.round(y1)), 'mousedown', '1']);
        for (let i = 1; i <= n; i++) {
            await sleep(ms / n);
            const x = Math.round(x1 + ((x2 - x1) * i) / n);
            const y = Math.round(y1 + ((y2 - y1) * i) / n);
            await this.xdotool(['mousemove', '--window', this.windowId, String(x), String(y)]);
        }
        await this.buttonSeen(['mouseup', '1']);
    }

    async screenshot(): Promise<Buffer> {
        // Xvfb's screen is exactly the window (1280×720 at 0,0). On WSLg the window sits somewhere on a bigger screen.
        const target = this.visible ? this.windowId : 'root';
        const { stdout } = await run('import', ['-window', target, 'png:-'], {
            env: { ...process.env, DISPLAY: this.display },
            encoding: 'buffer',
            maxBuffer: 32 * 1024 * 1024,
            timeout: 15_000,
        });
        return stdout;
    }

    async requestExit(): Promise<void> {
        if (!this.app || this.appExit !== null) throw new Error('the app is not running');
        this.app.kill('SIGTERM');
    }

    async waitForExit(timeoutMs: number): Promise<string> {
        if (!this.app) throw new Error('the app was never started');
        const ended = await Promise.race([this.appExited.then(() => true), sleep(timeoutMs).then(() => false)]);
        if (!ended) throw new Error(`the app was still running ${timeoutMs} ms later`);
        return this.appExit ?? 'ended';
    }

    async listFiles(dir: string): Promise<string[]> {
        try {
            return (await readdir(dataPath(this.dataDir, dir))).sort();
        } catch (e) {
            if ((e as NodeJS.ErrnoException).code === 'ENOENT') return [];
            throw e;
        }
    }

    async readFile(path: string, offset = 0): Promise<Buffer> {
        let handle;
        try {
            handle = await open(dataPath(this.dataDir, path), 'r');
        } catch (e) {
            if ((e as NodeJS.ErrnoException).code === 'ENOENT') return Buffer.alloc(0);
            throw e;
        }
        try {
            const { size } = await handle.stat();
            const start = offset < 0 ? Math.max(0, size + offset) : Math.min(offset, size);
            const buffer = Buffer.alloc(size - start);
            await handle.read(buffer, 0, buffer.length, start);
            return buffer;
        } finally {
            await handle.close();
        }
    }

    /** readFile(), but at once (the events' mark(), IS-005). The app writes each event straight to the file. */
    private readFileNow(path: string, offset: number): Buffer {
        let fd;
        try {
            fd = openSync(dataPath(this.dataDir, path), 'r');
        } catch (e) {
            if ((e as NodeJS.ErrnoException).code === 'ENOENT') return Buffer.alloc(0);
            throw e;
        }
        try {
            const { size } = fstatSync(fd);
            const start = Math.min(offset, size);
            const buffer = Buffer.alloc(size - start);
            const got = readSync(fd, buffer, 0, buffer.length, start);
            return buffer.subarray(0, got);
        } finally {
            closeSync(fd);
        }
    }

    async writeFile(path: string, data: Buffer | string): Promise<void> {
        const full = dataPath(this.dataDir, path);
        await mkdir(dirname(full), { recursive: true });
        await writeFile(full, data);
    }

    async removeFolder(dir: string): Promise<void> {
        if (!dir || dir === '.') throw new Error('removeFolder: name a folder inside the data folder');
        await rm(dataPath(this.dataDir, dir), { recursive: true, force: true });
    }

    events(): EventStream {
        if (!this.stream) throw new Error('launch() first: there are no events before the app starts');
        return this.stream;
    }

    /** Desktop only, for the harness's own tests: kills the app the hard way, like a crash would end it. */
    killApp(signal: NodeJS.Signals = 'SIGKILL'): void {
        if (!this.app || this.appExit !== null) throw new Error('the app is not running');
        this.app.kill(signal);
    }

    async dispose(): Promise<void> {
        if (this.app && this.appExit === null) {
            this.app.kill('SIGTERM');
            const ended = await Promise.race([this.appExited.then(() => true), sleep(5_000).then(() => false)]);
            if (!ended) {
                this.app.kill('SIGKILL');
                await this.appExited;
            }
        }
        if (this.stdoutFd !== null) {
            closeSync(this.stdoutFd);
            this.stdoutFd = null;
        }
        if (this.stdoutRead !== null) {
            await this.stdoutRead.close();
            this.stdoutRead = null;
        }
        if (this.runtimeDir) rmSync(this.runtimeDir, { recursive: true, force: true });
        this.runtimeDir = '';
        if (this.xvfb) {
            this.xvfb.kill('SIGTERM');
            this.xvfb = null;
        }
    }

    /** Starts Xvfb on the first free display from :90 (once per driver). Visible mode uses WSLg's display instead. */
    private async startDisplay(): Promise<void> {
        if (this.display) return;
        if (this.visible) {
            this.display = process.env.DISPLAY || ':0';
            return;
        }
        for (let n = 90; n < 100; n++) {
            if (existsSync(`/tmp/.X${n}-lock`)) continue; // another X server has it
            const xvfb = spawn('Xvfb', [`:${n}`, '-screen', '0', '1280x720x24', '-nolisten', 'tcp'], { stdio: 'ignore' });
            let exited = false;
            xvfb.on('exit', () => (exited = true));
            // Ready = it answers. (Under WSLg /tmp/.X11-unix is read-only, so Xvfb only has its abstract socket: no file to wait for.)
            for (let i = 0; i < 50 && !exited; i++) {
                try {
                    await run('xdotool', ['getdisplaygeometry'], { env: { ...process.env, DISPLAY: `:${n}` }, timeout: 2_000 });
                    this.xvfb = xvfb;
                    this.display = `:${n}`;
                    return;
                } catch {
                    await sleep(100);
                }
            }
            xvfb.kill('SIGKILL');
        }
        throw new Error('could not start Xvfb on any display from :90 to :99');
    }

    /**
     * Sends xdotool `args` (one key or button change) and waits until the app has read it: until borealis has logged GLFW
     * key `code` with one of `actions` (1 = down, 0 = up) after this call began. Only then may the next change go out (IS-005).
     */
    private async inputSeen(args: string[], code: number, actions: (0 | 1)[]): Promise<void> {
        const from = await this.stdoutSize();
        await this.xdotool(args);
        const lines = actions.map((action) => `/ Code: ${code} / Action: ${action}`);
        const started = Date.now();
        for (;;) {
            const text = await this.stdoutFrom(from);
            if (lines.some((line) => text.includes(line))) return;
            if (this.appExit !== null) return; // the input ended the app (or it crashed): the scenario's next wait says so
            if (Date.now() - started >= SEEN_TIMEOUT_MS) {
                console.warn(`[desktop] the app did not read "xdotool ${args.join(' ')}" within ${SEEN_TIMEOUT_MS / 1000} s ` +
                    '(stuck, or its window lost the focus): going on');
                return;
            }
            await sleep(1);
        }
    }

    /**
     * A mouse button change (`args`), followed by the marker key (down and up at once) in the same xdotool call; waits
     * until the app has read it. Either of the marker's lines will do: GLFW now and then drops the marker's press (7 in
     * ~1,900, IS-005) and logs only its release, and both went out after the button.
     */
    private buttonSeen(args: string[]): Promise<void> {
        return this.inputSeen([...args, 'key', '--delay', '0', MARKER_KEY], GLFW_CODES[MARKER_KEY], [1, 0]);
    }

    /** How long the app's stdout file is now (bytes). */
    private async stdoutSize(): Promise<number> {
        if (!this.stdoutRead) throw new Error('launch() first: there is no app output yet');
        return (await this.stdoutRead.stat()).size;
    }

    /** The app's stdout from byte `from` to its end. */
    private async stdoutFrom(from: number): Promise<string> {
        if (!this.stdoutRead) throw new Error('launch() first: there is no app output yet');
        const size = (await this.stdoutRead.stat()).size;
        if (size <= from) return '';
        const buffer = Buffer.alloc(size - from);
        const { bytesRead } = await this.stdoutRead.read(buffer, 0, buffer.length, from);
        return buffer.toString('utf8', 0, bytesRead);
    }

    private async xdotool(args: string[], timeoutMs = 5_000): Promise<string> {
        try {
            const { stdout } = await run('xdotool', args, { env: { ...process.env, DISPLAY: this.display }, timeout: timeoutMs });
            return stdout;
        } catch (e) {
            if (this.appExit) throw new Error(`xdotool ${args[0]} failed because the app is gone (it ${this.appExit})`);
            throw new Error(`xdotool ${args.join(' ')} failed: ${e instanceof Error ? e.message : String(e)}`);
        }
    }
}
