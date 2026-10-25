# WP-03 E2E harness: drivers + smoke scenario

Model: **Opus** · Size: L · Needs: WP-01, WP-02 · Owner: console on, Joy-Cons per WP-00 finding

## Goal
Turn the WP-00 spike into the real Node/TypeScript harness: one scenario format, two drivers (Desktop, Switch),
event-log assertions, golden screenshot diffs, crash detection. Prove it with a `smoke` scenario on both drivers,
using a minimal test-hook event writer added to the Hello app.

## Read first
`docs/TESTING.md` (all), STATUS.md console facts, `tools/spike/`.

## Deliverables
- [x] `tests/e2e/` in the root npm package (D30, made in WP-02; `tests/e2e/lib/` already has `pagecode.ts`, `suwayomi.ts`, `cli.ts`):
      add pixelmatch, jpeg-js/pngjs and an E2E vitest config
- [x] `Driver` interface + `DesktopDriver` (Xvfb by default, `--visible` uses WSLg; xdotool; ImageMagick screenshot)
- [x] `launch()` starts the app in application mode on both drivers (D21). Switch uses the hold-R path recorded in WP-00
      (`tests/e2e/device/profile.json`). Desktop passes `--mem-limit` when a scenario asks for it. `launch({mode: 'applet'})` exists only
      for the later `applet-guard` scenario (WP-08): desktop passes `--simulate-applet-mode`, Switch uses the Album path
- [x] `SwitchDriver` (sys-autopilot REST with token from `tests/e2e/device/.env`). Deploy+hash-verify, launch (method from WP-00),
      quit, recover-to-home, crash-report diff, fatal-screen detection → clear message asking the owner to press POWER
- [x] `EventStream`: tail `test/events.jsonl` (file on desktop, `GET /files?offset=` polling on Switch). `waitFor(pred, timeout)`
- [x] Minimal C++ `TestHooks` in `src/app/` emitting `app_start`, `screen`, `idle` (full set comes in WP-08)
- [x] Golden compare with masks + `--update-goldens`. Failure artefacts in `tests/e2e/out/<scenario>/`
- [x] `smoke.test.ts` passing on desktop and on the Switch
- [x] `tools/e2e.sh desktop|switch [scenario]` and CLAUDE.md commands
- [x] Short `tests/e2e/README.md`: how to write a scenario (used by every later UI WP)

## Acceptance
`tools/e2e.sh desktop smoke` and `tools/e2e.sh switch smoke` both green twice in a row
(the Hello app's `app_start` reports `application`). `launch({mode: 'applet'})` checked once on desktop (`app_start` reports `applet`;
the Hello app has no guard yet). On the Switch it's first used in WP-08, once the crash handler (WP-03b) exists. A deliberately broken golden
fails with a diff image. Killing the app mid-test gives a clear error, not a hang.

## Sonnet-safe chunks
- Golden-diff utility + its unit tests, given the interface.
- README for writing scenarios, after Opus writes the first scenario.
