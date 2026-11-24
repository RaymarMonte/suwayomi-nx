# IS-007 server-downloads e2e: after "Move to top" the app stays busy for 15 s and the scenario fails

Kind: bug fix · Status: done · Opened: 2026-11-24 · Closed: 2026-11-24
Outcome: fixed (test-only): before Move to top the scenario waits until the server has sent the bottom's move out
Model: Opus · Owner: –

## Report
Not the owner's: found by IS-004 and IS-006 (desktop e2e, test server). The scenario `server-downloads` fails now and
then at `server-downloads.test.ts:311` (Server downloads tab → a row → Move to top): the move answers ok
(`server_download_action` top, ok), then `no "idle" event within 15.0 s`, with a `busy` event (`jobs: true`). IS-006 saw
it in the whole desktop suite in application mode and read the test server's log as "its downloader had started a
chapter (`Running: 1, Queued: 6`) while every test-source call was slowed". It passed alone and in applet mode there.

## What we checked
- 2026-11-24 ~07:40 (UTC+08:00), the runs kept from IS-004, IS-005, IS-006 (`tests/e2e/out/is-004/`, `is-005/`,
  `is-006/` in the main checkout; 27 server-downloads runs with events):
  - **The server's downloader was not running at that step.** The log's two `DownloadManager` lines
    (`.test-server/root/logs/application.log`, 07:30:47 `Running: 0, Queued: 1` and 07:30:49 `Running: 1, Queued: 6`) are
    the two enqueues of step 1 (the app's t 3405 and 5786), where the downloader is meant to run. Nothing after them
    until the next scenario restarted the server; the app's events show the queue `paused`, `started: false` all along.
  - **What kept the app busy:** its own "Move to top" request. `top` answered ok after 821 ms; `ServerDownloads`
    (`src/ui/downloads/ServerDownloads.hpp`) counts an action as done when a read 500 ms after it (`serverDownloadCheckMs`)
    shows it done, because the server sometimes holds a reorder's answer for 30 s (SUWAYOMI-API.md, WP-17). The request
    itself stays open, and `idle` needs no request in flight (TESTING.md). A paused queue isn't polled, so no other
    request shows up either: no `net` event for 15 s.
  - **Not tied to the whole suite or to a mode:** IS-004's A/B run `is-004/ab-server-downloads/fix-1.txt` failed the same
    way (top 821 ms, then no idle) **in applet mode with the scenario run alone**. 2 failures in 27 runs, both at `top`,
    both 821 ms. In every run `top` took 298–343 ms (or 821), never less; `bottom` and `remove` 4–11 ms (or ~320).
- ~07:50, the server's code (`javap -c` on the pinned jar, v2.4.2366, classes `graphql.mutations.DownloadMutation`
  and `manga.impl.download.DownloadManager`):
  - `reorderChapterDownload` calls `DownloadManager.reorder(chapter, to)`, then waits (30 s at most) for the first
    batch of `DownloadManager.updates` where `updates.indexOfFirst { it.chapterId == chapter } <= to`. That compares the
    chapter's place **in the batch's list of changes**, not its place in the queue.
  - A change (reorder, a page's progress, a state change) goes into a set of pending updates; the set is sent as one batch
    on a 1 s beat (`notifyFlow.sample(1.seconds)`), then cleared. Subscribing first gives the pending set at once.
  - So for "Move to top" (`to` = 0) the answer comes only from a batch where the moved chapter is first, or absent.
    When another chapter's change waits in the same set before it, every batch fails the test and, with a paused queue,
    no more batches come: the answer waits the full 30 s. "Move to bottom" (`to` = rows − 1) always passes.
- 07:58 – 08:05, a probe without the app (`tests/e2e/out/is-007/probe-top.ts`, fresh test server): the scenario's
  steps 1–3 over GraphQL (source slowed 8 s, enqueue 1 then 2–6, stop at ~8.4–9.2 s, bottom, top 0.85 s later, a
  queue read 0.5 s after the top, like the app). **The top was held 30 s and then failed ("Exception while fetching
  data") in 3 of 10 rounds**, the others answered in 3–4 ms; the read showed the new order at once every time (3 ms).
  With the server's batches logged (its subscription), the held round shows the cause: the bottom and the top move
  went out in **one** batch, `[c1:POSITION@5 c3:POSITION@0]`, so the moved chapter was second; nothing came after.
  Without the logger listening it happens the same way (1 of 6).
- 08:03 – 08:09, `server-downloads` alone with the app, the batch logger listening (`is-007/alone-*`; round 1 didn't
  build: this worktree had no submodules yet, so rounds 2 and 3 used the shared desktop-test binary, whose only change
  was another session's reader drawing). Both green. The bottom's move went out ~0.5 s after its tap, the top's in the
  next batch, alone. **In the app a top that isn't held takes ~300 ms, not ~3 ms**: when the menu closes the tab reads
  the queue again at the same moment, and both requests take ~300 ms (a `net` event at +295 ms, the top's answer 16 ms
  later). Not this issue; it is also why a held top shows 821 ms (the check read at 500 ms + ~320 ms).
- The gap between the bottom's and the top's tap in all 27 kept runs: **854–962 ms, always under the server's 1 s
  beat**; the two failures had the shortest (854 ms). Whether a beat falls between the two moves then depends on where
  the beat happens to be, and on whether the top's request or the tab's read gets the slower ~300 ms connection. So it
  is timing alone, in either mode, alone or in the whole suite; the other scenarios don't cause it.
- 08:12, the fix's idea checked with the probe (`WAIT_NEWS=1`, `probe-3-wait-news.txt`): after the bottom, wait for the
  batch with its move, then send the top **at once** (0.02–1.0 s after the bottom): **10 of 10 tops answered in
  2–3 ms**.
- 08:27 – 08:33, the verification, first try (`is-007/try1/`, a build of this worktree in its own folder
  `~/build/suwayomi-nx-is007`: the shared `~/build/suwayomi-nx/desktop-test` is configured for the main checkout's
  sources, see "For the owner"): **red in `reader-webtoon`**, not this issue's scenario (`reader-webtoon.test.ts:550`,
  the strip's settings → Left to right; no `reader` paged within 30 s). Just before, the app's main thread spent 329 ms
  on a config save (`config_saved` `ms` 329); a tap's press and release then came in one frame, the next taps hit the
  wrong place and a menu stayed open: the lost-input kind IS-005 fixed elsewhere. Left as it is (a follow-up); the
  count of three green suites started again.
- 08:33 – 08:35, second try (`is-007/try2/`): **red in `reader-webtoon` again**, at another strip-settings step (no
  `reader` strip full within 30 s). Against a green run (IS-006's): the entry tap opened the choices, then the choice
  tap brought nothing. `stripSetting` (`reader-webtoon.test.ts:189`) still waits with a plain
  `waitForEvent('idle', {}, { from: mark })` after each tap, which an `idle` from before the tap can satisfy (IS-005's
  `waitForIdleAfter` was made for that), so likely the same race, more often with this cold build folder. Not this
  issue's change (test-only, server-downloads). From here the suites run with `--bail 0`, so every scenario runs.
- 08:36 – 09:44, the verification (`is-007/verify.sh`, outputs `verify-*`): three whole desktop suites in application
  mode, a fresh test server each, then `server-downloads` alone in applet mode. **`server-downloads` green all four
  times** (83–87 s in the suites, 84.8 s in applet mode), and its Move to top answered in **304–323 ms every time**, so
  never held (a held one shows ≥ 500 ms). Suite 2 was green all through (39 passed, 2 skipped). Suites 1 and 3 each
  had one other scenario red, neither this issue's:
  - suite 1, `switch-downloads.test.ts:334`: the golden `chapters-on-switch.png` was off by 1005 pixels (0.109 %,
    0.1 % allowed). Green in suites 2 and 3.
  - suite 3, `touch-pass.test.ts:54`: after the tap on Sort, the scenario's plain `waitForEvent('idle', {}, { from:
    mark })` (line 52) took an `idle` from before the menu opened (t 3201; the menu's choice got focus at 3322), so the
    tap on "oldest" likely came while the menu was still coming in: the menu closed (focus back on Sort at 3329) with
    no sort change. The same stale-idle kind as `reader-webtoon`'s `stripSetting` above. Green in suites 1 and 2.
  So the whole suite wasn't green three times in a row, because of these two (and the two `reader-webtoon` tries).
  They are left for follow-ups, see "For the owner".

## Cause
A server quirk, not the app: the test server (Suwayomi v2.4.2366) answers "Move to top" only from an update batch
whose first change is the moved chapter. The scenario taps Move to bottom and then Move to top less than 1 s apart, so
now and then both moves go out in one batch on the server's 1 s beat, the top's second; with the queue paused nothing
else comes, and the server holds the answer 30 s, then fails it. The app shows the move after its 0.5 s check (as
designed, WP-17), but its request stays open, so it isn't `idle`, and the scenario's 15 s wait for `idle` ran out. The
downloader was never running at that step (the `Running: 1` log line is step 1's enqueue). The app's behaviour for a
user is fine: the screen is right after 0.5 s, other actions aren't blocked, and the next queue change releases the
held answer (a batch that doesn't name the chapter passes the server's check).

## What changed
- `tests/e2e/lib/server-downloads.ts`: `ServerQueueNews`, the server's own queue news (its GraphQL subscription
  `downloadStatusChanged` over a WebSocket, what the web UI listens to): `open()`, `waitFor(what, chapterId, type,
  since)`, `close()`. Auth none only (the e2e test server).
- `tests/e2e/scenarios/server-downloads.test.ts` step 3: after Move to bottom, wait until the server has sent Chapter
  1's move out (≤ ~1 s), then Move to top. No limit changed (D58 / D62 as they were). No app code changed.
- Docs: SUWAYOMI-API.md (Reorder: why it holds), TESTING.md (4e). No D-entry (no new rule), no CHANGELOG (nothing the
  owner sees).
- Tests: `npm run typecheck` and `npm test` green; `server-downloads` green in three whole desktop suites (application
  mode) and alone in applet mode (see the 08:36 – 09:44 run above). Unit tests not run: no C++ changed.

## For the owner
Nothing for this issue. Three things found on the way, left as they are (not this issue's):
- **Two scenarios still wait the old way**: `reader-webtoon` (`stripSetting`, line 189) and `touch-pass` (line 52)
  wait for a plain `idle` after a tap, which an `idle` from before the tap can satisfy; IS-005's `waitForIdleAfter` is
  made for that. In these five suites `reader-webtoon` was red twice (once likely this race, once a tap lost after a
  329 ms config save) and `touch-pass` once. A follow-up issue.
- **A golden just past its limit now and then**: `switch-downloads`' `chapters-on-switch.png` (0.109 %, 0.1 %
  allowed; once in three suites).
- **A build-folder pitfall for parallel sessions**: `tools/build.sh` sets up a build folder only when it is new, and
  `~/build/suwayomi-nx/desktop-test` is set up for the main checkout's source. So `tools/e2e.sh` run from a worktree
  builds and tests the main checkout's files, not the worktree's. This issue used its own folder
  (`SNX_BUILD_ROOT=$HOME/build/suwayomi-nx-is007`, configured from the worktree; it can be deleted).

## If it comes back
1. `tests/e2e/out/server-downloads/events.jsonl`: a `server_download_action` `top` with `ms` ≥ 500 (answered by the
   app's check read) means the server held the move; a `busy` with `jobs: true` after it is that request.
2. Then run `tests/e2e/out/is-007/batch-logger.ts` next to the scenario (`alone-with-logger.sh` does both): the batch
   that carries the top's `POSITION@0` should be alone. If it shares a batch with another chapter's change although
   the scenario waited, something else changed the queue in between (a late `PROGRESS` of the stopped chapter, say).
3. A new server version: `javap -c` on `DownloadMutation$reorderChapterDownload$1$1$1` shows whether the check
   (`indexOfFirst { chapterId } <= to` on the batch) is still there; the probe (`probe-top.ts`, without `WAIT_NEWS`)
   shows whether tops are still held (3 of 10 here).
Runs and scripts: `tests/e2e/out/is-007/` (git-ignored, may be gone later).
