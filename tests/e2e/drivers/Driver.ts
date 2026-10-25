// One scenario, two drivers (docs/TESTING.md "Drivers"). A scenario only talks to this interface, so the same test runs on
// the desktop build (DesktopDriver: Xvfb + xdotool) and on the console (SwitchDriver: sys-autopilot-nx REST).

import type { EventStream } from '../lib/events.ts';

export type DriverName = 'desktop' | 'switch';

/** Controller buttons, with sys-autopilot's names. The desktop driver has only some of them (see DesktopDriver). */
export type Button =
    | 'A' | 'B' | 'X' | 'Y' | 'L' | 'R' | 'ZL' | 'ZR' | 'PLUS' | 'MINUS'
    | 'UP' | 'DOWN' | 'LEFT' | 'RIGHT' | 'LSTICK' | 'RSTICK' | 'HOME';

export interface LaunchOptions {
    /**
     * 'application' (default) for every scenario (D21). 'applet' only for the applet-guard scenario (WP-08):
     * desktop passes --simulate-applet-mode, the Switch goes through the Album.
     */
    mode?: 'application' | 'applet';
    /** Written to test/config.json before the start (server URL, auth, no animations, ...). Without it, that file is removed. */
    testConfig?: object;
    /** Desktop only: start with --mem-limit=<MB> (low-memory runs). The Switch driver refuses it. */
    memLimitMb?: number;
    /** More app flags. */
    args?: string[];
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

    /** The screen at 1280×720: PNG on desktop (lossless), JPEG on the Switch. sharp and the golden compare read both. */
    screenshot(): Promise<Buffer>;

    /** A file in the app's data folder (D10), e.g. "logs/app.log". Missing file = empty buffer. */
    readFile(path: string, offset?: number): Promise<Buffer>;
    /** Writes a file in the app's data folder (D10), e.g. "test/config.json". */
    writeFile(path: string, data: Buffer | string): Promise<void>;

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
