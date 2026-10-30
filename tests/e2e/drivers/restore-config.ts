// Puts the console's own config.json back if a run left the tests' one there (ownerConfig.ts). tools/e2e.sh switch runs this
// after every console run, also after a failure or Ctrl+C; `tools/e2e.sh switch --restore-config` runs only this.
// With nothing pending it only says so and doesn't contact the console.

import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

import { fail, isMain } from '../lib/cli.ts';
import { OWNER_CONFIG_DIR, readPendingOwnerConfig } from './ownerConfig.ts';
import { STOP_FILE, SwitchDriver } from './SwitchDriver.ts';

async function main(): Promise<void> {
    const pending = readPendingOwnerConfig();
    if (!pending) {
        console.log("[e2e] the console's own config.json: nothing to put back");
        return;
    }
    if (existsSync(STOP_FILE)) {
        fail(
            `the console's own config.json was not put back: ${STOP_FILE} says the console needs checking first. Its copy ` +
                `(from ${pending.takenAt}) stays in ${OWNER_CONFIG_DIR}; once that file is deleted, run tools/e2e.sh switch --restore-config.`,
        );
    }
    const driver = new SwitchDriver({ out: fileURLToPath(new URL('../out/', import.meta.url)) });
    await driver.restoreOwnerConfig();
    console.log(`[e2e] put the console's own config.json back (as it was at ${pending.takenAt})`);
}

if (isMain(import.meta.url)) {
    try {
        await main();
    } catch (e) {
        fail((e as Error).message);
    }
}
