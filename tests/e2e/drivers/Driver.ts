// One scenario, two drivers (docs/TESTING.md "Drivers"). A scenario only talks to this interface, so the same test runs on
// the desktop build (DesktopDriver: Xvfb + xdotool) and on the console (SwitchDriver: sys-autopilot-nx REST).

import type { EventStream } from '../lib/events.ts';

export type DriverName = 'desktop' | 'switch';

/** Controller buttons, with sys-autopilot's names. The desktop driver has only some of them (see DesktopDriver). */
export type Button =
    | 'A' | 'B' | 'X' | 'Y' | 'L' | 'R' | 'ZL' | 'ZR' | 'PLUS' | 'MINUS'
    | 'UP' | 'DOWN' | 'LEFT' | 'RIGHT' | 'LSTICK' | 'RSTICK' | 'HOME';

/**
 * WP-21 (D62): the Album's heap as the desktop pretends to have it in applet mode (--mem-limit), so its budgets match the
 * console's: 259.7 MiB there (WP-21's console checks; the first guess was 380).
 */
export const ALBUM_HEAP_MB = 260;

export interface LaunchOptions {
    /**
     * 'application' or 'applet' (from the Album, WP-21). Scenario.launch() fills in the run's mode (tools/e2e.sh --mode,
     * SNX_E2E_MODE) when a scenario doesn't name one. Applet: desktop passes --simulate-applet-mode and
     * --mem-limit=ALBUM_HEAP_MB (unless memLimitMb is given), the Switch goes through the Album (WP-21, D62).
     */
    mode?: 'application' | 'applet';
    /**
     * WP-21: the app is expected not to start (applet mode with too little free memory: the low-memory screen), so launch()
     * waits for launch_refused instead of app_start.
     */
    refused?: boolean;
    /** Scenario.launch() only: in applet mode, leave the Album tip to config.json (no "appletTip": false in the test config). */
    appletTipFromConfig?: boolean;
    /** Written to test/config.json before the start (server URL, auth, no animations, ...). Without it, that file is removed. */
    testConfig?: object;
    /** Desktop only: start with --mem-limit=<MB> (low-memory runs). The Switch driver refuses it. */
    memLimitMb?: number;
    /** More app flags. */
    args?: string[];
    /**
     * WP-08: keep the app's config.json from the last launch (a restart test). By default every launch removes config.json
     * and config.json.bak first, so each scenario starts like a first run unless its test config names a server. The waiting
     * progress saves (progress-queue.json + .bak, WP-15b) go and stay the same way (ownerConfig.ts CONFIG_FILES).
     */
    keepConfig?: boolean;
}

/** A refused start writes only launch_refused (SAFETY.md S12), so launch() waits for it instead of app_start. */
export function startEventFor(opts: LaunchOptions = {}): 'app_start' | 'launch_refused' {
    return opts.refused ? 'launch_refused' : 'app_start';
}

export interface Driver {
    readonly name: DriverName;

    /** Desktop: checks the build folder. Switch: uploads the .nro to sdmc:/switch/suwayomi-nx/ unless the same file is there (SHA-256). */
    deploy(buildPath: string): Promise<void>;

    /** Starts the app and waits for its app_start event. A new events() stream starts with every launch. */
    launch(opts?: LaunchOptions): Promise<void>;

    /**
     * Quits the app the normal way (desktop: SIGTERM, which the app turns into a clean quit; Switch: PLUS) and checks that
     * it ended cleanly: desktop exit code 0, Switch no new crash report. A new fatal_errors file on the Switch stops everything.
     */
    quit(): Promise<void>;

    /** Taps a button (default 100 ms) or holds it for `ms`. */
    press(btn: Button, ms?: number): Promise<void>;
    /** Taps buttons one after another with a short pause. */
    sequence(btns: Button[]): Promise<void>;
    /** Pushes a stick (x, y from -1 to 1) for `ms`. Not on desktop yet. */
    stick(side: 'left' | 'right', x: number, y: number, ms: number): Promise<void>;
    /** One finger at screen pixels 0-1279 × 0-719. Tap = 100 ms by default. */
    touch(x: number, y: number, ms?: number): Promise<void>;
    /** One finger drag from (x1, y1) to (x2, y2) over `ms`. */
    swipe(x1: number, y1: number, x2: number, y2: number, ms: number, steps?: number): Promise<void>;
    /**
     * WP-08: types text into the keyboard dialog that is open (desktop: borealis's text dialog reads the PC keyboard).
     * The Switch can't (the system keyboard has no automation): NotSupportedError, so its scenarios fill fields from the
     * test config instead.
     */
    typeText(text: string): Promise<void>;

    /** The screen at 1280×720: PNG on desktop (lossless), JPEG on the Switch. sharp and the golden compare read both. */
    screenshot(): Promise<Buffer>;

    /**
     * Asks the app to close the way the system does, and doesn't wait: desktop SIGTERM, Switch HOME → Close software
     * (an exit request, which borealis answers by quitting). Then call waitForExit().
     */
    requestExit(): Promise<void>;

    /**
     * Waits until the app has ended by itself (a crash drill) or after requestExit(), and says how: desktop "exited with
     * code 1" or "killed by SIGSEGV", Switch "the console is back in hbmenu" or "the console shows the HOME menu". On the
     * Switch it then checks the crash folders like quit() does (a new fatal file stops everything).
     */
    waitForExit(timeoutMs: number): Promise<string>;

    /** The names in a folder of the app's data folder, e.g. "logs". Missing folder = []. */
    listFiles(dir: string): Promise<string[]>;

    /** A file in the app's data folder (D10), e.g. "logs/app.log". Missing file = empty buffer. */
    readFile(path: string, offset?: number): Promise<Buffer>;
    /** Writes a file in the app's data folder (D10), e.g. "test/config.json". */
    writeFile(path: string, data: Buffer | string): Promise<void>;
    /**
     * WP-18: deletes a folder of the app's data folder with everything in it (the switch-downloads scenario's known state:
     * the test server's downloads folder). Desktop only. The Switch can't (sys-autopilot deletes nothing, S10):
     * NotSupportedError; there the app deletes its own downloads.
     */
    removeFolder(dir: string): Promise<void>;

    /** The events of the current launch. Throws before the first launch(). */
    events(): EventStream;

    /**
     * End of a scenario: stops the app if it still runs and puts things back (desktop: stops Xvfb; Switch: closes the software
     * and goes back to the HOME menu's first tile). Safe to call twice.
     */
    dispose(): Promise<void>;
}

/** Something the driver can't do on this platform. */
export class NotSupportedError extends Error {}
