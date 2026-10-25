// Small helpers for the Node command-line tools.

import { realpathSync } from 'node:fs';
import { pathToFileURL } from 'node:url';

/** True when the module with this import.meta.url was started directly with `node <file>` (not imported by a test). */
export function isMain(moduleUrl: string): boolean {
    const script = process.argv[1];
    if (!script) return false;
    try {
        return pathToFileURL(realpathSync(script)).href === moduleUrl;
    } catch {
        return false;
    }
}

/** Prints the error without a stack trace for expected failures, then exits with code 1. */
export function fail(message: string): never {
    console.error(`error: ${message}`);
    process.exit(1);
}
