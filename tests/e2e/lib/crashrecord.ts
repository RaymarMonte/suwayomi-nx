// Reads the app's crash records (logs/crash-<time>-<n>.txt; format: src/app/crash/README.md). The app's own reader is
// core::crashrecord::parseCrashRecord(); this is the same thing for the crash-drill scenarios.

export const HEADER = 'suwayomi-nx crash record 1';
export const FOOTER = 'end of crash record';

export interface CrashRecord {
    type: string; // cpu_exception, abort, gpu_abort, terminate, main_loop_exception, worker_exception, watchdog
    what: string;
    thread: string; // main, watchdog or other
    platform: string;
    mode: string;
    buildId: string;
    complete: boolean; // the footer is there: the whole record reached the disk
    stack: string[]; // "BASE + 0x1a2b" or plain addresses, innermost first
    log: string[];
}

/** File names of new records in logs/ (filed away to logs/crashes/ by the next launch). */
export function isCrashFileName(name: string): boolean {
    return /^crash-.+\.txt$/.test(name);
}

/** null if the text doesn't start like a crash record. */
export function parseCrashRecord(text: string): CrashRecord | null {
    if (!text.startsWith(HEADER)) return null;
    const record: CrashRecord = { type: '', what: '', thread: '', platform: '', mode: '', buildId: '', complete: false, stack: [], log: [] };
    let section = '';
    for (const raw of text.split('\n')) {
        const line = raw.replace(/\r$/, '');
        if (line === FOOTER) {
            record.complete = true;
            continue;
        }
        if (line.startsWith('  ')) {
            if (section === 'stack') record.stack.push(line.trim().replace(/^#\d+ /, ''));
            else if (section === 'log') record.log.push(line.slice(2));
            continue;
        }
        section = line.startsWith('stack') ? 'stack' : line.startsWith('log') ? 'log' : '';
        const colon = line.indexOf(': ');
        if (colon < 0) continue;
        const key = line.slice(0, colon);
        const value = line.slice(colon + 2);
        if (key === 'type') record.type = value;
        else if (key === 'what') record.what = value;
        else if (key === 'thread') record.thread = value;
        else if (key === 'platform') record.platform = value;
        else if (key === 'mode') record.mode = value;
        else if (key === 'build_id') record.buildId = value;
    }
    return record;
}
