// DesktopDriver (docs/TESTING.md "Drivers"): runs the desktop build of the app on a virtual X screen (Xvfb, 1280×720) and drives
// it with xdotool. `visible: true` uses the WSLg screen instead, so the owner can watch.
//
// Things learned in WP-00/WP-01 that this relies on:
// - The app starts in its build folder (borealis loads ./resources) and keeps its data in ./data/ there (D10).
// - GLFW prefers Wayland and finds WSLg's socket through XDG_RUNTIME_DIR even without WAYLAND_DISPLAY, which would open the
//   window on the owner's screen. So the app gets an empty runtime folder and XDG_SESSION_TYPE=x11, and falls back to X11.
// - borealis reads the keyboard and mouse once a frame, and only while its window has the focus. So every key and click is
//   held for 100 ms, and the window gets the focus first.
// - borealis's desktop keyboard map has only: Enter = A, Escape = B, arrow keys = D-pad, middle mouse button = X. The other
//   buttons (Y, L, R, ZL, ZR, PLUS, MINUS, sticks) can't be pressed on desktop yet; press() says so. The app quits on SIGTERM
//   (WP-03), which replaces PLUS here.

import { execFile, spawn, type ChildProcess } from 'node:child_process';
import { closeSync, existsSync, mkdtempSync, openSync, rmSync } from 'node:fs';
import { mkdir, open, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { promisify } from 'node:util';

import { EventStream, type EventSource } from '../lib/events.ts';
import { NotSupportedError, type Button, type Driver, type LaunchOptions } from './Driver.ts';

const run = promisify(execFile);
const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

/** X key names for the buttons desktop borealis understands (glfw_input.cpp). */
const KEYS: Partial<Record<Button, string>> = { A: 'Return', B: 'Escape', UP: 'Up', DOWN: 'Down', LEFT: 'Left', RIGHT: 'Right' };
const MIDDLE_MOUSE_BUTTONS: Partial<Record<Button, true>> = { X: true };

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
    private stdoutFd: number | null = null;
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

        const args = ['-d', '--log-file'];
        if (opts.mode === 'applet') args.push('--simulate-applet-mode');
        if (opts.memLimitMb) args.push(`--mem-limit=${opts.memLimitMb}`);
        args.push(...(opts.args ?? []));

        this.runtimeDir = mkdtempSync(join(tmpdir(), 'snx-runtime-'));
        const env: NodeJS.ProcessEnv = { ...process.env, DISPLAY: this.display, XDG_RUNTIME_DIR: this.runtimeDir, XDG_SESSION_TYPE: 'x11' };
        delete env.WAYLAND_DISPLAY;
        await mkdir(this.out, { recursive: true });
        this.stdoutFd = openSync(join(this.out, 'stdout.txt'), 'a');
        const app = spawn('./suwayomi-nx', args, { cwd: this.buildDir, env, stdio: ['ignore', this.stdoutFd, this.stdoutFd] });
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
            appGone: async () => this.appExit,
        };
        this.stream = new EventStream(source, { pollMs: 100, gonePollMs: 100 });
        await this.stream.waitForEvent('app_start', {}, { timeoutMs: 20_000 });

        // The window: found by the app's process id, then given the focus (borealis ignores input without it).
        const found = await this.xdotool(['search', '--sync', '--onlyvisible', '--pid', String(app.pid)], 20_000);
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

    async press(btn: Button, ms = 100): Promise<void> {
        const key = KEYS[btn];
        if (key) {
            await this.xdotool(['keydown', key]);
            await sleep(ms);
            await this.xdotool(['keyup', key]);
        } else if (MIDDLE_MOUSE_BUTTONS[btn]) {
            await this.xdotool(['mousedown', '2']);
            await sleep(ms);
            await this.xdotool(['mouseup', '2']);
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
        await this.xdotool(['mousemove', '--window', this.windowId, String(Math.round(x)), String(Math.round(y)), 'mousedown', '1']);
        await sleep(ms);
        await this.xdotool(['mouseup', '1']);
    }

    async swipe(x1: number, y1: number, x2: number, y2: number, ms: number, steps?: number): Promise<void> {
        const n = Math.max(1, steps ?? Math.round(ms / 16));
        await this.xdotool(['mousemove', '--window', this.windowId, String(Math.round(x1)), String(Math.round(y1)), 'mousedown', '1']);
        for (let i = 1; i <= n; i++) {
            await sleep(ms / n);
            const x = Math.round(x1 + ((x2 - x1) * i) / n);
            const y = Math.round(y1 + ((y2 - y1) * i) / n);
            await this.xdotool(['mousemove', '--window', this.windowId, String(x), String(y)]);
        }
        await this.xdotool(['mouseup', '1']);
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

    async writeFile(path: string, data: Buffer | string): Promise<void> {
        const full = dataPath(this.dataDir, path);
        await mkdir(dirname(full), { recursive: true });
        await writeFile(full, data);
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
