import { defineConfig } from 'vitest/config';

// E2E scenarios (npm run e2e, usually through tools/e2e.sh). One app, one screen, one console: everything runs one at a time,
// and the first failure stops the run (on the Switch nothing should carry on after something went wrong).
export default defineConfig({
    test: {
        include: ['tests/e2e/scenarios/**/*.test.ts'],
        fileParallelism: false,
        maxWorkers: 1,
        bail: 1,
        testTimeout: 6 * 60_000,
        hookTimeout: 2 * 60_000,
        reporters: ['verbose'],
    },
});
