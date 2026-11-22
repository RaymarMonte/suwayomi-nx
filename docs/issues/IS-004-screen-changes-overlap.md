# IS-004 Opening a manga shows two screens on top of each other for a moment

Kind: small change · Status: waiting on owner · Opened: 2026-11-22 · Closed: –
Outcome: –
Model: Opus · Owner: OK on the fix plan below; later, try it on the console

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

## Planned fix (waiting for the owner's OK)
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
Nothing yet (findings and plan only).

## For the owner
Say whether the cross-fade (the plan) is what you want, or an instant cut. After the fix: try Library → a manga → back on
the console.

## If it comes back
Open any screen and capture a frame within 200 ms (desktop build, or `sa.sh -o shot.jpg GET /screenshot` right after A on
the Switch). If the old screen shows at full strength behind the new one, look for a layout root without
`backgroundColor` (the check should have caught it) or a screen built in code without a background. The owner's screenshot
is in `tests/e2e/out/is-004/` (git-ignored; may be gone later).
