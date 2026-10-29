# WP-12 MVP hardening, soak test, v0.1 release

Model: **Opus** · Size: M · Needs: WP-11 · Owner: **use the app with your real server** and report issues

## Goal
Make the MVP trustworthy: survive network loss, server restarts, sleep/resume, long sessions. Then ship v0.1.

## Read first
STATUS.md, `docs/TESTING.md` (scenarios 8–10), any open bug notes.

## Deliverables
- [x] Scenario `offline-errors`: stop the test server mid-session → errors shown → restart → retry works everywhere
      (desktop and console green 2026-10-30; found and fixed three gaps, D45)
- [ ] Sleep/resume (manual, owner presses the power button once): app reconnects, reader continues, no crash
- [x] Touch pass on the console in handheld mode across all MVP screens: automated if the touch fork exists (D19), otherwise the owner runs a 5-minute checklist
      (scenario `touch-pass`, desktop and console (handheld) green)
- [x] Scenario `soak` (device, application mode): 500 page turns across chapters + library scrolls. Assert memory within the profile's budget,
      no crash reports, no `error` events (console green twice, numbers in D45)
- [ ] Applet-mode guard on the **release** build (D21): open it from the Album → the message shows → back to hbmenu, no new crash report or fatal file
- [x] Tune `makeRuntimeProfile` budgets and disk budgets from soak data → DECISIONS.md (D45: page cache 512 → 256 MiB from the
      desktop soak, confirmed on the console)
- [ ] Crash-safety pass: long fuzz runs (≥ 10 min per target), full `crash-drills` in application mode plus `applet-guard`, `check-elf.sh` on the release build,
      zero `atmosphere/fatal_errors` files across all device runs of this WP
      (done: fuzz 600 s per target, no crash, 2026-10-30; `check-elf.sh` PASS on the release build. console `crash-drills` 10/10; `fatal_errors` empty after every run. Left: the release build from the Album)
- [x] Release build keeps the crash handler (only test hooks/drills are compiled out). Archive the matching `.elf` with the release for symbolizing user crash logs
      (`tools/release.sh` checks both and packs the .elf.gz, D45)
- [ ] Owner dogfooding against the **real** server (read-only use; note issues in STATUS). Fix the blocking ones.
      On the Switch that's `http://<server-lan-ip>:4568` (no auth so far), not the Tailscale address (D18 "Update")
- [x] App icon, name, version, author in NACP. Release build (no test hooks) (0.1.0; owner kept the author and the icon)
- [x] `README.md` user section: install, first run, controls, **safety notes** (open it by holding R while starting a game: the Album isn't supported yet, D21; FAT32 recommended,
      exit with + rather than HOME → Close while downloading, close games before opening the Album, where crash logs are). `CHANGELOG.md`
- [ ] Tag `v0.1.0`. GitHub Release with .nro + SHA-256 (if a repo exists)

## Acceptance
All layers green, soak green twice, owner happy to use it.

## Sonnet-safe chunks
- README user section, CHANGELOG, release notes.
