// Scenario plumbing shared by every tests/e2e/scenarios/*.test.ts (how to write one: tests/e2e/README.md).
// tools/e2e.sh sets the environment: SNX_E2E_DRIVER (desktop|switch), SNX_E2E_BUILD (desktop build folder or .nro),
// SNX_E2E_VISIBLE=1 (desktop on the WSLg screen), SNX_UPDATE_GOLDENS=1 (write goldens instead of comparing).

import { mkdirSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { expect, it } from 'vitest';

import { DesktopDriver } from '../drivers/DesktopDriver.ts';
import type { Driver, DriverName, LaunchOptions } from '../drivers/Driver.ts';
import { SwitchDriver } from '../drivers/SwitchDriver.ts';
import type { AppEvent, EventStream } from './events.ts';
import { expectGolden, type GoldenResult, type MaskRect } from './golden.ts';

export const OUT_ROOT = fileURLToPath(new URL('../out/', import.meta.url));

export function driverName(): DriverName {
    const name = process.env.SNX_E2E_DRIVER ?? 'desktop';
    if (name !== 'desktop' && name !== 'switch') throw new Error(`SNX_E2E_DRIVER must be desktop or switch, not ${name}`);
    return name;
}

function buildPath(name: DriverName): string {
    const path = process.env.SNX_E2E_BUILD;
    if (path) return path;
    const home = process.env.HOME ?? '';
    return name === 'desktop' ? join(home, 'build/suwayomi-nx/desktop-test') : join(home, 'build/suwayomi-nx/switch-test/suwayomi-nx.nro');
}

/** What a scenario body gets. */
export class Scenario {
    readonly name: string;
    readonly driver: Driver;
    readonly out: string; // tests/e2e/out/<scenario>/, emptied at the start

    constructor(name: string, driver: Driver, out: string) {
        this.name = name;
        this.driver = driver;
        this.out = out;
    }

    get events(): EventStream {
        return this.driver.events();
    }

    /**
     * Starts the app and returns its app_start event, after the first assertion of every scenario (TESTING.md): it runs in
     * application mode with the Full profile. With `mode: 'applet'` it checks for applet mode instead.
     */
    async launch(opts: LaunchOptions = {}): Promise<AppEvent> {
        await this.driver.launch(opts);
        const start = await this.events.waitForEvent('app_start');
        if (opts.mode === 'applet') {
            expect(start.mode, 'app_start.mode').toBe('applet');
        } else {
            expect(start.mode, 'app_start.mode').toBe('application');
            expect(start.profile, 'app_start.profile').toBe('full');
        }
        expect(start.build, 'app_start.build (a test build writes events)').toBe('test');
        expect(start.platform, 'app_start.platform').toBe(this.driver.name);
        return start;
    }

    /** Takes a screenshot and compares it with tests/e2e/goldens/<driver>/<scenario>/<step>.png. */
    async expectGolden(step: string, opts: { masks?: MaskRect[]; threshold?: number; maxDiffPercent?: number } = {}): Promise<GoldenResult> {
        const shot = await this.driver.screenshot();
        return expectGolden(shot, { driver: this.driver.name, scenario: this.name, step, outDir: this.out, ...opts });
    }

    /** No "error" events so far (TESTING.md smoke rule). */
    expectNoErrorEvents(): void {
        expect(this.events.errors(), 'error events').toEqual([]);
    }

    /** On failure, while the app may still be on screen: the last screen. */
    async saveLastScreen(): Promise<void> {
        try {
            const shot = await this.driver.screenshot();
            writeFileSync(join(this.out, `last-screen.${this.driver.name === 'desktop' ? 'png' : 'jpg'}`), shot);
        } catch {
            // no screen to show (Xvfb gone, console unreachable)
        }
    }

    /**
     * After the app has ended: all events and the app's log. (On the Switch the log can't be read while the app keeps it
     * open for writing.)
     */
    async saveLogs(): Promise<void> {
        try {
            writeFileSync(join(this.out, 'events.jsonl'), this.driver.events().text());
        } catch {
            // never launched
        }
        try {
            const log = await this.driver.readFile('logs/app.log');
            writeFileSync(join(this.out, 'app.log'), log.toString('utf8').replace(/\x1b\[[0-9;]*m/g, ''));
        } catch {
            // no log
        }
    }
}

export interface ScenarioOptions {
    /** Only run on these drivers; other drivers skip the test. */
    drivers?: DriverName[];
    timeoutMs?: number;
}

/**
 * Declares one scenario test. The body gets a Scenario with a deployed driver; afterwards the driver is always disposed,
 * and on failure the event log, the app log and the last screenshot land in tests/e2e/out/<name>/.
 */
export function scenario(name: string, body: (s: Scenario) => Promise<void>, opts: ScenarioOptions = {}): void {
    const which = driverName();
    const skip = opts.drivers !== undefined && !opts.drivers.includes(which);
    const timeout = opts.timeoutMs ?? (which === 'switch' ? 6 * 60_000 : 2 * 60_000);
    it.skipIf(skip)(`${name} (${which})`, async () => {
        // Empty the folder rather than delete it: Windows refuses to delete a folder that is open somewhere (Explorer, a shell).
        const out = join(OUT_ROOT, name);
        mkdirSync(out, { recursive: true });
        for (const entry of readdirSync(out)) rmSync(join(out, entry), { recursive: true, force: true });
        const driver: Driver =
            which === 'desktop' ? new DesktopDriver({ out, visible: process.env.SNX_E2E_VISIBLE === '1' }) : new SwitchDriver({ out });
        const s = new Scenario(name, driver, out);
        try {
            await driver.deploy(buildPath(which));
            await body(s);
        } catch (e) {
            await s.saveLastScreen();
            throw e;
        } finally {
            try {
                await driver.dispose();
            } finally {
                await s.saveLogs();
            }
        }
    }, timeout);
}
