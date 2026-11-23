# IS-005 Two desktop e2e checks fail now and then: Resume's 2 s, and a lost press or tap

Kind: bug fix (test harness) · Status: done · Opened: 2026-11-23 · Closed: 2026-11-23
Outcome: fixed (test-only; D58/D62 amended with the owner's answer: Resume's 2 s is timed on screen; the desktop driver
waits for the app to read each input; the scenarios wait for an `idle` that comes after the input and its answer)
Model: Opus · Owner: – (D58's Resume limit answered 2026-11-23)

## Report
Found while testing IS-004 (2026-11-22/23, desktop, test server), not by the owner. Two flakes that come and go without
any app change (IS-004 has the runs):
1. `server-downloads` step 4: "Resume → the queue running (ms)" ≤ 2000 (`LIMITS.resumeRunningMs`, D58 as amended by
   D62) fails by 12–85 ms now and then (2012–2083 ms), in both modes, more often from the Album.
2. The desktop driver loses an input now and then: `soak` lost a LEFT press; `updates-history` lost the tap on the
   Reading category sent right after the tap on the Library tab.

## What we checked
- ~08:15 (UTC+08:00), the Resume runs kept in `tests/e2e/out/is-004/` (7 with events) and `tests/e2e/out/server-downloads/`.
  What the app does after a tap on Resume (`src/ui/downloads/ServerDownloads.cpp`, `resume()` / `runQueueAction()` /
  `confirmAction()` / `updatePolling()`):
  - t = 0: the tab shows "Starting..." at once, and the app sends the server's `startDownloader`.
  - 0.5 s (`serverDownloadCheckMs` 500): no answer yet, so one `downloadStatus` read. If it already shows the server
    STARTED, the action counts as done (its `ms` ≈ 503) and the tab shows running.
  - If not: the read changes nothing, and the next poll comes one `serverDownloadPollMs` (1000) later, at ~1.5 s. It
    shows STARTED, and **the tab shows running at ~1.51 s**. But the action itself still waits for its own answer.
  - The server answers `startDownloader` 0.03–0.57 s after that poll (1542–2083 ms after the tap). That answer is the
    `server_download_action` event whose `ms` the test checks.
  - Per run (the tap = the action's `t` − `ms`; "on screen" = the first `server_downloads` event with view running):

    | run | action's answer (what the test checks) | running on screen |
    |---|---|---|
    | e2e-app (application) | 503 | 503 |
    | e2e-baseline app-1 (application) | **2067** | 1511 |
    | e2e-baseline app-2 (application) | 1737 | 1509 |
    | e2e-baseline applet-1 | 1542 | 1511 |
    | e2e-final applet | **2012** | 1510 |
    | e2e-fix applet | **2013** | 1513 |
    | e2e-fix application | **2036** | 1513 |
    | server-downloads (last run) | 503 | 504 |

  So the 2 s limit is checked on the server's answer, not on what the screen shows. On screen the queue showed running
  after ~0.5 s or ~1.51 s in every run, 0.49 s inside the limit. Which of the two depends on whether the server's 1 s beat
  (SUWAYOMI-API.md) comes before or after the 0.5 s check. Why the server's answer comes later than its own
  `downloadStatus` shows STARTED is the server's business; a guess: the mutation waits for its once-a-second status
  update, while `downloadStatus` reads the live state.
- ~08:20, the lost inputs. borealis reads the keyboard and the mouse buttons once a frame (`glfwGetKey` /
  `glfwGetMouseButton`, `library/lib/platforms/glfw/glfw_input.cpp`), so a release and the next press handled in the same
  frame look like the key was held the whole time: no new press. borealis logs each key as GLFW hands it over
  (`Key: … / Code: … / Action: 1|0`, stdout, flushed per line). In the failed soak run (`e2e-fix/application-soak/app.log`)
  the presses come every ~138 ms with a key-up → next key-down gap of ~18 ms (the driver's own pace), until the last one:
  down at 21200, up at **21364** (164 ms, not ~120), down at **21364**. The app was busy for ~65 ms (the chapter's end
  page had just come up) and read both at once, so the press was lost and the reader never moved.
  So a fixed pause after each release (the first idea: ≥ 20 ms) would not have saved it: the driver already left 18 ms,
  and the app's frame was longer than that. A pause long enough for such a frame (~100 ms) would add ~50 s to `soak`
  alone (500 presses).
- The mouse buttons aren't logged by borealis, so `updates-history`'s lost tap can't be seen the same way; it is the same
  kind of loss (the Library tab's tap was released while the app was busy, then the next tap came at once).
- ~08:25, the owner's answer (AskUserQuestion, four options: time the screen / keep the server's answer at 2.5 s / the
  app counts the poll as the action's answer / the app polls every 0.25 s while "Starting..."): **time the screen**,
  limit stays 2 s, no app change.
- ~08:30, the driver's fix, first try: wait after each key or button change until borealis's log line for it is in the
  app's stdout file (mouse buttons: followed by the Pause key, which nothing in borealis or the app reacts to: no button,
  no character, no watched key; GLFW code 284). `smoke` waited 5 s for each tap and found nothing: the file stayed
  empty until the app quit. borealis flushes its log only on Windows builds (`logger.hpp`, `#ifdef __MINGW32__`), so on
  Linux stdout into a file goes out in 4 KiB pieces. The driver now starts the app under `stdbuf -oL` (coreutils: line
  by line, same process id): `smoke` 1.1 s again, as before, each Pause line there.
- 08:36 – 09:05, `server-downloads`, `soak`, `updates-history`, 3 rounds × both modes (`tests/e2e/out/is-005/after/`):
  **all 18 green**. Resume on screen: 505–508 ms (4 of the 6 server-downloads runs) or 1512–1513 ms (2); the server's own
  answer 505–1822 ms. Times like before (soak 84–90 s, one 112 s; updates-history 95–116 s; server-downloads 80–93 s).
  But 7 waits of 5 s ("did not read", all on a mouse button's marker): GLFW dropped the Pause key's **press** 7 times in
  ~1,900 (its release line came, the press line not; GLFW's X11 code skips a press it takes for an input method's
  duplicate, `x11_window.c` `keyPressTimes`; why here is not clear). The release line proves the same (the button went
  out before the marker's press and release), so the marker now counts with either line. No real key's press was missed.
- In one soak run (3, applet) a key-up reached the app ~0.6 s late twice (the whole machine stalled ~0.9 s: the app logged
  nothing meanwhile), so LEFT was held ~0.7 s and the reader turned on by itself (borealis repeats a held button; X's own
  auto-repeat lines, 68 `Action: 2`, change nothing there, so turning it off wouldn't help). soak passed anyway. A stall
  of the whole machine is not the driver's to fix.
- 09:05 – 09:47, the whole desktop suite once per mode (`--bail 0`, `suite-application.txt`, `suite-applet.txt`), still
  with the press-only marker: application 9 files red, applet 2. **Caused by the 5 s waits:** `browse` had 4 of them
  (+20 s on its usual ~100 s) and passed its 120 s limit. vitest then moves on without the scenario's cleanup, so browse's
  app kept running on its own X screen and writing into the same data folder: its events mixed into the next scenarios',
  it took a touch command meant for `reader-webtoon` (even into the applet run, 17 min later), and the manga browse adds
  stayed in the library (`updates-history`: skipped 158, not 156; `library-browse`, `smoke`'s golden, ...). Not a new
  problem in the app; a timed-out scenario never stopped its app before either (a follow-up, below). The leftover
  Xvfb was stopped by hand, the test server reset.
- 09:51 – 10:50, with "either marker line": the 3 scenarios 2 rounds × both modes (`final/`), then the whole suite per mode
  (`suite-final-*.txt`). **No "did not read" any more** (0 in the suite). But 4 red: `server-downloads` round 1 in both
  modes, `switch-download-queue` (suite, application), `updates-history` round 1 applet. Round 2 and the applet suite all
  green.
  - 3 of them are one more race, in the harness, not in the input: a tap that opens a menu, then a tap on its entry. The
    second went out ~10 ms after the menu opened and hit the row under it (focus back on the row, the menu still there,
    nothing chosen; `final/1-application-server-downloads/stdout.txt`: the menu shows at 09:51:34.741, the next tap's
    marker at .746). The scenario waits for `idle` between them, but that wait matched an `idle` written just before the
    first tap: `events.mark()` was the count of events *read* so far, and the newest ones weren't polled yet, so they
    counted as after the mark. Applet run: `idle` at t 2871, the tap at 2875. The original `updates-history` lost tap is
    the same kind: there the Library tab's tap had no wait at all before the category tap.
  - 1 is not about input: `updates-history` reads Alpha ch. 3 to page 4 (index 3, saved ok), waits 1.5 s, stops the server
    hard, and after the restart the server has page 0: the test server's database lost its last commit. Not seen before
    in any kept run; a follow-up if it comes back (below).
- ~10:55, the fix for the race: `mark()` first reads the events file (desktop: a plain local read, the app writes each event
  straight to the file), so it is exact; a poll that was waiting meanwhile reads again instead of taking lines twice.
  `updates-history` waits for `idle` after its Library tab taps (`showLibrary`). Unit tests for both in `events.test.ts`.
- 10:58 – 11:55, again 2 rounds + the whole suite (`mark/`, `suite-mark-*.txt`): server-downloads, soak green except one;
  **`updates-history` red every time** (both modes, also in the suite), and `browse` red once (suite, applet).
  - `updates-history`: its second and third Library visits find the tab still on Reading (it keeps its category), so the
    tap on Reading changes nothing and no new `library` event comes. Before, the test passed only because the Library
    tab's own `library` event wasn't read yet at the category tap's mark and counted as its answer: the race had been
    holding this step up. Now `openFromReading` looks at the tab's own `library` event and taps Reading only when the tab
    shows something else (category id 1, 3 manga).
  - `server-downloads` round 1 applet, the same spot as in `final/` (the first chapter menu after the details screen):
    an `idle` at t 2867 (the app settling after a config save), the tap read at 2872. The exact mark didn't help: that
    idle came *after* the mark, before the app read the tap. So "an `idle` from the mark" isn't enough.
  - `browse` (applet): the cover tap (focus at t 82420), an `idle` at 82628, then the Manga screen at 82892 (it waits
    for its data first); the `idle` matched, and the next tap (Categories) went to the Source screen still under it.
    Older than this issue (same in any run where the screen comes late).
- ~11:58, the fix: `events.waitForIdleAfter(from, cause)`, an `idle` after both the mark taken once the tap returned
  (desktop: the app has read the release, and borealis acts on the release) and the event the tap brought. Key presses
  act when the key goes down, so `pressUntil` gives the mark from before the press and the cause. Used in every
  scenario's `tapUntil` / `tapSettled` / `pressUntil` and in `openManga` (`lib/reader.ts`). `updates-history` alone:
  green, 121 s.
- 12:00 – ~13:00, the final check (`mark2/`, `suite-mark2-*.txt`): the 3 scenarios 2 rounds × both modes, **all 12 green**,
  then the whole suite once per mode, **green in both** (29 files; skipped as always: `crash-drill-oom`, `keyboard-wait`).
  0 "did not read". Times as before: server-downloads 81–82 s, soak 84–86 s, updates-history 96–105 s; the whole suite
  1264 / 1269 s (earlier suites 1191–1252 s, but those had red files that ended early). Resume on screen: 505–507 ms
  (3 runs), 1512 ms (1).
- The 4 waits whose cause is a `focus` event (a tap that opens a row's menu, `browse`, `library-tools`): `focus` events
  don't re-arm the idle tracker (`TestHooks.cpp`: only `write()` / `screen()` call `markBusy()`), but the menu's open
  animation makes the app busy, so an `idle` always follows. The Switch writes the same events in the same order, so
  the cause half of `waitForIdleAfter` can't wait in vain there either.

## Cause
1. The check measures the wrong moment: the server's late answer to `startDownloader`, which comes up to ~0.6 s after
   the app already shows the queue running. Not the app's behaviour.
2. The desktop driver sends the next input without knowing whether the app has seen the previous release. When the app
   is in a long frame, both reach it together and borealis sees no new press.
3. (found while checking 2) A scenario's "wait for `idle` after the tap" could match an `idle` that says nothing about
   the tap: one written before the tap but not read yet at the mark, one written between the mark and the moment the app
   read the tap, or one between the tap and its answer. Then the next tap came before the screen had settled (a menu
   that had just opened, a screen still on its way) and hit what was under it. In `updates-history` the same race had
   hidden a wrong step (a tap on the category the tab already showed).

## What changed
No app code changed; no CHANGELOG line (test-only, the owner doesn't see it); no new D-entry (D58 and D62 amended).
- **Resume (cause 1):** `tests/e2e/scenarios/server-downloads.test.ts` step 4 times the queue running from the tap to the
  first `server_downloads` event with view `running`; `LIMITS.resumeRunningMs` stays 2000. Its log line gives both times
  (on screen, and the server's answer). D58's amended bullet and D62 say so; `SUWAYOMI-API.md`'s Start bullet has the
  late answer; TESTING.md's 4e line.
- **Lost inputs (cause 2):** `tests/e2e/drivers/DesktopDriver.ts`: every key or button change waits until borealis's
  `Key: … / Code: … / Action: …` line for it is in the app's stdout file (the app now runs under `stdbuf -oL`, so stdout
  goes out line by line). A mouse button is followed by the Pause key (GLFW 284) as its marker, and either of the
  marker's lines counts. After 5 s without the line the driver warns "did not read" and goes on. So `touch()` returns
  once the app has read the release.
- **Settled waits (cause 3):** `tests/e2e/lib/events.ts`:
  - `mark()` first reads what the app has written when the source has `readNow`. Desktop has it: a plain local read of
    `test/events.jsonl`, `DesktopDriver.readFileNow`.
  - A poll that waited meanwhile reads again instead of taking lines twice.
  - New `waitForIdleAfter(from, cause?)`.
  - 3 new unit tests in `events.test.ts`.

  Every scenario's `tapUntil` / `tapSettled` / `pressUntil` (browse, library-tools, offline-reading, server-downloads,
  switch-download-queue, switch-downloads) and `openManga` (`lib/reader.ts`) wait with it. `updates-history.test.ts`:
  `openFromReading` taps the Reading category only when the Library tab doesn't already show it (3 places).
  TESTING.md has a bullet for each of the three.
- SwitchDriver unchanged. On the Switch the helpers' wait now also needs an `idle` after the tap's answer, which is
  stricter than before and needs no exact mark. **Not run on the Switch.** Its `mark()` can still be up to one poll behind.
- Tests (2026-11-23):
  - `npm run typecheck` green; `npm test` 147 passed (14 skipped, as before), at 13:00.
  - Desktop e2e: `after/` 18 of 18 (the driver fix alone); with the final code `mark2/` 12 of 12, and the whole suite
    green in both modes (What we checked).
- **Follow-ups (not done here):**
  - A scenario that runs past vitest's time limit leaves its app and Xvfb running. Its cleanup never runs, so the app
    keeps writing into the shared data folder, takes later scenarios' commands, and its manga stay in the library
    (09:05 above). Older than this issue.
  - Once (`final/`, updates-history round 1 applet) the test server's database lost the last save after the scenario's
    hard stop 1.5 s later. Not seen again in the 15 runs since. If it comes back: a longer wait, or a gentle stop there.
  - The Switch's events source has no `readNow` (its file comes over the network), so its mark isn't exact. The next
    Switch matrix run will show whether that matters.

## For the owner
Nothing. (D58's Resume limit: answered 2026-11-23, "time the screen".)

## If it comes back
1. `grep '"action":"resume"' events.jsonl` and the first `"view":"running"` after the "starting" event: if the screen
   shows running late (> 1.6 s), the app polls too slowly or the server started late (the test server's log has the
   `Running: …, Queued: …` beat).
2. A lost press: the scenario's `stdout.txt` has borealis's `Key: … / Code: … / Action: …` lines. A key-up and the next
   key-down in the same millisecond = they reached the app in one frame. "did not read" in the test output = the
   driver gave up waiting for a line (5 s); many of them slow a scenario past its time limit.
3. A tap that hits what's under a menu or a new screen: in `events.jsonl`, the `idle` the helper took (it comes before
   the screen or menu the tap opened?), and in `stdout.txt` the tap's marker line (Code 284) against the event times.
Runs: `tests/e2e/out/is-004/` (IS-004's) and `tests/e2e/out/is-005/` (this issue's), git-ignored, may be gone later.
