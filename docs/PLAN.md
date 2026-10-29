# suwayomi-nx: master plan

## Goal

A Switch homebrew app that connects to your own Suwayomi-Server over the LAN. You can browse your library,
read chapters (paged and webtoon), keep reading progress in sync with the server, and later browse
sources, manage extensions, and read offline.

## The short version

- **There's no existing Suwayomi client for Switch.** (GitHub search for "suwayomi switch" / "tachidesk switch" found nothing.)
  But there are close relatives we can learn from: a MangaDex reader built on Borealis, a Komga client,
  a local CBZ reader, and **Switchfin** (a Jellyfin client). Switchfin's architecture is almost exactly what we need. See `RESEARCH.md`.
- **Start from `xfangfang/borealis_template`** with the `xfangfang/borealis` UI library (Apache-2.0). It gives Switch-style UI,
  controller navigation, recycling lists, a software keyboard, i18n, *and a desktop build*. The desktop build is the key to cheap testing.
- **Talk to Suwayomi with GraphQL** (`POST /api/graphql`). Page images come from the URLs that `fetchChapterPages` returns.
  Notes are in `SUWAYOMI-API.md`.
- **Test at six levels.** Most tests run on the PC with no Switch attached. The real console is driven by
  **sys-autopilot** (deploy the .nro, press buttons, take screenshots, read our log files from SD). See `TESTING.md`.
- **Work is cut into ~20 work packages (WPs).** Each one fits in one session and names a model.
  Opus 5.5 is the default, because accuracy comes first. Sonnet only gets clear-path, mechanical work. See `ROADMAP.md`.

## Phases

| Phase | WPs | Outcome |
|---|---|---|
| 0. Foundations | WP-00 … WP-03b | Tools installed, a "hello" .nro deployed and screenshotted by a script, repo + CI, test server with fixture manga, E2E harness, **safety layer + crash drills** |
| 1. Core (no UI) | WP-04 … WP-07 | Models, GraphQL, HTTP, Suwayomi client with auth, image pipeline + caches, all unit/integration-tested on PC |
| 2. MVP UI | WP-08 … WP-12 | Server setup/login, library, manga details, paged reader with progress sync, hardening. **Release v0.1** |
| 3. Features | WP-22, WP-13 … WP-21 | **Portrait reader first** (WP-22, console held sideways), webtoon mode + zoom, browse/search, updates/history, extensions, downloads, offline reading, polish, releases, applet mode (nice to have) |

Phase 0 matters most. WP-00 proves the whole loop (build → deploy → launch → screenshot → read log) on *your* console
before any real app code exists. If something in the chain doesn't work (controller order, memory mode, network),
we find out on day one, not in week four.

## How multi-session work stays cheap

- `CLAUDE.md` holds the session protocol. It's auto-loaded and kept short.
- `docs/STATUS.md` is a ≤40-line handoff note, overwritten each session (not an ever-growing log).
- Each WP file lists exactly what to read, what to build, and how to prove it's done.
  A session reads `STATUS.md`, its WP file, and the headers of the modules it depends on. Nothing else.
- Module `README.md` files and headers are the contracts between modules. Sessions don't read other modules' `.cpp` files.
- Tests assert on the app's **event log** (cheap text), not on screenshots the model has to look at (expensive images).
  Screenshot comparison is done by the harness with pixel diffs. Claude only looks at an image when a test fails.
- Decisions are written down once in `DECISIONS.md`, so later sessions don't argue them again.

## Model assignment (summary, details in ROADMAP.md)

- **Opus 5.5 is the default for every WP** (22 of 25). The owner prioritises accuracy over token savings.
- **Sonnet** only where the path is clear and the result is easy to check: WP-15 (Updates/History, copies the Library
  list pattern), WP-17 (small downloads-queue screen), WP-20 (release packaging/docs), plus short chores
  (adding fixtures/test cases, re-running goldens after an intended change, CI version bumps, making an Opus-written failing test pass).
- **Haiku**: pure chores, like i18n string extraction and changelog lines.
- Inside Opus WPs, the file lists **"Sonnet-safe chunks"**. Opus may hand those to a Sonnet subagent with a tight spec
  (e.g. "write 12 more table-driven parser cases like these") and then review the result. Opus still owns correctness.
- Every Sonnet WP has "Escalate to Opus if…" triggers. If one fires, Sonnet stops and writes a note instead of guessing.
- Never Sonnet: threading, memory/image pipeline, auth tokens, the reader, on-device debugging, or any new pattern others will copy.

## Risks and things to watch (each is covered by a WP)

| Risk | Why it matters | Where handled |
|---|---|---|
| Virtual controller isn't "player 1" | sys-autopilot's Pro Controller is player 1 only when no physical controller is connected. hbmenu and the app may ignore it otherwise | **Checked in WP-00: no problem.** HOME, hbmenu and borealis react to it with the Joy-Cons attached or detached (D24). The app still reads all pads (WP-08) |
| **Library code trips the S3 safety gate** | WP-00 scan of the borealis demo: libnx's own `appletInitialize` references `apmSetPerformanceConfiguration`, and borealis's `SwitchPlatform::setBacklightBrightness` (a virtual function, so it's always linked) calls `lblSetCurrentBrightnessSetting`. A plain `nm` gate would fail every build | **Decided in WP-01 (D28, owner OK):** both get do-nothing stand-ins through `ld --wrap`, so the real functions aren't linked and the gate needs no exceptions. Neither ever runs today (`docs/safety-baseline.txt`). Built in WP-03b |
| borealis defaults that don't suit us | `InputCell`'s keyboard limit defaults to 32 characters (`maxInputLength`), too short for many server URLs. borealis's logger only flushes after each line on Windows builds, so the last lines are lost when the app is killed | WP-08 sets a longer limit for URL/user/password fields. Our logger flushes itself (WP-03/03b, S1) |
| Applet mode vs application mode | Opening hbmenu from the Album (applet mode) gives far less RAM and only 2 network sockets. It's even less if a game is suspended behind it. Holding R on a game (application mode) gives nearly full RAM. Decoded manga pages are big | **Application mode only for now** (D21, amends D12). The app detects applet mode with `appletGetAppletType()`, shows how to open it with R, and exits (WP-08). One runtime profile, `Full`, with budgets from measured memory. WP-00 measures; WP-07/08 implement; every E2E scenario runs in application mode. Applet mode is a later nice-to-have (WP-21) |
| Huge webtoon images | Long strips (e.g. 800×20000) exceed GPU texture limits and memory. They need tiling and downscaling | WP-07, WP-13 |
| Network from Switch to PC | Windows firewall. WSL2 on Windows 10 is NAT'd, so the Switch can't reach servers inside WSL | SETUP.md: test server runs on Windows Java, not in WSL |
| Sleep mode drops Wi-Fi | Breaks remote testing; the app also has to recover after a real sleep | SETUP.md (disable auto-sleep); WP-12 resume handling |
| Text entry on Switch | The software keyboard is a system applet and slow to drive remotely | Tests pre-seed config on SD; keyboard is tested once manually |
| Suwayomi API drift | GraphQL schema changes between releases (multi-user auth is new in preview) | Checked-in schema + contract tests (WP-02/04); `aboutServer` version check |
| HTTPS servers | If your server is behind a TLS reverse proxy, the devkitPro mbedTLS may lack TLS 1.3 (the mangadex-reader author hit this) | Optional, WP-19 |
| **Crashing the console (reboot screen)** | In applet mode, Atmosphère turns *any* CPU crash in our app into a system fatal. We don't run there any more (D21), but the short refusal screen does. deko3d and libnx also abort on some errors | **SAFETY.md S1–S2**: our own exception handler + `diagAbortWithResult` override → log + clean exit. Build gate bans `fatalThrow`. Crash drills in application mode (WP-03b). **S12**: the applet-mode refusal screen is kept tiny and gets its own drill (WP-08) |
| **Bricking / SD corruption** | Writes to internal storage or system settings could brick. Interrupted writes on exFAT can corrupt the SD so Atmosphère won't boot | **SAFETY.md S3–S5**: banned system-write APIs (build gate), write jail to our own folder, atomic + infrequent writes, free-space guard |
| Fatal screen during tests | Needs a physical POWER press. sys-autopilot can't reboot | Harness treats any new `atmosphere/fatal_errors` file as a hard failure, stops, and asks you |
| Touch input | Touch is our main input (D16), but stock sys-autopilot can't inject touch | Desktop: mouse + test-hook gestures. Console: our slim fork with touch (D19), otherwise a manual checklist |
| **sys-autopilot crashing the console** | Stock v1.5.0 needs ~6.7 MB of the sysmodule memory pool. There are crash reports on FW 22 (at boot, and on `/screenshot`) | D19 slim fork. SETUP.md B.2 has the install and undo steps |
| Reading away from home | There's no Tailscale client for the Switch | D18: offline reading (WP-18), a Tailscale travel router, or Funnel + HTTPS (WP-19) |

## Owner's answers (2026-10-21)

1. Console: **v1 (Erista), FW 22.5.0, Atmosphère 1.11.2, booting emuMMC** (the "E" in `22.5.0|AMS 1.11.2|E`).
   sys-autopilot isn't installed yet. Read D19 before installing it.
2. Real server: latest stable Suwayomi on an always-on laptop, `basic_auth`, HTTP on the home LAN (Tailscale for other devices). See D18.
   (2026-10-26: it now runs on an always-on Linux machine, v2.4.2366, no auth yet. Other devices use a Tailscale HTTPS address; the Switch
   uses the home-LAN address on port 4568. D18 "Update".)
3. GitHub repo: yes, **public** (D23). Licence Apache-2.0 confirmed (D8).
4. **Touch first, controller second** (D16).
5. Application mode: hold R while launching **Spelunky 2**. `override_config.ini` and `[hbloader]` were never changed, so they're at the defaults.
6. (Added by the owner) **No overclocking**: stock clocks only (D17).
7. (2026-10-22) **Application mode only.** Applet mode (Album) is a later nice-to-have. Until then the app shows how to open it with R and exits (D21).
