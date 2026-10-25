import { defineConfig } from 'vitest/config';

// Node unit tests for the test tooling (npm test). E2E scenarios get their own config in WP-03.
export default defineConfig({
    test: {
        include: ['tools/**/*.test.ts', 'tests/e2e/lib/**/*.test.ts'],
        exclude: ['**/node_modules/**', 'tools/sys-autopilot-nx/**', 'library/**'],
        testTimeout: 60_000,
    },
});
