# IS-004 Opening a manga shows two screens on top of each other for a moment

Kind: small change · Status: done · Opened: 2026-11-22 · Closed: 2026-11-24
Outcome: fixed (e273d01, D64); the owner's console try passed
Model: Opus · Owner: plan OK'd 2026-11-22 (cross-fade); tried Library → a manga → back on the console: looks good

## Report
The owner, 2026-11-22, on the Switch, normal build, own server: "Whenever I'm navigating to a manga from the library. The
transition is really jarring, like the 2 UIs are just printed together for a split second then the manga description UI
shows up on its own. I'm not sure if there are other instances of this jarring UI transition, if there are, I'd like to
fix those as well."

The owner caught it in a console screenshot (Library → Berserk): the whole library grid, the side menu and the library's
buttons at full strength, with the details screen's cover, title, Continue button, description and "Loading chapters..."
drawn over them. The screenshot shows the owner's library, so it stays out of the repo: `tests/e2e/out/is-004/` (git-ignored).

## What we checked
- 22:13 (UTC+08:00): every screen change in `src/` calls `brls::Application::pushActivity(...)` / `popActivity()` with
  borealis's default animation, `FADE` (none of our calls pick another one, except two pops in setup/login, also `FADE`).
- How borealis draws `FADE` (pinned borealis, `library/lib/core/application.cpp` `pushActivity` / `popActivity` / `frame`,
  `lib/core/view.cpp` `show` / `hide` / `isTranslucent`):
  - **Opening a screen:** the new screen fades in from invisible to fully visible over 200 ms (`brls/animations/show`). While
    it fades it counts as "see-through", so the screen under it is drawn too, **at full strength, not fading out**. When the
    fade ends, the new screen stops being see-through and the old one is simply not drawn any more, in one frame.
  - **Going back (B):** the screen underneath is shown at full strength at once, and the top screen fades out over it.
  - The fade only makes a screen's own drawings see-through. Our screens have **no background of their own**: the root of
    every layout (`<brls:AppletFrame>` in `resources/xml/activity/*.xml`) draws nothing, and the grey you see is the window
    being cleared each frame (`brls/clear`). So while a screen fades in, the old screen shows through all of its empty
    space, and at the end the old screen vanishes at once. That is the "printed together, then shows up on its own" look.
- The reader is different: its root view paints a solid background (black / white / grey, `ReaderActivity::applyLook`),
  so opening and leaving the reader already looks like a clean cross-fade.
- borealis's theme: `brls/background` is the same color as `brls/clear` in both the dark and the light theme, so painting
  that color behind a screen changes nothing once the fade is over.
- borealis applies the fade to everything a view draws, background and images included (`View::a()`, `Image::draw`).
- 22:26 – 22:30, the owner's OK (cross-fade). Desktop test build, test server, Reading → Alpha and B back, recorded at
  60 fps with ffmpeg on the hidden X screen (a throwaway probe scenario, kept in `tests/e2e/out/is-004/`). Size of each
  frame-to-frame change (RMSE, 0 = same, 1 = all different):
  - **Before:** opening fades over ~10 frames (steps ≤ 0.06), then **0.19** in one frame (the library vanishes). Going
    back starts with **0.19** (the library appears at full strength under the details), then fades. Same look as the
    owner's console screenshot.
  - **After the fix:** both ways the biggest step is **0.06** (the first frame of the fade); the library fades out while
    the details fade in. No pop.
  - A step of ~0.035–0.05 two frames after the fade, both before and after: the bottom button hints light up again
    (borealis greys them while it blocks input during the animation). Unrelated, left as it is.
- 22:29 – 23:42, tests with the fix: unit tests, `npm test` (with the new check), typecheck green. Desktop e2e, every
  scenario in both modes (`--bail 0`): application 26 / 29 files green, applet 28 / 29. Failing: **server-downloads**
  (Resume → queue running 2012–2067 ms, limit 2000; the server's 1 s beat + the app's ~1.5 s poll, D58 / D62),
  **soak** (a LEFT press lost: its key-down came in the same millisecond as the previous key-up, so the app, which reads
  keys once per frame, saw no new press) and **updates-history** (the tap on Reading right after the Library tab tap was
  lost). Without the fix (layouts stashed, rebuilt, 23:32 – 23:42): server-downloads failed the same way (2067 ms), so
  that one is not ours; soak and updates-history passed twice.
- 23:45, desktop frame rate (a temporary log line, not committed; Xvfb, software rendering): without the fix 614–690 fps,
  with it 519–562 fps per screen (Library, Reading, details), i.e. ~1.55 → ~1.9 ms a frame: the one full-screen
  rectangle the background adds, drawn by the CPU. On the Switch the GPU fills it in a few hundredths of a millisecond.
  0.35 ms more per frame makes two inputs landing in one frame only ~20 % likelier, too little to explain the lost
  inputs.
- 23:49 – 00:08 (2026-11-23), A/B, application mode, alternating builds (with the fix, without, three rounds): soak and
  updates-history **green all 12 times** (fix 3 / 3 each, without 3 / 3 each). So the earlier soak / updates-history
  failures were the harness's input timing (desktop: `xdotool` key-up late, the next key-down or tap in the same frame),
  not the fix. Those early runs were the first ones after WSL started (22:22), one alongside the Switch build.
- 00:09 – 00:48, the whole desktop suite with the fix once more (`--bail 0`): **application mode all green** (29 files, 1
  skipped as always); applet mode 28 green, server-downloads red again (2012 ms).
- 00:49 – 00:55, A/B of server-downloads in applet mode (alternating, 3 rounds). The "queue running" time is two-valued:
  ~503 ms (the app's first check after the tap sees the server started) or ~2010–2085 ms (one 1 s poll later), by where
  the tap falls in the server's 1 s beat. Without the fix: 2083 (red), 503, 503. With it: red before Resume (the app
  stayed busy > 15 s on a request while the test source was slowed on purpose), 504, 2022 (red). Over the whole session,
  with the fix the slow case came 5 of 6 times in applet mode, without it 1 of 4, and in application mode without the fix
  it also came (2067). A 2000 ms limit sits on the slow case's edge, so this check flips on timing alone; the scenario's
  steps follow the server's beat (D62), so a fraction of a millisecond per frame on the PC can move the tap's phase. The
  app's behaviour doesn't change (it has no say in when the server starts). Left as it is here; a follow-up for the
  owner (D58's limit is the owner's call).
- 2026-11-24, the owner (in IS-008's session): tried it on the console earlier, "they both look good" (this and
  IS-006). Closed.

**Where the owner sees it** (going in, and the reverse when going back with B):
- Library → a manga's details (the report); Library → Categories, → "Move to category"
- Updates / History → a manga's details
- Browse → a source, Global search, Languages, Filters; a source or search result → a manga's details; adding a manga →
  the category picker
- Switch downloads / Server downloads → a manga's details
- A manga's details → the category picker
- Settings → Crash logs, Diagnostics; the setup and login screens

Not affected: the reader (above), the pop-up menus (they dim the screen behind on purpose), switching tabs on the main
screen (borealis doesn't animate it).

## Cause
borealis's fade only fades the new screen in; it never fades the old one out. Because our screens have no background of
their own, the old screen shows through the new one for the whole fade and then disappears in one frame.

## Planned fix (the owner chose the cross-fade, 2026-11-22; done, see What changed)
Small: about 13 one-line layout changes and one new check. It touches no threading, reader, progress sync or safety code,
so it stays an issue (not a WP).

1. **Give every full screen a solid background.** Add `backgroundColor="@theme/brls/background"` to the root
   `<brls:AppletFrame>` of all 13 layouts in `resources/xml/activity/` (categories, category_picker, crash, crash_logs,
   diagnostics, filters, global_search, languages, login, main, manga, setup, source). With a solid background the fading
   screen covers the old one more and more, so the change looks like a real cross-fade: the old screen fades out while the
   new one fades in, and nothing pops at the end. Going back works the same in reverse. Same color as now, so a finished
   screen looks exactly as before (the e2e goldens should not change).
2. **A check so a new screen can't bring it back:** `tests/contract/screen-backgrounds.test.ts` (runs with `npm test`) reads
   every `resources/xml/activity/*.xml` and fails, naming the file, if its root has no `backgroundColor`.
3. **Prove it on the PC:** before and after the change, open a manga from the library in the desktop build and capture a
   frame about 100 ms into the fade: before, the library is at full strength under the details; after, the two are blended
   and the library is fading. Keep the pictures in `tests/e2e/out/is-004/`.
4. **Tests:** unit tests (`tools/build.sh tests`), `npm test`, the desktop e2e scenarios in both modes (`tools/e2e.sh desktop`
   and `--mode applet`; every screen's layout changes, so all of them). Then a Switch build, uploaded, and the owner tries
   Library → manga → back on the console.
5. **Docs:** a CHANGELOG line under Fixed ("Opening or leaving a screen fades cleanly instead of showing two screens on top
   of each other"), and a short D-entry for the rule "every full screen paints its own background" (the check enforces it).

Other ways, not chosen:
- **No animation at all** (`TransitionAnimation::NONE` on ~30 push/pop calls, plus borealis's built-in B = back, which
  pops with `FADE`): an instant cut, also clean, but abrupt and more places to touch.
  Easy to add on top if the cross-fade still feels too soft.
- **Fade the old screen out first, then the new one in:** needs a change inside borealis (pinned submodule, D1). Not worth it.

## What changed
- `resources/xml/activity/*.xml` (13 layouts): `backgroundColor="@theme/brls/background"` on the root `brls:AppletFrame`.
- `tests/contract/screen-backgrounds.test.ts` (new, `npm test`): fails, naming the file, if a layout's root lacks it
  (checked: it fails on `source.xml` with the line taken out).
- CHANGELOG (Unreleased → Fixed), D64 (the rule).
- Tests: unit tests, `npm test`, typecheck green; desktop e2e: application mode all green, applet mode all green but
  server-downloads' 2 s Resume limit (it fails without the fix too; What we checked). Switch: `tools/build.sh switch` (gate PASS), uploaded to
  the console (SHA-256 matched). No Switch e2e run: the change is layout data only, and the console was the owner's.

## For the owner
Nothing. Tried on the console (Library → a manga → back): looks good (the owner, 2026-11-24). The server-downloads
"queue running ≤ 2 s after Resume" question went to IS-005 (the owner's call: timed on screen), and that scenario's
remaining flake has its own issue on the `claude/wizardly-engelbart-c19dea` branch.

## If it comes back
Open any screen and capture a frame within 200 ms (desktop build, or `sa.sh -o shot.jpg GET /screenshot` right after A on
the Switch). If the old screen shows at full strength behind the new one, look for a layout root without
`backgroundColor` (the check should have caught it) or a screen built in code without a background. On the PC:
`tests/e2e/out/is-004/is004-fade-probe.test.ts.txt` (copy it into `tests/e2e/scenarios/` as `.test.ts`, run it with
`SNX_PROBE_LABEL=<name> tools/e2e.sh desktop is004-fade-probe`) records the change at 60 fps; `steps.sh <frames dir> 20 34`
next to it prints each frame-to-frame change (a single step near 0.19 = the pop is back). The owner's screenshot, the
recordings (`before/`, `after/`), the test runs (`e2e-*`, `ab/`) and the frame-rate probe are in `tests/e2e/out/is-004/`
(git-ignored; may be gone later).
