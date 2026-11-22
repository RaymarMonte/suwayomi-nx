# IS-003 CI red: ThreadSanitizer finds a race in the reader's offline unit tests

Kind: bug fix · Status: done · Opened: 2026-11-22 · Closed: 2026-11-22
Outcome: fixed (test-only: the 6 tests make the SD card and SafeFs before the harness; commit "IS-003: ...")
Model: Opus · Owner: –

## Report
Found by Claude at the end of WP-21 (2026-11-22) while checking CI after the push: every CI run on `main` since 5337d86
(WP-18b, 2026-11-20 15:12) failed. The owner said "Yes, fix it" when asked.

## What we checked
- 2026-11-22 ~19:40: CI's failing job is "Unit tests + coverage", step "Unit tests under ThreadSanitizer"
  (`tools/build.sh tsan`). The plain and test-hooks unit runs in the same job pass; only TSan reports. Last green run:
  ea064ab (2026-11-20 13:40); first red: 5337d86 (WP-18b's desktop work, which added the offline reader tests).
- The report (7472af1's run): a data race in `unit.ui`, test case at `tests/unit/test_ui_reader.cpp:1145` ("offline, a
  chapter the Switch knows nothing more of ..."). The main thread runs `SafeFs::~SafeFs()` at the end of the test while
  an image worker thread (`ImageLoader::readFile` → `SafeFs::readFile` → `jailPath`) still reads the same `SafeFs`.
- The tests declare `Harness h;` first, then `SdCard sd;` and `const platform::SafeFs fs(sd.root);`, then
  `h.useFiles(fs)` gives the harness's new `ImageLoader` a pointer to `fs`. C++ destroys local variables in reverse
  order, so `fs` (and the SD folder) go first while the harness's loader can still be reading a page through it.
- The app is not affected: `main()` makes its `SafeFs` (`src/app/main.cpp:308`) before the `ImageLoader` (line 461),
  so the loader is destroyed first.
- The same order is in all 7 tests that call `useFiles` (5 in `test_ui_reader.cpp`, 2 in `test_ui_strip.cpp`).

## Cause
Test-only: the tests destroy the SD-card file helper (`SafeFs`) before the harness whose image loader reads through it.
Whether the worker thread is still reading at that moment depends on timing, so it fails only now and then, and only
TSan sees it.

## What changed
- `tests/unit/test_ui_reader.cpp` (4 tests) and `tests/unit/test_ui_strip.cpp` (2 tests): `SdCard sd;` and
  `const platform::SafeFs fs(sd.root);` now come before `Harness h;`, so the harness and its loader are destroyed first.
  A note at both `useFiles` helpers says why. (The seventh call, `h2.useFiles(fs)`, already came after `fs`.)
  No app code changed; no CHANGELOG line (the owner doesn't see it); no D-entry.
- Also seen 2026-11-22: CI on 3e60f5b failed the same way in the other offline reader test (it starts at line 1065).
- Tests (2026-11-22, ~19:50): `tools/build.sh tests` green (7/7, contract 57/57); `tools/build.sh tsan` green (7/7);
  `unit.ui` under TSan repeated 10 times (`ctest --repeat until-fail:10`): 10 of 10 green. CI on the push: checked after.

## For the owner
Nothing.

## If it comes back
`wsl.exe -d Ubuntu --exec bash -lc '<repo>/tools/build.sh tsan'` (a few minutes); a report names the two threads and the
lines. On GitHub: `gh run list` then `gh run view --job <id> --log-failed` from WSL.
