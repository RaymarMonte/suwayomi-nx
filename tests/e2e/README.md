# E2E harness

End-to-end tests drive the real app: the desktop build on a hidden X screen, or the `.nro` on the test Switch. One scenario file runs
on both. Background and the full plan: `docs/TESTING.md` (layers 4 and 5). Decisions: D24 (device launch), D32 (harness design).

```
tests/e2e/
  scenarios/*.test.ts   the scenarios (vitest), one file per feature
  lib/scenario.ts       scenario() + the Scenario object every scenario gets
  lib/events.ts         EventStream: reads the app's test events, waitFor()
  lib/golden.ts         golden screenshot compare (pixelmatch), masks, --update-goldens
  lib/pagecode.ts       reads the fixture pages' colour code from a screenshot (which manga page is on screen)
  lib/suwayomi.ts       GraphQL client for the test server (check what the app changed on the server)
  drivers/              Driver interface, DesktopDriver, SwitchDriver, sys-autopilot client with the safety allowlist
  goldens/<driver>/<scenario>/<step>.png   committed golden screenshots
  device/               profile.json (how to drive this console), .env (address + token, git-ignored), local/ (reference crops, git-ignored)
  out/<scenario>/       what a run leaves behind: events.jsonl, app.log, golden diffs, last screen on failure (git-ignored)
  out/owner-config/     Switch: the copy of the console's own config.json (+ .bak) the run puts back (git-ignored, see Device safety)
```

## Running

Inside WSL, from the repo (`wsl.exe -d Ubuntu --exec bash -lc '<repo>/tools/e2e.sh desktop smoke'` from Windows):

```bash
tools/e2e.sh desktop              # every scenario on the desktop build (builds the test build first)
tools/e2e.sh desktop smoke        # one scenario: tests/e2e/scenarios/smoke.test.ts
tools/e2e.sh switch smoke         # the same on the console (on, reachable, HOME menu's first tile = the app-mode game)
tools/e2e.sh desktop smoke --visible          # watch it in a WSLg window instead of the hidden Xvfb screen
tools/e2e.sh desktop smoke --update-goldens   # write new golden screenshots (only for an intended UI change)
tools/e2e.sh switch smoke --no-build          # reuse the last test build
tools/e2e.sh switch --restore-config          # only put the console's own config.json back (after a run that couldn't)
```

The run stops at the first failure (`bail: 1`). Then look in `tests/e2e/out/<scenario>/`: the error message names the file to open.

## Writing a scenario

```ts
// tests/e2e/scenarios/library-browse.test.ts
import { expect } from 'vitest';

import { scenario } from '../lib/scenario.ts';

scenario('library-browse', async (s) => {
    await s.launch({ testConfig: { server: { url: '...' } } }); // checks app_start: application mode, Full profile, test build
    await s.events.waitForEvent('screen', { name: 'Library' });
    await s.events.waitForEvent('idle');

    const mark = s.events.mark(); // only look at events after this point
    await s.driver.press('DOWN');
    const focus = await s.events.waitForEvent('focus', {}, { from: mark });
    expect(focus.mangaId).toBe(12);
    await s.events.waitForEvent('idle', {}, { from: mark });

    await s.expectGolden('second-row', { masks: [{ x: 0, y: 650, w: 420, h: 70, why: 'clock' }] });
    s.expectNoErrorEvents();
    await s.driver.quit(); // checks a clean exit (desktop) or no new crash report (Switch)
});
```

`scenario(name, body, { drivers: ['desktop'] })` limits a scenario to some drivers (the others skip it); `timeoutMs` changes the
time limit (default 2 min desktop, 6 min Switch). Afterwards the driver always cleans up (desktop: stops the app and Xvfb;
Switch: closes the software and goes back to the HOME menu), and the logs land in `out/<scenario>/`.

### Rules

1. **Wait for events, never sleep.** Every action has an event that says it is done (`screen`, `focus`, `reader`, ...), and `idle` says
   the app has settled. Take `s.events.mark()` before the action and wait `{ from: mark }`, or you may match an older event. If an action
   has no event yet, add one to the app (`src/app/TestHooks`, WP-08 brings the full set) instead of sleeping.
2. `idle` is written once per quiet spell: after a new `screen`, or after something was animating. An action that changes nothing on
   screen gives no new `idle`, so wait for the action's own event first.
3. **Assert on events first, screenshots second.** Events are text and cheap. Use a golden for what only the picture shows (layout,
   colours). Mask anything that changes by itself: the clock (bottom left), battery, Wi-Fi, animations. The Switch golden is a JPEG
   screenshot, so keep the default tolerance there.
4. **Same scenario, both drivers.** Use `press`, `touch`, `swipe` and `readFile`/`writeFile` (paths inside the app's data folder, like
   `logs/app.log`). Desktop limits: buttons A (Enter), B (Escape), X (middle mouse) and the D-pad only (`press('Y')` throws
   NotSupportedError there); no sticks. Touch works on both (desktop: the mouse is one finger).
5. **An app that ends by itself** (crash drills, `scenarios/crash-drills.test.ts`): `waitForExit(ms)` instead of `quit()`; it
   says how it ended (desktop "exited with code 1", Switch one of `SwitchDriver`'s `ENDED_*`). `requestExit()` is the system's exit request (desktop SIGTERM, Switch HOME →
   Close software). `listFiles('logs')` lists a folder of the data folder; `lib/crashrecord.ts` reads crash records.
6. **Low memory and applet mode:** `launch({ memLimitMb })` is desktop only. `launch({ mode: 'applet' })` is only for the
   applet-guard scenario (WP-08: it waits for `launch_refused`, the only event then). On the Switch it is opt-in
   (`SNX_APPLET_CONSOLE=1`, owner watching) and never runs a drill: from the Album any crash is a system fatal (D41).
7. Fixture manga and the test server: `docs/TESTING.md` "Fixture manga", `tools/test-server/README.md`. Read the page on screen with
   `findPageCodes()` from `lib/pagecode.ts` (decode the screenshot with sharp first), not by looking at the picture.
8. **WP-08:** every `launch()` removes `config.json` first (a first run) unless `keepConfig: true` (restart tests). On the Switch
   that is the owner's own file: the driver copies it first and puts it back after the scenario (Device safety). Text fields:
   `typeText()` on desktop (tap the field, type, tap the dialog's OK hint, `KEYBOARD_OK`), the test config's `showSetup` on the
   Switch. Touch coordinates of the app's screens: `lib/screens.ts`.

### Goldens

`s.expectGolden('step')` compares with `goldens/<driver>/<scenario>/step.png`. A missing golden fails with a hint. Make or refresh goldens
with `--update-goldens`, then **look at the new PNGs** (masked parts are magenta) and say in the commit that the UI change was intended.
A mismatch writes `out/<scenario>/<step>.actual.png` and `<step>.diff.png` (red = different pixels).

## Device safety (the SwitchDriver does this for you)

- It only calls the sys-autopilot endpoints on the SAFETY.md S10 allowlist (`drivers/autopilot.ts`, unit-tested). Anything else throws.
- Before every press that could start something outside the app (HOME menu, hbmenu) it checks the screen against a reference crop.
- It lists `atmosphere/crash_reports`, `fatal_errors` and `fatal_reports` before and after every launch, and once more at the end of the
  scenario against the first launch (a report that comes a moment late). A new crash report fails the scenario. A new
  fatal file stops everything and writes `out/STOP-console-fatal.txt`: the driver refuses to run until the owner has looked at the console
  and deleted that file.
- After a crash the HOME menu says "The software was closed because an error occurred." (D35). `waitForExit()` reports that
  (`ENDED_SOFTWARE_CLOSED`) and dismisses it with A, only after the screen check (reference crop `software-closed`).
- **The owner's settings** (`drivers/ownerConfig.ts`): the test console is also the owner's reading console, and its
  `config.json` names the owner's real server. Before the first launch changes it, the driver copies `config.json` and
  `config.json.bak` (whichever exist) to `out/owner-config/`, checks each copy against the console's SHA-256, and writes
  `pending.json` last. At the end of every scenario it uploads them again with a hash check (or deletes what the tests left where
  there was no file), removes the harness's `test/config.json` (a test build would start with the test server), and renames
  `pending.json` to `restored.json`. `tools/e2e.sh switch` runs `drivers/restore-config.ts` once more after the run, also after a
  failure or Ctrl+C, for a scenario stopped before it could. A `pending.json` still there means the console has the tests' files:
  it is never overwritten, the next run puts it back, or `tools/e2e.sh switch --restore-config` does. After a new fatal file it
  waits (nothing touches the console until the owner has checked it). Only files inside `sdmc:/switch/suwayomi-nx/`.
- It sends one request at a time, at least 150 ms apart: bursts of connections make the console reset new ones for ~10 s.
- If the console stops answering, the error asks the owner to look at it (it might show the "press POWER" fatal screen).
- Missing reference crops (`device/local/*.png`): `device/profile.json` says which screen and region to crop.
