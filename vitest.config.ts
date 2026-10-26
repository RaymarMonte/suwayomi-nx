import { defineConfig } from 'vitest/config';

// Node unit tests for the test tooling (npm test). E2E scenarios have their own config: vitest.e2e.config.ts (npm run e2e).
export default defineConfig({
    test: {
        include: ['tools/**/*.test.ts', 'tests/e2e/lib/**/*.test.ts', 'tests/e2e/drivers/**/*.test.ts', 'tests/contract/**/*.test.ts'],
        exclude: ['**/node_modules/**', 'tools/sys-autopilot-nx/**', 'library/**'],
        testTimeout: 60_000,
    },
});
