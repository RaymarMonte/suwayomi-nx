// SwitchDriver (docs/TESTING.md "Drivers", D24): drives the real console through sys-autopilot-nx's REST API (autopilot.ts,
// with the S10 allowlist) and starts the app with hbmenu's network loader (nxlink). The calibrated steps and screen regions come
// from tests/e2e/device/profile.json; the reference crops from tests/e2e/device/local/ (git-ignored, console-specific).
//
// Safety rules (SAFETY.md, TESTING.md):
// - Every press that could start something waits for a screen check first. On the wrong screen an A starts a game.
// - A new file in atmosphere/fatal_errors/ or fatal_reports/ is a hard stop: the driver writes tests/e2e/out/STOP-console-fatal.txt and refuses
//   to run again until the owner has looked at the console and deleted that file. It never retries.
// - The console not answering at all gets a message asking the owner to look at it (it may show the fatal "press POWER" screen).

import { execFile } from 'node:child_process';
import { createHash } from 'node:crypto';
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { promisify } from 'node:util';

import { EventStream, type EventSource } from '../lib/events.ts';
import { APP_DIR, Autopilot, ConsoleUnreachableError, CRASH_REPORTS_DIR, FATAL_DIRS, UNREACHABLE_HELP } from './autopilot.ts';
import { NotSupportedError, type Button, type Driver, type LaunchOptions } from './Driver.ts';
import { parseRegion, regionRmse, type Region } from './screencheck.ts';

const run = promisify(execFile);
const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

const DEVICE_DIR = fileURLToPath(new URL('../device/', import.meta.url));
export const STOP_FILE = fileURLToPath(new URL('../out/STOP-console-fatal.txt', import.meta.url));

const NRO_SD = `${APP_DIR}/suwayomi-nx.nro`;
const NXLINK_TARGET = 'suwayomi-nx/suwayomi-nx.nro'; // relative to sdmc:/switch/ (hbmenu adds that), so always inside our folder
const APP_ARGS = ['-d', '--log-file'];

/**
 * Applet mode (from the Album) waits for WP-03b's crash handler and WP-08's guard: there, any crash of ours is a system fatal
 * (D21, SAFETY.md). WP-08 sets this to true for the applet-guard scenario.
 */
const APPLET_MODE_READY = false;

/** A new fatal error file appeared on the console. Nothing may carry on until the owner has checked it. */
export class FatalConsoleError extends Error {}

interface Profile {
    screenCheck: { threshold: number };
    references: Record<string, { region: string; screen: string } | string>;
}

interface Reference {
    region: Region;
    png: Buffer;
}

export interface SwitchDriverOptions {
    out: string;
    autopilot?: Autopilot;
}

export class SwitchDriver implements Driver {
    readonly name = 'switch' as const;
    private readonly out: string;
    private readonly autopilot: Autopilot;
    private readonly profile: Profile;
    private readonly refs = new Map<string, Reference>();
    private nroPath = '';
    private nroHash = '';
    private stream: EventStream | null = null;
    private crashBefore: { crash: string[]; fatal: string[] } | null = null; // set at launch, cleared once checked
    private launched = false; // the app was started and hasn't quit yet
    private touched = false; // the console was moved away from the HOME menu by this driver

    constructor(options: SwitchDriverOptions) {
        if (existsSync(STOP_FILE)) {
            throw new FatalConsoleError(`${readFileSync(STOP_FILE, 'utf8').trim()}\nOnce the console is checked, delete ${STOP_FILE}.`);
        }
        this.out = options.out;
        this.autopilot = options.autopilot ?? new Autopilot();
        this.profile = JSON.parse(readFileSync(join(DEVICE_DIR, 'profile.json'), 'utf8')) as Profile;
    }

    async deploy(nroPath: string): Promise<void> {
        if (!nroPath.endsWith('.nro') || !existsSync(nroPath)) throw new Error(`no .nro at ${nroPath} (tools/build.sh switch --test-hooks makes one)`);
        this.nroPath = nroPath;
        this.nroHash = createHash('sha256').update(readFileSync(nroPath)).digest('hex');
        await this.checkReachable();
        if ((await this.autopilot.sha256(NRO_SD)) === this.nroHash) return;
        await this.putVerified(NRO_SD, readFileSync(nroPath));
    }

    async launch(opts: LaunchOptions = {}): Promise<void> {
        if (!this.nroPath) throw new Error('deploy() first: the driver needs the .nro');
        if (opts.memLimitMb) throw new NotSupportedError('--mem-limit is desktop only (TESTING.md: low memory is tested on the PC)');
        const applet = opts.mode === 'applet';
        if (applet && !APPLET_MODE_READY) {
            throw new NotSupportedError('applet mode on the Switch waits for the crash handler (WP-03b) and the guard (WP-08), D21');
        }
        if (this.launched) throw new Error('the app is already running: quit() first');
        await this.checkReachable();
        this.crashBefore = await this.crashLists();

        this.touched = true;
        if (applet) await this.toHbmenuAppletMode();
        else await this.toHbmenuApplicationMode();

        if (opts.testConfig) await this.writeFile('test/config.json', JSON.stringify(opts.testConfig, null, 2));
        else await this.autopilot.deleteFile(`${APP_DIR}/test/config.json`);
        await this.autopilot.deleteFile(`${APP_DIR}/test/events.jsonl`);

        await this.startWithNxlink([...APP_ARGS, ...(opts.args ?? [])]);
        this.launched = true;

        const source: EventSource = {
            read: async (offset) => (await this.autopilot.getFile(`${APP_DIR}/test/events.jsonl`, offset)) ?? Buffer.alloc(0),
            appGone: () => this.appGone(),
        };
        this.stream = new EventStream(source, { pollMs: 500, gonePollMs: 3_000 });
        await this.stream.waitForEvent('app_start', {}, { timeoutMs: 20_000 });

        // hbmenu wrote the .nro it received to the card in place (not atomic): check it arrived whole.
        const onCard = await this.autopilot.sha256(NRO_SD);
        if (onCard !== this.nroHash) throw new Error(`the .nro on the card doesn't match the build after nxlink (${onCard} vs ${this.nroHash})`);
    }

    async quit(): Promise<void> {
        if (!this.launched) throw new Error('the app is not running');
        await this.autopilot.tap(['PLUS']);
        const back = await this.waitForScreen('hbmenu-header', 10_000);
        this.launched = false;
        if (!back) throw new Error('PLUS did not bring back hbmenu within 10 s');
        await this.checkCrashes();
    }

    async press(btn: Button, ms?: number): Promise<void> {
        if (ms) {
            await this.autopilot.hold([btn]);
            await sleep(ms);
            await this.autopilot.release([btn]);
        } else {
            await this.autopilot.tap([btn]);
        }
    }

    async sequence(btns: Button[]): Promise<void> {
        for (const b of btns) {
            await this.autopilot.tap([b]);
            await sleep(300);
        }
    }

    async stick(side: 'left' | 'right', x: number, y: number, ms: number): Promise<void> {
        await this.autopilot.stick(side, x, y, ms);
    }

    async touch(x: number, y: number, ms?: number): Promise<void> {
        await this.autopilot.touch(x, y, ms);
    }

    async swipe(x1: number, y1: number, x2: number, y2: number, ms: number, steps?: number): Promise<void> {
        await this.autopilot.swipe(x1, y1, x2, y2, ms, steps);
    }

    screenshot(): Promise<Buffer> {
        return this.autopilot.screenshot();
    }

    async readFile(path: string, offset = 0): Promise<Buffer> {
        return (await this.autopilot.getFile(this.sdPath(path), offset)) ?? Buffer.alloc(0);
    }

    async writeFile(path: string, data: Buffer | string): Promise<void> {
        await this.putVerified(this.sdPath(path), Buffer.isBuffer(data) ? data : Buffer.from(data, 'utf8'));
    }

    events(): EventStream {
        if (!this.stream) throw new Error('launch() first: there are no events before the app starts');
        return this.stream;
    }

    /** Back to the start state: nothing running, HOME menu on the first tile. Then the crash check, if quit() didn't do it. */
    async dispose(): Promise<void> {
        if (!this.touched) return;
        try {
            await this.recover();
            this.touched = false;
            this.launched = false;
        } finally {
            if (this.crashBefore) await this.checkCrashes();
        }
    }

    /** HOME, then close whatever runs (hbmenu or the app), until the HOME menu shows the first tile with nothing running. */
    async recover(): Promise<void> {
        await this.autopilot.clearInput();
        await this.autopilot.tap(['HOME']);
        await sleep(1_500);
        if (await this.screenIs('home-first-tile')) return;
        await this.closeSoftware();
        if (!(await this.waitForScreen('home-first-tile', 6_000))) {
            throw new Error('could not get back to the HOME menu with the first tile selected. Please check the console.');
        }
    }

    // --- launching ---------------------------------------------------------------------------------------------------------

    /** HOME → first tile (the app-mode game) → hold R + A → A on the user picker → hbmenu (profile.json toHbmenuApplicationMode). */
    private async toHbmenuApplicationMode(): Promise<void> {
        // Between scenarios the console usually still sits in hbmenu in application mode: then there's nothing to do.
        const shot = await this.autopilot.screenshot();
        if ((await this.screenIs('hbmenu-header', shot)) && !(await this.screenIs('hbmenu-applet-badge', shot))) return;

        await this.autopilot.tap(['HOME']);
        await sleep(1_500);
        if (!(await this.screenIs('home-first-tile'))) {
            await this.closeSoftware();
            if (!(await this.waitForScreen('home-first-tile', 5_000))) {
                throw new Error('the HOME menu is not on the first tile (the app-mode game, Spelunky 2). Please check the console.');
            }
        }
        await this.autopilot.hold(['R']);
        try {
            await sleep(300);
            await this.autopilot.tap(['A']);
            if (!(await this.waitForScreen('user-picker', 8_000))) throw new Error('the user picker did not appear after A on the first tile');
            await this.autopilot.tap(['A']);
            if (!(await this.waitForScreen('hbmenu-header', 15_000))) throw new Error('hbmenu did not appear after the user picker');
        } finally {
            await this.autopilot.release(['R']);
        }
        if (await this.screenIs('hbmenu-applet-badge')) throw new Error('hbmenu says "Applet Mode" although it was started with R held');
    }

    /** HOME → Album → hbmenu with the "Applet Mode" badge (profile.json toHbmenuAppletMode). Only for the applet-guard scenario. */
    private async toHbmenuAppletMode(): Promise<void> {
        await this.recover();
        await this.autopilot.touch(550, 544); // the Album icon: the first tap selects it
        await this.autopilot.tap(['A']);
        if (!(await this.waitForScreen('hbmenu-header', 10_000))) throw new Error('hbmenu did not appear from the Album');
        if (!(await this.screenIs('hbmenu-applet-badge'))) throw new Error('hbmenu from the Album shows no "Applet Mode" badge');
    }

    /** hbmenu's network loader: Y, then nxlink sends the .nro, hbmenu writes it to our folder and starts it (D24). */
    private async startWithNxlink(args: string[]): Promise<void> {
        if (!(await this.screenIs('hbmenu-header'))) throw new Error('not in hbmenu, so Y would not open the network loader');
        await this.autopilot.tap(['Y']);
        await sleep(1_500);
        const nxlink = process.env.DEVKITPRO ? join(process.env.DEVKITPRO, 'tools/bin/nxlink') : 'nxlink';
        const log: string[] = [];
        for (let attempt = 1; attempt <= 5; attempt++) {
            try {
                const { stdout, stderr } = await run(nxlink, ['-a', this.autopilot.host, '-p', NXLINK_TARGET, '--args', args.join(' '), this.nroPath], {
                    timeout: 60_000,
                });
                log.push(`attempt ${attempt}: ok\n${stdout}${stderr}`);
                writeFileSync(join(this.out, 'nxlink.txt'), log.join('\n'));
                return;
            } catch (e) {
                log.push(`attempt ${attempt}: ${e instanceof Error ? e.message : String(e)}`);
                await sleep(1_000);
            }
        }
        writeFileSync(join(this.out, 'nxlink.txt'), log.join('\n'));
        await this.autopilot.tap(['B']); // leave the network loader
        throw new Error(`nxlink failed 5 times, see ${join(this.out, 'nxlink.txt')}`);
    }

    /** profile.json closeSoftware: X on the HOME menu, then A only once the "Close the software?" dialog is really there. */
    private async closeSoftware(): Promise<void> {
        await this.autopilot.tap(['X']);
        if (!(await this.waitForScreen('close-dialog', 4_000))) throw new Error('no "Close the software?" dialog after X on the HOME menu');
        await this.autopilot.tap(['A']);
        await sleep(2_500);
    }

    // --- state checks ------------------------------------------------------------------------------------------------------

    /** For the event stream: is the app still on screen? Back in hbmenu or on the HOME menu means it ended. */
    private async appGone(): Promise<string | null> {
        const shot = await this.autopilot.screenshot();
        if (await this.screenIs('hbmenu-header', shot)) return 'the console is back in hbmenu';
        if (await this.screenIs('home-first-tile', shot)) return 'the console shows the HOME menu';
        return null;
    }

    private async checkReachable(): Promise<void> {
        try {
            await this.autopilot.status();
        } catch (e) {
            if (e instanceof ConsoleUnreachableError) throw new ConsoleUnreachableError(`${e.message}\n${UNREACHABLE_HELP}`);
            throw e;
        }
    }

    /** File names in the crash folders, with their folder: "atmosphere/fatal_reports/0001_0100....log". */
    private async crashLists(): Promise<{ crash: string[]; fatal: string[] }> {
        const names = async (dirs: readonly string[]) =>
            (await Promise.all(dirs.map(async (d) => (await this.autopilot.listDir(d)).map((e) => `${d.slice(1)}/${e.name}`)))).flat();
        return { crash: await names([CRASH_REPORTS_DIR]), fatal: await names(FATAL_DIRS) };
    }

    /** Compares the crash folders with the lists taken at launch. New fatal file → hard stop. New crash report → failure. */
    private async checkCrashes(): Promise<void> {
        const before = this.crashBefore;
        if (!before) return;
        this.crashBefore = null;
        const after = await this.crashLists();
        const newFatal = after.fatal.filter((n) => !before.fatal.includes(n));
        if (newFatal.length) {
            const message =
                `NEW FATAL ERROR FILE on the console: ${newFatal.join(', ')} ` +
                `(scenario output: ${this.out}). The E2E harness stopped and will not run on the console again. ` + UNREACHABLE_HELP;
            writeFileSync(STOP_FILE, `${new Date().toISOString()} ${message}\n`);
            throw new FatalConsoleError(message);
        }
        const newCrash = after.crash.filter((n) => !before.crash.includes(n));
        if (newCrash.length) {
            throw new Error(`the app crashed: new crash report(s) ${newCrash.join(', ')}`);
        }
    }

    // --- screen checks -----------------------------------------------------------------------------------------------------

    private reference(name: string): Reference {
        const cached = this.refs.get(name);
        if (cached) return cached;
        const entry = this.profile.references[name];
        if (!entry || typeof entry === 'string') throw new Error(`profile.json has no reference "${name}"`);
        const file = join(DEVICE_DIR, 'local', `${name}.png`);
        if (!existsSync(file)) {
            throw new Error(`missing reference crop ${file}: take a screenshot of "${entry.screen}" and crop ${entry.region} (profile.json)`);
        }
        const ref = { region: parseRegion(entry.region), png: readFileSync(file) };
        this.refs.set(name, ref);
        return ref;
    }

    /** True when the screen (a fresh screenshot, or `shot`) shows the reference crop `name`. */
    private async screenIs(name: string, shot?: Buffer): Promise<boolean> {
        const ref = this.reference(name);
        const image = shot ?? (await this.autopilot.screenshot());
        return (await regionRmse(image, ref.region, ref.png)) < this.profile.screenCheck.threshold;
    }

    /** Polls screenshots until the reference matches. False after the timeout. */
    private async waitForScreen(name: string, timeoutMs: number): Promise<boolean> {
        const until = Date.now() + timeoutMs;
        do {
            if (await this.screenIs(name)) return true;
            await sleep(300);
        } while (Date.now() < until);
        return false;
    }

    // --- files -------------------------------------------------------------------------------------------------------------

    private sdPath(path: string): string {
        if (path.startsWith('/') || path.split(/[\\/]/).some((s) => s === '..' || s === '.')) throw new Error(`not a path inside the data folder: ${path}`);
        return `${APP_DIR}/${path}`;
    }

    /** PUT writes in place and deletes the file if the upload breaks off, so the hash is checked; one retry on a mismatch. */
    private async putVerified(sdPath: string, data: Buffer): Promise<void> {
        const want = createHash('sha256').update(data).digest('hex');
        for (let attempt = 1; attempt <= 2; attempt++) {
            await this.autopilot.putFile(sdPath, data);
            if ((await this.autopilot.sha256(sdPath)) === want) return;
        }
        throw new Error(`upload of ${sdPath} failed twice (SHA-256 mismatch)`);
    }
}
