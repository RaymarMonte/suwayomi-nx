# Decisions log

Short records so later sessions don't re-argue settled questions. Change one only with the owner's OK.
Format: ID, decision, why, alternatives rejected.

**D1 UI framework: `xfangfang/borealis`** (Apache-2.0, maintained fork of natinusala/borealis).
Why: Switch-native look, automatic controller navigation, recycling lists, swkbd, i18n, storage helpers,
and a desktop build (PLATFORM_DESKTOP) so the whole UI can be tested on the PC. Used by Switchfin, wiliwili, Moonlight-Switch.
Rejected: Plutonium (Switch-only, no desktop build), raw SDL2 (we'd rebuild navigation/lists ourselves),
Dear ImGui (not controller-first), natinusala/borealis upstream (less maintained; mangadex-reader had to patch two bugs).

**D2 Build system: CMake**, from `xfangfang/borealis_template`. Targets: `PLATFORM_DESKTOP` (Linux in WSL) and `PLATFORM_SWITCH`.
Switch graphics backend: deko3d (`-DUSE_DEKO3D=ON`). Fall back to OpenGL only if deko3d causes a blocking bug.

**D3 Language: C++17**, exceptions and RTTI on (borealis requires it).

**D4 Server API: GraphQL** at `/api/graphql` for everything except image bytes. Image bytes come from the page URLs that
`fetchChapterPages` returns, plus thumbnail URLs. JSON: nlohmann/json. Query documents live as `.graphql` files and are
embedded at build time, so contract tests can validate the same files against the schema.
Rejected: REST v1 (legacy), OPDS-PSE (read-only, misses library and progress features).

**D5 HTTP: libcurl** (devkitPro `switch-curl`, system curl on desktop) behind our own `HttpClient` interface.
One network thread drives a curl multi handle (applet mode only has 2 socket sessions). Concurrency comes from the runtime profile (D12),
with request priorities (visible covers > current reader pages > prefetch).

**D6 Images: decode JPEG/PNG/WebP on the device** (stb_image or libjpeg-turbo, plus libwebp), downscaled at decode time to screen size,
with tall images split into tiles. `?format=image/jpeg` server-side conversion is an optional setting, off by default.

**D7 Tests: doctest** for C++ (single header, fast), **Node + TypeScript + vitest** for the E2E harness and contract tests
(Node and pnpm are already installed; sys-autopilot's own tooling is pnpm-based). Python isn't installed and isn't needed.

**D8 Licence: Apache-2.0 (confirmed by the owner 2026-10-21).** It's compatible with borealis and Switchfin.
A GitHub repo will be created (it enables CI). It's public (owner, 2026-10-25, D23).
Consequence: never copy code from GPL projects (wiliwili, komga-switch). Reading them for ideas is fine.

**D9 Supported Suwayomi: the stable release current when the schema is dumped in WP-02** (v2.4.2366 as of 2026-09-30) and newer.
The app checks `aboutServer` and shows a warning on older servers. Preview-only features (multi-user `ui_login`) are supported
but optional.

**D10 On-SD layout:** `sdmc:/switch/suwayomi-nx/` → `suwayomi-nx.nro`, `config.json`, `cache/`, `logs/`, `test/` (test builds only).
Desktop uses the same layout under `./data/`.

**D12 Two runtime profiles, chosen at startup: `Full` (application mode) and `Lite` (applet mode).**
**Amended by D21 (2026-10-22): only `Full` is used for now, and the app refuses to run in applet mode. The `Lite` parts below wait for WP-21.**
**Amended 2026-10-25 (WP-00, owner OK): the memory inputs.** `svcGetInfo(UsedMemorySize)` can't be used for budgets: libnx reserves the
whole heap at start-up, so it already reads 3,185 of 3,189 MiB before the app allocates anything (measured, application mode). So
`totalMem` = the heap libnx got (`fake_heap_end - fake_heap_start`: 3,184 MiB in application mode on the owner's v1 console) and
`usedMem` = the bytes our allocator has handed out (`mallinfo().uordblks`). Desktop: `--mem-limit=<MB>` sets `totalMem`.
Detection: `appletGetAppletType()`. `AppletType_Application` or `AppletType_SystemApplication` → Full, anything else → Lite.
This is the same check hbmenu and borealis's `isApplicationMode()` use. Budgets aren't hard-coded per mode. They come from
a pure function `makeRuntimeProfile(mode, totalMem, usedMem)` in `core`, fed by the heap size and the allocator's in-use bytes (see the
2026-10-25 amendment above), because applet-mode memory shrinks further when a game is suspended in the background. Lite never blocks the user (unlike Switchfin's hint
screen). It shows a dismissible tip explaining how to launch in full mode. Desktop builds pick a profile with `--profile=lite|full`
(default full) and can cap memory with `--mem-limit=<MB>`, so both profiles are tested on the PC too.
Rejected: refusing to run in applet mode (unfriendly), one-size-fits-all budgets (either wastes full mode or crashes lite mode).

**D13 Crash containment is mandatory** (SAFETY.md S1/S2): our own `__libnx_exception_handler` (with `__nx_exception_stack`),
`diagAbortWithResult` override and `std::set_terminate`, all ending in a logged, clean `exit()`. `fatalThrow*` is banned by a build gate.
Why: Atmosphère's creport turns crashes of non-application processes (our app in applet mode) into a system fatal, and deko3d routes
GPU failures through `diagAbortWithResult` even in release. Pattern follows nxdumptool/Goldleaf (GPL, so we reimplement, not copy).
**Amended by D35** (owner OK 2026-10-26): on the Switch a crash ends with `svcExitProcess`, not `exit()`.

**D14 The app never writes outside `sdmc:/switch/suwayomi-nx/`** and never calls system-writing APIs (SAFETY.md S3/S4 list), enforced
by `SafeFs` plus an ELF symbol gate. Any exception needs the owner's OK. Why: that's the only realistic way a userland reader app could brick
or soft-brick a console.

**D15 No in-app self-update in the MVP** (SAFETY.md S11). Updates are a manual `.nro` copy. Revisit after v0.1 with the safeguards listed.

**D11 Development environment: WSL2 Ubuntu 24.04** for host toolchain, devkitPro, Xvfb/WSLg, and Node harness.
**Suwayomi *test* server runs on Windows** (Java 21 is installed) on port **4568**, so the Switch can reach it through the LAN.
It's kept separate from the owner's real server so tests never touch the real library.

**D16 Touch first, controller second** (owner, 2026-10-21). Handheld touch is the main way to use the app. Every screen must work fully
by touch from the first UI WP (WP-08) on. Touch isn't a WP-19 polish item any more. Controllers still work everywhere, because docked mode has no
touchscreen and borealis gives controller navigation for free.
Basics: touch targets big enough for a thumb (aim for ≥ 72 px at 1280×720), tap to open, drag/flick to scroll lists. Reader: tap the left/right
edge for previous/next page (mirrored for right-to-left), tap the centre for the menu, swipe to turn pages. Pinch or double-tap zoom and drag-to-pan come in WP-13.
Testing: the desktop build uses the mouse as one finger. Multi-finger gestures (pinch) go through a test hook that injects touch events into the app.
On the console, automated touch comes from our own sys-autopilot build (D19, built in WP-00b). Until it works, each UI WP has a short manual touch checklist.
Rejected: controller-first with touch added in WP-19 (the original plan).

**D17 Stock clocks, no overclocking** (owner, 2026-10-21). The app must run smoothly at Nintendo's stock clocks on the oldest model (v1 Erista):
CPU 1020 MHz, GPU 307–384 MHz handheld / 768 MHz docked. The app never changes clocks (not even the official CPU boost mode) and never depends on
sys-clk. All performance checks (page turns, list scrolling, decode time) are measured at stock clocks.
Why: most users don't overclock, and overclocking costs battery and heat. Consequences: decode stays off the main thread, images are downscaled
at decode time (D6), WP-11/WP-13 measure frame time, and WP-03b adds the clock-setting APIs to the banned-symbol gate.

**D18 The owner's real server and network** (owner, 2026-10-21). Latest stable Suwayomi-Server on the owner's always-on laptop.
Auth: `basic_auth` (recommended: it's the simplest, every client supports it, and the app supports all modes anyway). Plain HTTP on the home Wi-Fi.
Anyone on the same Wi-Fi could read a Basic-auth password sent over HTTP, so use one that isn't used anywhere else.
Tailscale: there's no Tailscale client for the Switch, so the Switch reaches the server only on the home network. Away from home, the options are,
in order: offline reading (WP-18, download at home and read anywhere); a travel router with Tailscale built in that lets devices on its Wi-Fi reach
the tailnet (no app change, but check the router supports it); Tailscale Funnel (a public HTTPS URL, which needs our HTTPS support from WP-19 and a strong password).
Consequences: the MVP is HTTP + basic auth on the LAN. HTTPS stays optional (WP-19). Offline reading matters more. Tests still never touch this server (D11).
**Update 2026-10-26 (owner):** the real server is running. Checked from the PC that day: Suwayomi-Server **v2.4.2366 Stable** (D9's version) on an
always-on **Linux machine** at home (not the laptop). The owner's address for it is `https://<server-tailnet-name>:4567` (web UI under `/library`):
Tailscale Serve in front of Suwayomi, a Let's Encrypt certificate (ECDSA, chain YE2 → Root YE → ISRG Root X2, renewed by Tailscale every
~90 days), TLS 1.2 works. Auth mode right now: **none** (only the owner's tailnet devices reach that address).
- **The Switch can't use that address** (no Tailscale client, see above). Later that day the owner opened a home-LAN path:
  **`http://<server-lan-ip>:4568`** (plain HTTP; checked from the PC: v2.4.2366, library and web UI answer). That's the Switch's address.
  Port 4567 stays closed on the LAN. The test server's 4568 is on the PC, another machine, so they don't clash; tests only use the PC (D11).
- Auth is still **none** on both addresses, so anyone on the home Wi-Fi can use the server fully. `basic_auth` stays the recommendation;
  turning it on is the owner's call. Until then the app's setup uses "no login" for this server.
- The real name and LAN IP stay out of the public repo (D23): they're in the git-ignored `docs/local/real-server.md` on the owner's PC.

**D19 sys-autopilot: use our own slim fork that can inject touch** (owner OK 2026-10-21, after comparing it with "try stock 1.5.0 first").
It's called **sys-autopilot-nx** and lives in this repo at `tools/sys-autopilot-nx/`: upstream's code copied in at a pinned commit (not a git submodule),
so there's only one repo to manage. It's built in WP-00b. We maintain it ourselves, so a future firmware that breaks it means we rebuild or fix it.
Why: (1) Stock v1.5.0 needs about 6.7 MB of the small memory pool Atmosphère keeps for background system modules. On FW 22, with other
sysmodules installed, that has crashed `am` at boot (upstream PR #30, tested on a v1 Erista like the owner's). Open upstream issues also report
a `/screenshot` crash on FW 22.1.0 + AMS 1.11.2 emuMMC (#28) and a boot crash on FW 17 (#27). (2) Stock includes endpoints we never want on a
test console: title install (to internal NAND too), DNS changes, airplane mode, power, system settings. (3) It can't inject touch, and touch is our main input (D16).
Plan: fork TooTallNate/sys-autopilot (MIT, declared in its package.json) at a pinned commit. Shrink its heap and buffers (the idea from PR #30,
reviewed by us, not merged blind). Compile out the installer, settings, DNS, power and titles code. Add `POST /input/touch` (tap or hold at x,y)
and `POST /input/swipe` (x1,y1 → x2,y2 over N ms) plus matching MCP tools, using libnx `hiddbgSetTouchScreenAutoPilotState` /
`hiddbgUnsetTouchScreenAutoPilotState`. We write that code ourselves from the libnx header. sys-botbase does the same thing but it's GPL, so we only read it.
Keep the same title ID `4200000000004150`, so the fork replaces stock cleanly. Host tests for the new argument parsing, following upstream's `tests/run.sh`.
The two touch endpoints join the SAFETY.md S10 allowlist.
Rejected: stock v1.5.0 as-is (crash reports on FW 22). Waiting for upstream to merge PR #30 (it's open and unreviewed).

**D20 Node runs inside WSL (Node 22 LTS via nvm)** (WP-00a, 2026-10-21; SETUP.md left this to WP-00a). The E2E harness, contract tests and fixture
generator use Node installed in WSL Ubuntu with nvm, not Windows Node. Today: nvm v0.40.8 + Node v22.23.4.
Why: the desktop driver starts the Linux build of the app and drives it with xdotool/Xvfb, and the test server, fixtures and build dirs all live in WSL.
Windows Node would cross the WSL boundary on every step and need path translation. nvm needs no sudo and pins the version per user.
Upstream sys-autopilot's `tests/run.sh` also runs a Node test when `node` is on the path.
Rejected: Windows Node (boundary crossing, path translation), Ubuntu's apt `nodejs` (v18, too old), NodeSource apt repo (another root apt repo, sudo for every upgrade).

**D21 Application mode only. The app refuses to run in applet mode** (owner OK 2026-10-22). Amends D12. Owner: "Making the app available in
applet mode seems to make things complex. Let's leave it off for now and make it nice to have."
- The app supports only application mode: hold R while starting an installed game → hbmenu → Suwayomi NX (or an hbmenu forwarder).
  All tuning, budgets and device tests use this mode. Applet mode (from the Album) becomes a later nice-to-have, WP-21 in ROADMAP.md.
- **Applet-mode guard.** Right after the logger and the crash handler's set-up, `main()` asks `platform::runtimeInfo()` for the mode.
  On Switch that's `appletGetAppletType()`: `AppletType_Application` or `AppletType_SystemApplication` → run normally (the same check as hbmenu and borealis). Anything else, unknown values
  included → applet mode: log one line, emit a `launch_refused` test event, show one short screen, then return from `main()` normally, which goes
  back to hbmenu. Screen text: "Suwayomi NX can't run from the Album yet. It needs the full memory a game gets. To open it: on the HOME Menu, hold R
  and start any game. Keep holding R until the Homebrew Menu appears, then choose Suwayomi NX. Press any button or tap the screen to close."
  (Text approved by the owner 2026-10-25.) It closes on any button, a tap, or by itself after 60 s. Built in WP-08.
- **The guard must be crash-safe**, because in applet mode any crash is a system fatal (SAFETY.md). It runs before borealis, the GPU, network,
  images and config. It draws with libnx's plain text console, writes its log line before showing anything, and if any step fails it just exits.
  Our crash handler (D13) is active from the first instruction. Rules: SAFETY.md S12. **Amended by D41:** in applet mode a
  crash still ends as a system fatal (the HOME menu stops the system when the Album's process ends abnormally).
- **`RuntimeProfile` stays.** Keep the struct, its `mode` field and `makeRuntimeProfile(mode, totalMem, usedMem)`, but build and use only `Full`.
  Every module still reads its limits from the profile, never from constants: budgets still depend on measured memory and on safe mode
  (after a crash: memory budgets halved, prefetch off; this replaces "Lite budgets" in SAFETY.md S1). So applet support can come back later
  as one more profile, not a rewrite. ARCHITECTURE.md keeps the `Lite` column as a reference for WP-21.
- Desktop: `--profile=lite|full` goes away. `--mem-limit=<MB>` stays (low-memory tests). New test flag `--simulate-applet-mode` makes the desktop
  build act as if it was started from the Album, so the guard is tested on the PC.
- Tests: every scenario runs in application mode only. Applet mode is used only by the `applet-guard` scenario (desktop flag, and the console
  via the Album), which includes one crash drill inside the guard screen, because that's the one place where a crash would still be a system fatal.
Why: applet mode has far less memory (even less with a game suspended), only 2 network sockets, and turns any crash into a system fatal.
Supporting it doubles the device test matrix and the budget tuning. Application mode costs the user one extra button (hold R).
Rejected: keeping `Lite` as D12 planned (too complex for now, owner). Running in applet mode with just a warning (users would hit the low-memory and
system-fatal risks we no longer test). Drawing the refusal screen with borealis (GPU setup, fonts and much more code running in the one mode where
a crash is a system fatal). The system error applet (it may add an entry to the console's Error History, a system write that D14 rules out).
D12 had rejected "refusing to run in applet mode" as unfriendly. The owner accepts that for now, and the screen says exactly how to open the app.

**D22 How sys-autopilot-nx is built and used** (WP-00b, 2026-10-25; carries out D19). Details and numbers in `tools/sys-autopilot-nx/README.md`.
- Removed for good: title installer, settings (incl. volume/brightness/clock), airplane mode, DNS, title list, power actions, OAuth. `build.sh`
  fails the build if any of those system functions (or `fatalThrow`) gets linked in. NPDM services cut from 17 to 8.
- Memory: inner heap 640 KiB (stock 4 MiB; upstream PR #30 tried 512 KiB, we keep a margin for the worst-case `read_file`). The 512 KiB
  screenshot buffer stays at libnx's documented size. One shared 512-token JSON parser for REST, one for MCP. Total 1.56 MiB vs 6.69 MiB stock.
  `/status` reports the heap (size/peak/used); heap trimming is off so `peak` is a true high-water mark.
- Safer than upstream: no token in `config.ini` = every request refused (upstream was then open to the whole Wi-Fi). Only sm, fs, hid:dbg and
  bsd may abort at start-up. caps:sc missing = no screenshots (503). PSC (sleep) registration failing = no sockets and a clean exit, because
  sockets without sleep handling crash the console at the next sleep.
- Touch API: `POST /input/touch` and `/input/swipe` plus MCP `touch`/`swipe`. Plain JSON integers only, on-screen coordinates only
  (0–1279 × 0–719), else HTTP 400. Durations ≥ 1 ms, capped at 10 s. Every call ends by lifting the finger, waiting 50 ms and handing the
  panel back (`hiddbgUnsetTouchScreenAutoPilotState`); `/input/clear` and sleep also hand it back.
- Use: the PC talks to it by IP (`switch-dev.local` doesn't resolve inside WSL), token from `tests/e2e/device/.env` via
  `tools/sys-autopilot-nx/scripts/sa.sh`. `log = false` normally (each request would otherwise write to the SD card).
- Updating the sysmodule itself goes through the SD card (Hekate USB mount, SETUP.md B.2), never through its own `PUT /files`: that writes in
  place and deletes the file when an upload breaks off, which could leave the console without a working sysmodule until the card is fixed on the PC.
Rejected: PR #30's numbers taken as-is (we measured each buffer instead). Shrinking the screenshot buffer (saves a few hundred KiB at most; screenshots were ~190 KB, and a too-small buffer makes captures fail).
Keeping upstream's open-without-token behaviour. Self-update over `/files`.

**D23 The GitHub repo is public** (owner, 2026-10-25; answers the question D8 left to WP-01). Licence Apache-2.0 (D8), so anyone can read, build
and reuse the code. Public repos also get free GitHub Actions minutes, which suits the CI plan.
Consequences for WP-01, before the first push:
- Check every tracked file for things that shouldn't be public. The bearer token is safe: it lives only in the git-ignored
  `tests/e2e/device/.env` (WP-00b checked that no tracked file contains it). Low-risk but personal details are in the docs today: the
  Switch's MAC address, home LAN addresses, the router model and Windows paths with the user name. WP-01 asks the owner whether to keep or blank them.
- Commits use the owner's GitHub "noreply" email address, so their real email address isn't published in the git history.
- Never commit backups, SD card dumps, crash reports from the console, or anything from `C:\Users\JohnDoe\SwitchBackup\`.
- **Answered 2026-10-25 (owner, WP-01):** the MAC address and router model are gone from the docs, the Switch's LAN IP is written
  `<switch-ip>` (the real one stays in the git-ignored `.env`), and the Windows/WSL user name in paths is written `JohnDoe`/`johndoe`.
  Commits use `29355973+RaymarMonte@users.noreply.github.com`, set in this repo's own git config only. Repo: `RaymarMonte/suwayomi-nx`.

**D24 How the device harness launches and drives the app** (WP-00, 2026-10-25). Calibrated steps: `tests/e2e/device/profile.json`.
- **Launch with hbmenu's network loader**: press Y in hbmenu, then `nxlink -a <ip> -p <path under sdmc:/switch/> --args "<args>" <nro>` from WSL.
  It doesn't depend on where the entry sits in hbmenu's list, and sends 4.5 MB in about 1 s. `-p` is relative to `sdmc:/switch/`. hbmenu writes
  the file there in place, then starts it. That write doesn't go through sys-autopilot's allowlist (S10), so the harness only ever passes
  `-p suwayomi-nx/…` (our folder). `PUT /files` + `/files/hash` stays the way to put and verify other files (test config) on the card.
- **Application mode** = HOME → first tile (the app-mode title, Spelunky 2: launching it once puts it first) → hold R + A → A on the user picker
  → hbmenu → release R. hbmenu shows a red "Applet Mode" badge when opened from the Album and nothing in application mode, and the harness checks it.
- **Every press that could start something waits for a screen check first.** The screenshot region is compared with a reference crop
  (ImageMagick RMSE < 0.08; same screen measured 0.000–0.009, other screens 0.12–0.39). No fixed sleeps before such presses: on the wrong
  screen an A starts a game or another homebrew. The crops are console-specific and git-ignored (`tests/e2e/device/local/`, D23).
- Logs come back from the SD card through `/files` (nxlink's `-s` stdio server can't reach WSL through Windows 10's NAT).
- The virtual controller drives HOME, hbmenu and borealis with the Joy-Cons attached or detached, so device runs leave them on.
- Screenshots during an app switch fail (HTTP 500, `capture failed`, rc `0xe0ce`). The harness retries and accepts only HTTP 200 + JPEG data.
Rejected: picking the entry in hbmenu's list as the main way (its position depends on what else is on the card; kept as a fallback).
An hbmenu forwarder (installing a title, ruled out by D14/D19). LEFT × N to reach the first HOME tile (the row wraps around).

**D25 Repo layout and build setup** (WP-01, 2026-10-25). Carries out D1–D3 and D7.
- borealis is a git submodule at `library/borealis`, **pinned to `5f08b286f3df737f3321d2247a6fe633fcead03c`** (xfangfang/borealis main,
  2026-04-25, the commit WP-00 tested). Of its own submodules only `library/lib/extern/glfw` is needed (GLFW on desktop and Switch); SDL isn't.
  Moving the pin is a deliberate change: build desktop + Switch, run the device check, record the new commit here.
- One CMake project, three builds (`tools/build.sh tests|desktop|switch`). Without `PLATFORM_DESKTOP`/`PLATFORM_SWITCH` it builds only our
  module libraries and the unit tests, without borealis, so unit tests build in seconds and CI's test job needs no submodules.
- Modules are static libraries `snx_core`, `snx_net`, `snx_api`, `snx_image`, `snx_platform`. Headers sit next to their `.cpp`
  (`#include "core/Version.hpp"`, include root `src/`). `src/ui/` is compiled into the app target, because only the app links borealis.
- Runtime resources are put together at build time: our `resources/` plus borealis's icon font, system images, `hints.json` texts and
  (desktop only) fonts, taken from the submodule. So borealis's fonts are never copied into our repo. deko3d shaders are compiled into the
  build folder, not the source tree.
- Every Switch build keeps its `.elf` (debug info) and `.nro` in `~/build/suwayomi-nx/archive/<GNU build id>/` (newest 20), so a crash log
  that names the build id can be symbolized later.
Rejected: one CMake target per test module (slower links, no gain at this size), copying borealis in (a submodule keeps upstream's history
and licence clear), building tests against the desktop app build (needs all of borealis for every unit-test run).

**D26 Two copies of nlohmann/json, kept apart** (WP-01, 2026-10-25). We vendor 3.12.0 (`third_party/nlohmann/`, D4). borealis bundles
3.11.2 and puts it on the include path of everything that links it. Our app target puts `third_party/` first and treats borealis's headers
as system headers, so our code always gets 3.12.0. borealis uses its copy only inside its own `.cpp` files (no borealis header exposes json
types), and the two versions live in different inline namespaces (`json_abi_v3_11_2` / `json_abi_v3_12_0`), so they can't mix at link time.
Rule: never pass nlohmann types to or from borealis. Rejected: using borealis's 3.11.2 everywhere (the unit tests would then need the
borealis submodule, and we'd be stuck on its version).

**D27 CI on GitHub Actions** (WP-01, 2026-10-25). `.github/workflows/ci.yml`, three jobs on every push to `main` and every PR:
`host` (ubuntu-24.04: unit tests, test-hooks unit tests, coverage report as an artifact), `desktop` (build + `tools/desktop-smoke.sh` on Xvfb,
screenshot as an artifact; it installs `dbus-x11` because borealis aborts without a D-Bus session bus), `switch` (`devkitpro/devkita64` image
pinned by digest, tag 20260219, used as it is; artifacts `.nro`, `.elf`, safety report). Later WPs add contract, integration and desktop E2E jobs.
The image has every package we need with the PC's versions (libnx 4.12.0, deko3d 0.5.0, the same portlibs), only an older compiler than the
PC's devkitA64 r30 / gcc 16.1, so CI also proves the code builds with one compiler release back. When devkitPro publishes a newer image,
bumping the digest is a chore (ROADMAP "CI version bumps").
Rejected: `dkp-pacman -Syu` in CI (tried in the first run: devkitPro's server answers CI machines with HTTP 403 and asks CI to use the
images), the `latest` tag (moves without notice).

**D28 Banned functions that libraries pull in get do-nothing stand-ins** (owner OK 2026-10-25, WP-01; built in WP-03b). The WP-01 scan
(`docs/safety-baseline.txt`) finds two S3 functions in the plain Hello app, neither of which ever runs: libnx's `appletInitialize` calls
`apmSetPerformanceConfiguration` only for a non-zero entry of `__nx_applet_PerformanceConfiguration` (default all zeros), and borealis's
virtual `SwitchPlatform::setBacklightBrightness` calls `lblSetCurrentBrightnessSetting` but nothing calls it. WP-03b links the app with
`-Wl,--wrap=<function>` for each of them and defines `__wrap_<function>` as a tiny stand-in of ours that does nothing and returns success
(0), so libnx's start-up never sees an error. The real functions then aren't linked at all, and the S3 gate stays strict with no
exceptions in `allowed-symbols.txt`. A banned function that a library update pulls in later gets the same treatment, with the owner's OK.
(D17's `appletSetCpuBoostMode` can't: libnx calls it from inside its own object file, which `--wrap` never reaches. It gets D34's
`only-from` rule instead, owner OK 2026-10-26.)
Rejected: allowlist entries with caller checks (more rules to keep right, and the functions stay in the binary), patching borealis (we'd
maintain a fork, and libnx's call would still need one of the other options).

**D29 The Suwayomi test server: pinned jar, Windows Java, files in `<repo>/.test-server/`** (WP-02, 2026-10-25). Carries out D9 and D11.
- Suwayomi-Server **v2.4.2366** (the stable release when the schema was dumped, D9), pinned in `tools/test-server/server.json` with its
  SHA-256. Bumping it = new version/URL/hash, then re-dump the schema and re-record the responses (`tools/test-server/README.md`).
- `tools/test-server/test-server.sh` (WSL) is the entry point: fixtures, library set-up (`seed.ts`), auth modes. `test-server.ps1` (Windows
  PowerShell, also usable alone) downloads the jar and starts/stops Java. Every setting is a `-Dsuwayomi.tachidesk.config.server.*`
  flag: port 4568 on all addresses, no tray/browser/WebUI/KCEF, no automatic updates or backups, `--auth none|basic|ui_login` with a
  20 s JWT. `reset` + `start` takes ~10 s.
- Files live in `<repo>/.test-server/` (git-ignored), not `%LOCALAPPDATA%`: inside Claude Code's sandbox, files that Windows programs write
  outside the project folder are invisible to WSL (found in WP-02). In the repo, WSL, Windows and the owner all see the same files.
  `reset` deletes `root\` only when it holds the `.snx-test-root` marker; `stop` only stops the java.exe whose command line names our root.
- Java is started through WMI (`Win32_Process.Create` → hidden `cmd.exe` writing the logs), not `Start-Process`: a child of the PowerShell
  that WSL called would inherit the pipe WSL reads from, and WSL would wait for Java to end.
- Network: WSL reaches Windows at its default gateway (NAT on Windows 10). The firewall needed a port rule for 4568 from LocalSubnet on
  every profile (the PC's network is Public) and the old Public Block rules for this Java switched off (SETUP.md A.4, owner).
Rejected: Docker (Rancher Desktop's daemon doesn't respond; CI can still use the image later), the server inside WSL (the Switch can't reach
it through the NAT, D11), `%LOCALAPPDATA%` (sandbox, above), allowing all of java.exe on Public networks (opens every Java port).

**D30 Node tooling: one npm package at the repo root, Node runs the `.ts` files directly** (WP-02, 2026-10-25). Carries out D7 and D20.
- `package.json` + `package-lock.json` at the repo root, exact versions: sharp 0.35.5, vitest 5.0.5, typescript 7.0.2 (type check only),
  graphql 17.0.2, @types/node 22. `npm ci` installs them. The test-server tools, the fixture generator, the schema dump and later the
  E2E harness (WP-03) and contract tests all share it, so there is one `node_modules`.
- **npm, not pnpm**: inside WSL the `pnpm` on the PATH is the Windows nvm4w shim (`/mnt/c/nvm4w/nodejs/pnpm`), which would mix Windows and
  Linux Node. npm comes with WSL's Node (D20). TESTING.md's `pnpm e2e` becomes `npm run e2e`.
- Node 22.23 strips TypeScript types itself, so tools run as `node tools/x/y.ts` with no build step and no tsx. Only syntax that can be
  erased is allowed (no enums, namespaces, constructor parameter properties): `tsconfig.json` sets `erasableSyntaxOnly`.
- CI (D27) gets a fourth job, `node`: `npm ci`, type check, `npm test` with the full fixture set generated twice.
Rejected: one package per tool (duplicated installs), pnpm via corepack in WSL (one more global shim for little gain), tsx/ts-node.

**D31 Fixture pages are drawn pixel by pixel, with a checksummed 4×4 colour code** (WP-02, 2026-10-25). Carries out TESTING.md "Fixture manga".
- `tools/gen-fixtures` draws every page into a raw RGB buffer with its own 5×7 pixel font (no system fonts, no SVG text) and only uses
  sharp to encode JPEG/PNG/WebP. Same bytes on every run (checked: identical digest on the Linux disk, the Windows disk and in the test).
  File and folder times are fixed (2026-01-01 + series days + chapter minutes), because Local Source uses the folder time as the
  chapter's upload date. The pristine set is regenerated only when the generator changes, then copied into the server's `local\`.
- The colour code (`tests/e2e/lib/pagecode.ts`): 4×4 cells of 8 pure colours (3 bits each) in a black frame with white gutters, top right
  of each page. Cells 0/3/12 are red/green/blue markers that give position, size and direction; the other 13 cells hold series (5 bits),
  chapter (10), page (8), tall-strip segment (4) and a 12-bit checksum. It's read with plain pixel maths from a 1280×720 JPEG screenshot
  of a scaled page (tested), so the harness never needs OCR or a model to know which page is on screen.
- Because Local Source copies the first page in as the cover when a folder has none, a manga without a cover can't exist. The
  "fallback art" case is the Broken series instead: its page 1 is the corrupt JPEG, so its cover is broken too.
Rejected: SVG text via sharp/librsvg (depends on installed fonts), OCR of the printed text, QR codes (need a decoder library and more pixels).

**D32 How the E2E harness is built** (WP-03, 2026-10-25). Carries out TESTING.md "Drivers"/"Golden screenshots" and D24. How to use it: `tests/e2e/README.md`.
- Node + vitest in the root package (D30): `tests/e2e/{drivers,lib,scenarios,goldens}`, own config `vitest.e2e.config.ts` (one scenario at a
  time, stop at the first failure), `tools/e2e.sh desktop|switch [scenario]` builds the test build first. A scenario only uses the `Driver`
  interface; `scenario()` always cleans up and saves the events, the app log and the last screen to `tests/e2e/out/<scenario>/`.
- Pictures: pixelmatch **7.2.0** (pinned; 8.0.0 was three weeks old) for goldens, sharp (already pinned) to decode and encode, instead of
  jpeg-js/pngjs as WP-03 listed. Masks are painted magenta into both pictures and the stored golden. Screen checks (D24) compute the same RMSE
  as ImageMagick in JS (equal to 4 decimals on WP-01's screenshots), so the device path needs no ImageMagick.
- Test events: `core/TestEvents` formats lines and decides `idle` (no running borealis ticking for 3 frames, once per quiet spell);
  `src/app/TestHooks` writes them open → append → close with a pending queue, because Horizon's file system won't open a file for writing
  while sys-autopilot reads it (an `idle` got lost that way). The Hello screen's `screen` event comes from `main.cpp` until WP-08.
- App flags and runtime info: `core::parseLaunchOptions` (never throws, unknown flags are logged), `core::RuntimeInfo`/`LaunchMode`,
  `platform::runtimeInfo()` (Switch: appletType, heap, mallinfo; desktop: PC RAM or `--mem-limit`, `--simulate-applet-mode`). Applet mode
  reports profile `none` (D21). Desktop SIGTERM/Ctrl+C = normal quit (exit code 0), which is how the desktop driver quits (no PLUS key there).
- Desktop driver: Xvfb from `:90`, xdotool; only Enter = A, Escape = B, middle mouse = X, arrows = D-pad exist on desktop borealis, other
  buttons and sticks throw `NotSupportedError`. Switch driver: steps and screen checks from `device/profile.json`, nxlink launch,
  `APPLET_MODE_READY = false` until WP-08 (applet mode needs WP-03b's crash handler first).
- sys-autopilot client: its own HTTP/1.1 over a TCP socket, one request at a time, ≥ 150 ms apart, the PC closes each connection. Bursts
  (~50 connections in a second) made the console reset every new connection for ~10 s, curl's too; reads, uploads and deletes then back off
  0.5 → 30 s (WP-03c: was 8 s, a spell lasted over 30 s), inputs are never repeated. Node's fetch and http modules had reported resets on `GET /files` where a plain socket didn't
  (possibly the same burst effect; not proven). Events are polled every 500 ms, "is the app still there" every 3 s by screenshot.
- Fatal gate: `atmosphere/fatal_reports/` joins `fatal_errors/` (owner OK 2026-10-25): Atmosphère's fatal screen writes `fatal_reports/`
  (except when creport caused it), `fatal_errors/` is only written at the next boot. A new file in either writes
  `tests/e2e/out/STOP-console-fatal.txt`, and the Switch driver refuses to start until the owner deletes it.
- CI is unchanged for now: the desktop job still runs `tools/desktop-smoke.sh`. A desktop-E2E job is a later chore (whether CI's Mesa renders
  the goldens within tolerance is untested).
Rejected: fetch/node:http for the console (resets), reusing connections (the console closes them after 50 ms idle anyway), parallel requests,
a uinput virtual gamepad on desktop (needs root), keeping the events file open (blocks the console's reads), ImageMagick for screen checks.

**D33 How crash containment and the write jail are built** (WP-03b, 2026-10-25). Carries out D13, D14, D28 and SAFETY.md S1–S5, S8.
Details: `src/app/crash/README.md`, `src/platform/README.md`.
- **How a crash ends** (on the Switch replaced by D35: the process always ends at once, after the crash screen if one shows).
  Main thread (CPU crash, `std::terminate`, `diagAbortWithResult`): record, then D13's `exit(EXIT_FAILURE)`
  after the "exit steps" stopped the other threads (borealis's task thread now, worker pools later) and with a 3 s deadline.
  Another thread: record, the thread parks forever, the healthy main thread shows the crash screen and quits normally. The
  watchdog (main loop stuck 10 s), a second crash, or an exit that hangs: `platform::exitProcessNow()` (`svcExitProcess`, an
  ordinary process exit like closing a game, never creport). Why not `exit()` everywhere: on the Switch `exit()` hands the CPU
  back to hbloader, which unloads our code; from a non-main thread, or with threads still running, they would run into unloaded
  memory (an application crash after all). GPU aborts (module 359) never show the crash screen.
- **Crash screen** = our own `ui::CrashActivity` (borealis's `Application::crash()` is an empty stub), only when the main thread
  is fine. After a main-thread crash the next launch shows a short "closed unexpectedly" note instead.
- **Record**: `logs/crash-<UTC time>-<n>.txt`, written into `logs/crash-pending.tmp` (opened empty at start-up, no allocation
  after a crash) and then renamed. The next launch files records into `logs/crashes/` (newest 10) and runs in safe mode
  (`app_start.safeMode`, cache cleared; WP-08 halves budgets). Text `key: value` format, `BASE + 0x…` offsets for
  `tools/symbolize-crash`. Our Switch code is built with `-fno-omit-frame-pointer` for the stack walk.
- The libnx hooks live in the app's own objects, not `platform/switch/` (libnx refers to the handler only weakly, so a library
  definition would never be linked). The desktop uses POSIX signals with per-thread alternate stacks, so all of it runs on the PC.
- **Watchdog**: its own 64 KB pthread, counts 20 ticks of 0.5 s without a frame (counting ticks, so a sleeping console doesn't
  trigger it), paused while the app is out of focus (borealis keeps the loop running in the background).
- **SafeFs** (`platform/SafeFs`, one POSIX implementation) + pure path rules (`core::jailPath`: case-insensitive match of the
  root, `sdmc:` only, `..` never above the root, odd characters and `.nro` files refused). Rename replaces the target (deleting
  it first where Horizon refuses). `tools/safety/check-writes.sh` (run by `tools/build.sh tests`, so CI) fails on raw writes
  anywhere else in `src/`.
- **Safety gate**: `check-elf.sh` runs inside the CMake `.nro` target before `elf2nro`, so no `.nro` exists unless the `.elf` passes
  (banned symbols, our hooks, `__nx_exception_stack` ≥ 32 KB). `tools/safety/stand-ins.txt` is the one list for the `--wrap`
  flags and the gate. `tools/safety/gate-selftest.sh` proves a deliberately linked `fatalThrow` / `setsysSet*` fails the build.
  `allowed-symbols.txt` exists and is empty. `appletSetCpuBoostMode` (D17) was switched off here (libnx's `_appletCleanup`
  references it); WP-03c switched it on with D34's `only-from` rule.
- Drills start on the first `idle`; the desktop driver's `launch()` returns without a window if the app has already ended.
Rejected: `svcExitProcess` for every crash (D13 says `exit()`; the device drills, WP-03c, show whether that's worth changing:
they did, D35),
a borealis loop inside the exception handler to show the crash screen for main-thread crashes (borealis isn't re-entrant),
pre-opening the final `crash-<time>.txt` (an empty one after a kill would look like a crash), the gate only as a `tools/build.sh`
step (a plain `cmake --build` would skip it), `std::thread` for the watchdog and drills (no way to set the stack size, S8).

**D34 The `only-from` rule for a banned function a library calls from inside its own file** (WP-03c, owner OK 2026-10-26).
Carries out D17 for `appletSetCpuBoostMode`. libnx defines it in `applet.o` next to `_appletCleanup` (its exit code), which calls it
with "Normal" only if a boost was set, and only the function itself sets one. `-Wl,--wrap` redirects only references between object
files, so a D28 stand-in can't reach that call. `banned-symbols.txt` says `S3 appletSetCpuBoostMode only-from=_appletCleanup`: the
function may be in the `.elf`, but `check-elf.sh` fails the build on any other reference to it: a call or jump naming it, its address
built with `adrp` + `add`, or its address stored in a loaded non-code section (a function pointer, a GOT entry). `gate-selftest.sh`
proves that a call (probe `BOOST`) and a stored pointer (probe `BOOSTPTR`) both fail the build.
Rejected: a D28 stand-in (can't reach a call inside the same object file), a source grep only (blind to other libraries), leaving
D17's ban off. Limit: an address computed some other way (arithmetic on another function's address) isn't seen; nothing does that.

**D35 On the Switch every crash ends the process at once** (WP-03c, owner OK 2026-10-26; amends D13 and D33's "How a crash ends").
A crash on the main thread (CPU exception, `std::terminate`, `diagAbortWithResult`): record, then `svcExitProcess`. A crash on another
thread or a caught exception: the crash screen as before, but closing it ends the process too (`crash::endAfterCrashScreen()`), not a
return to hbmenu. The desktop keeps D13's `exit()` (exit code 1). Why, seen on the console with our record already written each time:
`exit()` hands the CPU back to hbloader, which starts hbmenu in the same process with the same heap, and hbmenu then crashed under
Atmosphère's crash reporter. After a main-thread crash borealis's graphics were still set up (a crash skips their shutdown: a data
abort in hbmenu); after a worker crash the parked thread's stack was still mapped out of the heap (a deko3d abort in hbmenu, with the
parked thread in the report). What the user sees now: the HOME menu says "The software was closed because an error occurred." (the
system's note for a process that ends without telling the applet manager; no crash report, no fatal), then the next launch's
"closed unexpectedly" note. The exit steps and the 3 s exit deadline are now desktop-only.
Rejected: running borealis's graphics shutdown after a crash, then `exit()` (runs code of the program that just crashed: it could
hang or crash again); ending at once only after a thread crash (two rules, and the state behind a caught exception may be broken too).

**D36 How core's models, GraphQL and config are built** (WP-04, 2026-10-26). Carries out D4, D9, D12/D21 and SAFETY.md S6. Usage: `src/core/README.md`.
- **Errors are values:** every parser returns `Result<T>` (a value or `Error{Parse|Server|Unauthorized, message}`) and never throws.
  JSON from outside goes through `parseJson()`: size limit (8 MiB) and nesting limit (64, counted before parsing) from `JsonLimits`,
  which `RuntimeProfile::json` carries.
- **Tolerant models:** plain structs without JSON in their headers; `fromJson<T>()` in `models/FromJson.hpp`. A missing, null or
  wrong-typed field keeps the default; a model fails only when it isn't an object or has no valid id. List entries that fail are
  skipped and counted (`NodeList::skipped`). Page URLs keep their index even when odd (the index is what `lastPageRead` means).
  `LongString` ids stay text (`Source::id`), times become `int64`.
- **Operations:** `graphql/Operations.hpp` pairs each document with its variables and its answer reader. Partial data is used; with
  no data the first error comes back (an unauthorized one first). Server messages are cut to their first line, without the
  "Exception while fetching data (/x) : " prefix, ≤ 300 bytes.
- **Library tabs:** `mangas(condition: {inLibrary: true}, filter: {categoryId: {equalTo: id}})`, and `{isNull: true}` for the Default
  category (id 0), sorted by title then id. `category(id) { mangas }` has no paging arguments (checked on the test server).
- **Embedding:** `EmbedGraphql.cmake` turns `src/core/graphql/<Name>.graphql` into `constexpr` strings at build time (whole-line comments
  dropped). File name = operation name; the contract test (`tests/contract/`, Node `graphql`) checks that, the schema, and that app
  documents use no deprecated field. `tools/build.sh tests` runs it; CI runs it in the Node job.
- **Our own documents are recorded:** a recorder query file can hold just `# document: <Name>` (+ vars), and the recorder sends the
  app's document. Their answers (`tests/fixtures/responses/app-*.json`) are what the C++ tests parse, so documents and parsers are
  tested together offline. Such recordings must not change state that later recordings show.
- **Config:** `config.json` = `{schemaVersion, server{url, auth: none|basic|ui_login, username, password, accessToken, refreshToken},
  reader{direction: ltr|rtl|vertical, fit: screen|width|height|original, serverConvertsToJpeg}}`. Loading never fails: defaults plus
  `problems`. Migration = a list of steps (version N → N+1), empty while the version is 1. Password and tokens are plain text on the SD
  card, like every homebrew app (basic auth needs the password on every request); WP-08's setup screen says so.
- **RuntimeProfile:** budgets are shares of the free memory with floors and caps (image cache 40 % ≤ 512 MiB, covers 5 % ≤ 64 MiB,
  textures 15 % ≤ 256 MiB, one decode 10 % ≤ 128 MiB); on the console (3,184 MiB heap) all sit at their caps. Safe mode halves them
  (not below the floors) and turns prefetch off. Applet mode returns a small "applet" profile that the D21 guard never uses.
- **Fuzzing:** two libFuzzer targets (`fuzz_gql_response`: every reader and every `fromJson` on every object; `fuzz_app_config`: load,
  and the save/load round trip must not change anything), ASan + UBSan. `tools/build.sh fuzz [s]` uses clang when it is there; without
  it (WSL today) GCC builds the same targets with a main() that only replays the seeds. CI's `fuzz` job installs clang and fuzzes 60 s each.
Rejected: `category(id) { mangas }` for the library (no paging), exceptions from parsers (one bad answer must not unwind a screen),
failing a whole list on one bad entry, a GCC-only "fuzzer" of our own (libFuzzer's coverage guidance finds far more), recording our
documents by hand-copying them into the recorder's folder (two copies drift apart).

**D37 How net works: our own Future, errors as values, every limit in the profile** (WP-05, 2026-10-26). Carries out D5 and
ARCHITECTURE.md "net"; `src/net/README.md` has the details.
- **`submit()` returns `net::Future<HttpResult>`, our own small type** (the WP let us pick a future or a main-thread callback).
  `then(fn)` runs fn once, on the network thread, when the answer is in (at once if it already is); `wait()` is for tests. net never
  includes borealis, so it stays testable on the PC; the api layer (WP-06) passes results to the UI with `then` → `brls::sync`.
- **Errors are values:** `HttpResult = core::Result<Response>`. Any HTTP answer (404, 401, 500 included) is a `Response`; only "no
  answer" is an `Error`, with four new `core::ErrorKind`s: `Network`, `Timeout`, `Cancelled`, `TooLarge`. The future always
  completes exactly once, also on cancel and shutdown.
- **One network thread, one curl multi handle** (D5). At most `httpConcurrency` transfers (6); the rest wait in our queue, by
  `Priority` and then submit order. curl's connection pool is capped at the same number, so the app holds few sockets.
- **Cancellation:** `CancelToken` is a shared atomic flag. Queued jobs are dropped before they start; running ones stop within
  ~50 ms (the thread checks at least that often).
- **Limits come from `RuntimeProfile` (new fields):** connect 5 s, request 30 s (images 60 s: the image module passes it), 3 redirects
  (http/https only), headers 64 KiB, net thread stack 512 KiB (a pthread, since `std::thread` can't set a stack size). The body limit
  (`json.maxBytes`; the image module passes `maxImageBytes`) is checked against Content-Length and again on every piece received,
  after un-gzip, so an oversized or gzip-bomb answer stops early instead of being buffered first (S6).
- **Retries (`core::RetryPolicy`):** GETs, plus POSTs the caller marks `idempotent` (a GraphQL query; never a mutation, which could
  apply twice). Only transient failures: refused/reset connection, empty reply, HTTP 408/429/502/503/504. 2 retries, 250 ms then
  500 ms apart. Timeouts aren't retried (they already waited the full time).
- **Sleep/resume:** `resetConnections()` drops running transfers (they retry) and swaps in a new multi handle, so no stale kept-alive
  socket is reused. Whoever handles the resume event (WP-08/WP-12) calls it.
- **Tests:** cpp-httplib 0.60.1 (MIT, single header, host only) serves real HTTP on 127.0.0.1. `tools/build.sh tsan` runs every unit
  test under ThreadSanitizer (also a CI step), with address randomisation off (`setarch -R`): newer kernels randomise more address
  bits than TSan supports.
- **Switch:** devkitPro's `switch-curl` 7.69 (TLS via libnx), linked as `curl` + `z`; the code guards the options newer than 7.69. Its
  resolver is the blocking one (threaded resolver compiled out), so a slow DNS lookup stalls the network thread: use an IP address.
- **Switch sockets: we keep borealis's setup**, no override in `platform/switch`. In application mode its `userAppInit` asks for
  12 BSD sessions and `sb_efficiency` 8 (applet mode: 2 and 1, unused, D21). BSD sessions limit parallel *blocking calls*, and
  curl's multi handle makes one at a time; we hold at most 6 sockets. Checked on the console: `aboutServer`, then 20 requests at
  once, all HTTP 200 on the first try (`tests/e2e/scenarios/net-hello.test.ts`).
- **Test hooks:** the test config's `"server": {"url"}` gives the Hello screen its server (WP-08 takes it over), and every finished
  request writes a `net` event (method, url, status, attempts, error kind) through a ResponseObserver.
Rejected: `std::future` (no continuation, the UI would have to poll); calling back on the main thread from net (net would need
borealis); a thread per request (more sockets and stacks); retrying POSTs by default; buffering a whole body before checking its size.

**D38 How the api works: one client per HttpClient, results delivered by the app, one shared token renewal** (WP-06, 2026-10-26).
Carries out ARCHITECTURE.md "api" and "Error handling", D9; `src/api/README.md` has the details.
- **Errors:** `ApiError{kind, message, detail}`: `message` for the screen, `detail` for the log. Kinds Offline, Timeout, Auth,
  Permission, Server, Parse, plus Cancelled (never shown). `ApiResult<T>` = `core::Result<T, ApiError>`: core's `Result` gained an
  error-type parameter (default `core::Error`, nothing else changed). Network → Offline, TooLarge → Parse, HTTP 401 → Auth,
  403 → Permission, 408 → Timeout, other statuses → Server (404 says "check the URL and sub-path"), a 200 that isn't GraphQL → Parse.
  Permission is also picked when a server text says "permission" or "forbidden" (multi-user servers; not seen on the stable server).
- **Delivery:** `ClientOptions::deliver` hands each finished call to the app's thread of choice (the app: `brls::sync`), so api has
  no borealis. If the call's token fired by then, the result is Cancelled: a screen that went away never gets a late answer.
- **Auth headers:** GraphQL calls carry the header the client picks itself, so a call knows which token it used, and `login`/
  `refreshToken` can go without one (the server refuses a login while logged in). Every other request to the server (images,
  thumbnails) gets the header from the HttpClient's request decorator. So one SuwayomiClient per HttpClient; `configure()` changes it.
- **ui_login renewal:** a refused token (GraphQL "Unauthorized" or HTTP 401) is renewed once, then the call is repeated: first
  `refreshToken`; if the server refuses that (it lives 60 days), `login` with the saved username + password; else Auth ("Your login
  has expired"). One renewal at a time: calls refused meanwhile wait for it; a call whose token was already replaced just repeats.
  A refused renewal is remembered until the login changes (no stream of failed logins from a grid of thumbnails); Offline/Timeout
  aren't remembered. An image answered 401 starts a background renewal, so the image module (WP-07) only needs to try it once more.
  New tokens go to `onSettingsChanged` (the app saves config.json).
- **No early renewal from the token's `exp`:** Suwayomi's access token has `exp` but no `iat`, and the Switch's clock can be off, so
  "expires soon" can't be judged from the console's time. (If ever needed: compare `exp` with the answer's HTTP `Date` header.)
- **logout** is local: Suwayomi has no logout for ui_login (tokens just expire). It drops the tokens and the saved password.
  **`simple_login`** (cookie login page, expires on restart or after 30 min) isn't supported for now.
- **Progress:** pages below 0 are sent as 0; above the end the server caps at the page count (the api doesn't know it). The server
  saves 0 for a chapter whose pages were never fetched, so the reader calls `pages()` first (it does anyway).
- **Server version:** `serverWarning(info)` gives D9's warning text; the setup/home screen shows it (WP-08).
- **Integration tests:** `tests/integration/` (doctest binary, not a ctest entry). `tools/build.sh integration [modes]` resets the test
  server, then per auth mode starts it and runs suites `all` + that mode (~1 min for all three), and stops it at the end. Not in CI
  yet: CI would need the server on Linux (TESTING.md "CI" step 3).
Rejected: renewing early by `exp` (clock); the decorator for GraphQL calls too (a call couldn't tell which token it used, and login
must go without one); api calling `brls::sync` itself (api would need borealis); a second Result template for api (a copy of
core's); one renewal per refused call (five parallel calls → five refreshes); retrying mutations.

**D39 How the image module works: decoders, the size guard, caches, one loader** (WP-07, 2026-10-26). Carries out D6,
ARCHITECTURE.md "image" and SAFETY.md S5–S8; `src/image/README.md` has the details and the budget numbers.
- **Decoders, the same on both platforms:** libjpeg-turbo (DCT scaling 1/2, 1/4, 1/8; CMYK converted), libpng's simplified API
  (every colour type), **libwebp 1.6.0 built from the pinned submodule `library/libwebp`** (decoder library only), and **our own
  first-frame GIF decoder** (~250 lines, bounds-checked readers, fuzzed). libjpeg-turbo and libpng are the system packages on the
  PC (`libjpeg-turbo8-dev`, `libpng-dev`) and devkitPro's on the Switch (2.1.2 / 1.6.48).
  Why libwebp from source: devkitPro's `switch-libwebp` is 1.0.2, which has CVE-2023-4863 (the lossless Huffman heap overflow,
  fixed in 1.3.2), and our pages come from scraped websites. Why our own GIF: stb_image's GIF code has open memory-safety
  reports (GitHub Security Lab, 2023) and giflib isn't packaged for the Switch. GIF pages are rare; a small decoder we fuzz is safer.
- **Type from magic bytes**, never Content-Type. Unknown → Unsupported; animated WebP → Unsupported (GIF: first frame).
- **Corrupt means corrupt:** any libjpeg "corrupt data" warning (cut short, bad Huffman code) fails the image, as sharp does
  (`failOn: warning`), so the Broken fixture's page 1 is a placeholder, not two thirds of grey. A file cut short in any format fails.
- **Size guard (S6), from the header, before anything big is allocated:** each side ≤ `maxImageSide`, and **everything one decode
  has alive at once** ≤ `maxDecodeBytes`: the decoder's output + the shrunk copy (if any) + the decoder's own big buffers (a
  progressive JPEG's coefficients, 128 bytes per 8×8 block per component; a lossless WebP's full-size picture). `core::planDecode`
  plans it: if the box would need too much, the output gets smaller (JPEG first tries a bigger DCT reduction); only if not even
  that fits is the image TooLarge. So "no single decode exceeds the budget" holds by construction; libjpeg's `max_memory_to_use` is a second line.
- **Never bigger than the original:** the output fits the box (a side of 0 = no limit, the webtoon `{width, 0}`) and the box is
  capped at `maxDecodeScale` × the screen. The GPU scales up for free. Shrinking is a box filter (area average, alpha-weighted).
- **Shrink slack (`shrinkSlackPercent` 15, from the Switch benchmark):** if what the decoder makes is at most 15 % bigger per side
  than the box, it's kept as it is. A 1000×1500 page in the reader's 2× box (960×1440) took 134 ms on the console, 116 of them
  for a 4 % shrink that saves 0.5 MB; now 18 ms. The reader draws it at screen size anyway (the GPU scales). Memory stays within
  the decode budget, and the caches count the real size.
- **Switch benchmark** (v1, application mode, stock clocks, from the SD-card cache; `image-bench`): JPEG 1000×1500 page 18 ms,
  WebP strip 800×12000 176 ms (libwebp decoding 9.6 Mpx; the same when scaled down), cache read 6–11 ms, peak +38.6 MB while
  the strip is alive (its 38.4 MB of pixels). The profile's budgets stay: 2 decode threads keep up with page turns, and one
  decode needs well under the 128 MiB cap.
- **The desktop counts mmap'd memory:** glibc hands out big blocks (any decoded page) with mmap, which `mallinfo2().uordblks`
  leaves out; `runtimeInfo()` on the PC now adds `hblkhd`, so the memory-pressure guard and `--mem-limit` see images. The
  Switch's allocator has no mmap.
- **Safety-gate fix (`tools/safety/check-elf.sh`, owner OK 2026-10-26):** the only-from rule's data search compared hex words in awk as numbers, so a
  word like `903e2100` (the new address of `appletSetCpuBoostMode`, read as 903×10^2100 = infinity) "matched" every other such
  word: 236 false stored pointers. Now compared as text (also the adrp page check). Equally strict: `gate-selftest.sh` still
  fails all four planted calls, the stored pointer at its exact address.
- **Tiles:** `core::planTiles` cuts the decoded image into equal tiles ≤ `maxTextureSide` (2048, S7). A tile as wide as the image is
  a pointer into it (no copy). `core::tileWindow` picks the tiles a webtoon reader keeps (`webtoonTilesAround`).
- **GPU budget (S7):** `TextureBudget`/`TextureLease`: the UI reserves a texture's bytes before creating it, main thread only.
- **Memory caches:** two `core::ByteLru` caches of decoded images, pages (`imageCacheBytes`) and covers (`coverCacheBytes`), so a
  library grid never pushes out reader pages. Key = URL + box. Shared pointers: an evicted image the UI still draws stays alive.
- **Disk cache (S5):** `cache/images/<2 hex>/<16 hex FNV-1a of the URL>.img` = magic + URL + length + FNV-1a checksum + the bytes as
  downloaded, written once with `SafeFs::writeAtomic` (only images that decoded). Broken or cut-short files count as missing and
  are deleted; .tmp leftovers and strangers go at the first scan. Budget `diskCacheBytes`, least recently used first (after a
  restart: oldest file first). No write if the free space would drop below `minSdFreeBytes` (`SafeFs::freeBytes`, statvfs).
  `stop()` at exit. A miss costs no SD access (the index is in memory).
- **One `ImageLoader`** (made in `main()` after the HttpClient, so page requests carry the login, D38): `load(request, cancel,
  done)`, `done` on the main thread through `deliver` (the app: `brls::sync`), like D38. Loads of one URL share one download (and
  one decode per box). Stages: memory → disk (one disk thread) → download (`maxImageBytes`, `httpImageTimeoutMs`) → decode
  (`decodeThreads`, 1 MiB stacks) → caches. HTTP 401 is tried once more after `imageAuthRetryMs` (the api renews meanwhile).
  A load whose callers have all cancelled stops: a 50 ms sweep fires the job's own token (net drops the download, the decoder
  stops at its next row check). More than `imageQueueLimit` jobs → Busy. Errors are values (`ImageError`: Network, Timeout,
  Cancelled, Http, TooLarge, Unsupported, Corrupt, Busy, Paused).
- **Memory-pressure guard:** the loader samples `platform::runtimeInfo()` every `pressureCheckMs` (on its disk thread);
  `core::MemoryPressure` decides: ≥ `pressurePercent` → caches to `pressureShrinkPercent`, Prefetch/Background loads refused
  (Paused), `mem_pressure` event; below `pressureRecoverPercent` (new, 75) → back to normal.
- **Profile fields added:** `maxTextureSide` 2048, `shrinkSlackPercent` 15, `imageQueueLimit` 128, `imageAuthRetryMs` 1000,
  `decodeThreadStackBytes` 1 MiB, `workerThreadStackBytes` 256 KiB (S8), `pressureRecoverPercent` 75. platform's SafeFs gained
  `listDirEntries` and `freeBytes`.
- **Dependencies:** image now includes `platform/SafeFs.hpp` (all writes go through it, D14), besides core and net. Never api.
- **Test hooks:** `image` events (source memory/disk/net, sizes, fetch/decode ms, bytes, memUsed at the decode's peak) and
  `mem_pressure`. The test config's `imageBench` runs WP-07's benchmark on the Hello screen (`image-bench` scenario).
Rejected: stb_image for PNG/GIF (open security reports; libpng is better tested); devkitPro's libwebp 1.0.2 (CVE-2023-4863);
checking only the source's w×h×4 (would refuse big pages that DCT scaling decodes cheaply, and miss progressive coefficients);
partial images for corrupt JPEGs; one cache for pages and covers; textures inside the image module (it can't include borealis).

**D40 How the app shell works: one context, Activity + Model screens, the log and config files, the guard** (WP-08, 2026-10-26).
Carries out ARCHITECTURE.md "ui"/"platform", SAFETY.md S5/S12, D16, D21, D38. Usage: `src/ui/README.md`, `src/app/README.md`.
- **One `AppContext`** (`src/app/AppContext.hpp`), made once in `main()`: fs, runtime info, the profile (built once, halved in
  safe mode), last run, `ConfigStore`, HttpClient, SuwayomiClient, ImageLoader. Every screen gets `AppContext&`; no globals.
- **Screens = `XxxActivity` (view, XML layout) + `XxxModel` (state and rules, no borealis)**; the models are the library
  `snx_ui_models`, unit-tested in suite `ui`. Reused components: `StatusView` (loading / error + Retry / message, no spinner:
  a running animation would keep a test from ever seeing `idle`), `TextFieldCell` (a tap opens the keyboard), `checkServer`.
- **The main screen is always the first screen** and never closes; setup and login open on top of it (borealis never pops
  its first activity, so a setup screen as the root would come back with B). The first run opens setup over it.
- **Connection check = `aboutServer`, plus `categories` when a login is configured:** Suwayomi answers `aboutServer` without a
  login even in ui_login mode, so only the second call proves the login (and makes the client renew or log in, D38). A
  refused login opens the login screen (ui_login) or setup (basic).
- **Log:** always written to `logs/app.log` (`--log-file` is no longer needed), buffered and written at most every
  `logFlushMs` (2 s), at HOME/sleep and at exit, each time open -> append -> close (S5: no file open between frames; the
  harness can read it while the app runs). Before it passes `logFileBytes` (1 MiB) it becomes `app.1.log`; at start the last
  run's log becomes `app.1.log`. Line = `<ms> <level> <tag>: <message>`, the same as the crash record's log.
- **Config:** `ConfigStore` saves `config.json` with `writeAtomic` + `.bak` once nothing changed for `configSaveDelayMs`
  (2 s, at most 8 s after the first change), at once on HOME/sleep and at exit; a missing or unreadable file falls back to
  the `.bak`. The test config's server is used in memory only. The harness removes `config.json` before each launch
  (`keepConfig: true` for restart tests).
- **Applet-mode guard (S12):** after the crash handler and the logger, before borealis and config. libnx's text console,
  any new button/tap/60 s, exit request honoured; all writes (one log line, `launch_refused`, the drill's event) before the
  screen, none after (not even `crash::finish()`: the next start deletes the empty pending crash file). Desktop prints the
  text. Note: borealis's `userAppInit` (sockets, romfs and system services) still runs before `main()` in applet mode, as since
  WP-01; it's libnx start-up, standard for every homebrew.
- **Idle rule:** no borealis ticking, `http.inFlight() == 0` (new in net: counts a request until its callbacks ran) and
  `images.activeJobs() == 0`, for 3 frames.
- **Platform:** `networkConnected()` (nifm has an IP: a home network without internet counts), `setResumeHandler()` (OnResume ->
  `resetConnections()`, D37), `showLaunchRefused()`. HOME/sleep come as borealis's focus event (flush config and log).
- **Touch (D16):** cells `minHeight` 72, buttons 72 high, borealis's sidebar items 72 (style metric). Desktop tests type with
  `xdotool type` and tap the text dialog's OK hint: Enter doesn't reach borealis while GLFW's input method is on.
- **Test hooks:** `launch_refused`, `mem` (every 10 s), `focus`, `error`, `server_check`, `login`, `login_screen`,
  `config_saved`, `net_burst`, `screen.params`, budgets in `app_start`; test config `server.auth/username/password`,
  `showSetup`, `diagnostics`. WP-05/07's Hello screen became the test-only diagnostics screen.
Rejected: setup as the first screen (see above), keeping the log file open (S5, and unreadable on the Switch while open),
`aboutServer` alone as the check (public in ui_login), a spinner in `StatusView` (never idle), patching borealis's Switch input
now for "all controllers merged" (WP-08b: its `getControllersConnectedCount` stops at the first empty slot, so a controller in
slot 2 with slot 1 empty is ignored; the fix replaces one borealis file and needs a second real controller to check).

**D41 A crash in applet mode is always a system fatal; the console checks only the refusal screen** (WP-08, owner OK
2026-10-27; amends D21 and SAFETY.md S12). Found on the console with the owner watching (applet-guard, 2026-10-26):
- The refusal screen works from the Album: the text shows, a button closes it, the app returns to hbmenu, only
  `launch_refused` is written, no fatal. Its golden is `tests/e2e/goldens/switch/applet-guard/refused.png`.
- The null-pointer drill inside it: our crash handler caught the crash and wrote its record (symbolized: the drill, called from
  `showLaunchRefused`), then ended the process (D35, `svcExitProcess`). From the Album that process is a library applet that
  the HOME menu (qlaunch) started. qlaunch stops itself when a library applet ends abnormally (`0x290` / 2144-0001 =
  `la::ResultLibraryAppletExitedAbnormally`; Atmosphère's author on Atmosphère issue #2368: "just qlaunch crashing because
  homebrew crashed"), and that is Atmosphère's "press POWER" fatal screen. No damage: our writes were all before the screen.
- So D21's "our crash handler covers the guard too" and S12's "even a bug here ends as app closed + crash log" don't hold:
  in applet mode **any** crash of ours is a system fatal, however the handler ends the process. The guard's protection is
  that it is tiny (libnx text console, about 30 lines, no borealis/GPU/network/config) and that its normal path is verified.
- Tests: applet-guard runs the drill on desktop only. On the console it is opt-in (`SNX_APPLET_CONSOLE=1`, owner watching)
  and checks only the refusal screen, never a drill.
- A safer ending for a crash in applet mode (for example handing control back to hbloader instead of ending the process) is
  left to WP-21 (applet support, nice to have); each try there can cost a reboot.
Rejected: keeping the drill in WP-08's acceptance (it can't pass by system design); trying other crash endings now (applet
mode isn't supported, D21, and every try risks a fatal).

**D42 The library screen: a model per screen kept by the main screen, our own recycling grid, sorting on the console**
(WP-09, 2026-10-29; MINUS for the menu: owner OK the same day).
- `LibraryModel` (no borealis, unit-tested) lives in `MainActivity`, not in the tab: borealis's tab frame deletes a tab's view
  when another tab opens, and the library must come back at the same category, sort and cover. The tab's view only shows it.
- The server can only order manga by title, id and two dates, so title order comes from the server and loads a page at a
  time (`libraryPageSize`, the next page when the grid shows the last half page); "unread" and "last read" load the whole
  category, then sort on the console (stable, ties stay in title order). "Last read" uses `lastReadChapter { lastReadAt }`
  (added to LibraryByCategory; the server sends `"0"` for never read). A manga seen twice across pages shows once.
- The Default category is a tab only when it has manga (or is the only category), like Suwayomi's web UI.
- The grid is our own `CoverGrid` (structure adapted from borealis's RecyclerFrame, Apache-2.0, CREDITS.md; the math in
  `GridLayout`, unit-tested): only the rows on screen have cells, the focused cell is never reused, and a cell passes every
  focus move to the grid (Box's own answer dereferences a missing child index: a null-pointer crash found by the scenario).
  Covers load at Interactive priority per visible cell, are cancelled when the cell goes, and the row below loads at Prefetch
  priority when `coverRowPrefetch` is on. Textures are reserved in `AppContext::textures` (S7) and freed with the cell.
- PLUS stays "quit" on every screen (the device scripts rely on it), so the library's menu is on MINUS instead of PLUS as
  WP-09 first said. L/R category, Y sort, X refresh, A open; all of them also by touch (tabs, Sort, Refresh, covers).
- config.json gets a `library` section (`categoryId`, `sort`), no migration (an old file simply lacks it).
- Test server: the seed adds the 150-manga "Big shelf" and an "Empty" category, so the grid can be scrolled, paged and
  recycled, and the empty state has a golden.
- Hiding a view that holds the focus: first switch the visibility, then give the focus to the view now shown (a hidden view
  refuses it, and the highlight stayed on a hidden cover: found on the console with R to an empty category).
- Test builds draw the focus frame without its moving glow (`testhooks::steadyScreenshots`): borealis freezes the glow
  wherever it was when the input stopped, which made the Switch smoke golden differ by 0.15 % between runs. Release keeps it.
Rejected: wiliwili's RecyclingGrid (GPL), borealis's RecyclerFrame as is (a list: rows of cells would lose the column on
up/down), sorting on the server (it can't), loading every category at start (one category at a time is enough).

**D43 The manga details screen: the whole chapter list on the console, "Continue" worked out from it, bulk chapter
actions** (WP-10, 2026-10-29).
- `MangaModel` (no borealis, unit-tested) loads the details and the chapters at the same time, then every chapter page
  (`chapterPageSize`, 100) until the list is complete, at most `RuntimeProfile::maxChapters` (10000; 3000 in applet mode),
  so a server can't make the list grow without end (S6). The list shows with the first page. Sort (newest / oldest first,
  by `sourceOrder`), the filter (all / unread / bookmarked), "Continue" and "all before" work on the console. Sort and
  filter aren't saved yet (each visit starts newest first, all).
- "Continue" (`continueReading`): the chapter read most recently (`lastReadAt`, added to `Chapters`; a tie goes to the
  later chapter), counting only chapters that are read or have progress. Unfinished: resume it at its page. Finished: the
  next unread chapter after it (forward, so a skipped chapter or a second scanlation doesn't pull the user back), else the
  first unread one from the start. No time stamps: the last read chapter in reading order instead. Nothing read: a chapter
  with progress, else Start at chapter 1. Every chapter read: "All chapters read" (the button is off).
- Found on the test server: the server stamps `lastReadAt` when an update sends `lastPageRead` (also 0), but not for
  `isRead` alone. "Mark unread" sends `lastPageRead: 0` (unread starts from the first page again), so an unread chapter at
  page 0 is no reading position, whatever its stamp says.
- Actions go out as one `updateChapters` (new document) for all the chapters they change, only those that would change;
  the list shows what the server answers (no guessing ahead), one action at a time. The row menu: mark read / unread,
  bookmark / remove, mark all before read / unread ("before" = lower `sourceOrder`, the chapter itself not included).
  "Refresh" = `fetchMangaAndChapters` (new document), then the list is loaded again; the old list stays until the new one
  is complete.
- Screen: details on the left (cover from the cover cache, title, author, status, genres, Continue, the description: five
  lines, A or a tap opens the rest), the chapter list on the right: our `CoverGrid` with one column (80 px rows), a "..." on
  each row for touch (borealis asks the innermost view first and cancels the row's own tap). A / tap opens the chapter, X
  toggles read, Y the row menu, MINUS the screen menu (D42), all also by touch (D16). The reader is a stand-in until WP-11.
- After chapter actions the library's unread badge (and its Unread sort) follow: `LibraryModel::updateManga`.
- Test builds: every event a screen writes now starts a new quiet spell, so `idle` follows it (on the console an action's
  answer came within one frame and the `idle` a scenario waited for never came); a `busy` event says what keeps the app
  busy for 5 s on end.
Rejected: loading chapters only near the end of the list like the library (sort, filter, Continue and "all before" need
every chapter); guessing the new state before the server answers (two quick presses could send the wrong toggle);
Suwayomi's `firstUnreadChapter` for Continue (it goes back to any gap).

**D44 The paged reader: one model for pages, window and progress; only the page on screen is a texture; progress saves are
throttled and never cancelled** (WP-11, 2026-10-30). Carries out ARCHITECTURE.md "core" reader math, D16, D17. Usage:
`src/ui/README.md`, `src/core/README.md`.
- `ReaderModel` (no borealis; unit-tested with `FakeHttpClient` and a real `ImageLoader`) holds the chapter's page list
  (`fetchChapterPages`), the position from -1 (the "previous chapter" screen) to `pageCount` (the "next chapter" screen), and
  one slot per page. The window comes from the profile (`core::prefetchOrder`: the page on screen at `Reader` priority, then
  `prefetchAhead` pages after it nearest first, then `prefetchBehind` before it, at `Prefetch`; Full = -1/+3). Pages that
  leave the window are cancelled and let go (the loader's memory cache keeps decoded pages within its budget). A prefetch
  refused because memory is low (Paused) or the queue is full (Busy) waits until it is the page on screen.
- When the window reaches past the last page, the next chapter's page list and its first `nextChapterPrefetchPages` pages
  load at `Prefetch` priority (only with `backgroundPrefetch`), so the next chapter opens without waiting. The next chapter
  opens where it was left (`lastPageRead`) unless it is read (then at the start), like Mihon; the previous one at its last
  page. Chapters go by `sourceOrder`; the list comes from the details screen.
- Progress (`core::ProgressSync`, one per opened chapter): `lastPageRead` at most every `progressSyncMs` (5 s, counted from
  opening), `isRead: true` at once when the last page shows (never `false`: re-reading never un-reads), and everything left
  at once when leaving a chapter, at HOME/sleep (borealis's focus event) and on close. One update per chapter at a time; a
  failed one is tried again after the interval (not after closing). Saves are never cancelled (a mutation cut off half way
  may or may not have reached the server): they belong to a small shared "saver" that outlives the reader. The details screen
  reloads its chapter list once the last save has ended (`ReaderActivity`'s `onClosed`), so Continue follows at once.
- Fits: Screen; Width (a tall page scrolls with the D-pad's up/down or a drag); Height (a page wider than the screen can be
  dragged sideways). Original is understood in config.json but not offered (WP-13 brings zoom). Pages decode at the fit's box in
  real pixels (`core::decodeBoxFor`; the loader caps it at `maxDecodeScale` x the screen) and are drawn from their source size.
  A sideways swipe turns only when the page was already at its edge that way (it moves the page first). Spreads (wider than
  1.2 x tall) are shown whole and reported; splitting or rotating them is left for later.
- Controls (D16): L/R and the D-pad's left/right (and the sticks: borealis's NAV buttons) turn pages, mirrored for right to
  left; up/down scroll; A the menu; X retry; B close; MINUS reader settings (PLUS stays quit, D42). Touch: the outer 30 % on
  each side turn (mirrored), the middle opens and closes the menu (with it open, any tap on the page closes it), a sideways
  swipe turns, a drag moves a page bigger than the screen; the menu has Close and Reader settings. Settings: direction (left to
  right / right to left), fit, background (black / gray / white), page number. Direction and fit are per manga in config.json
  (`reader.manga`, the one changed last at the end, at most `readerMangaSettingsMax` = 500, by the server's manga id);
  background and page number are for every manga. No migration: new keys, old files lack them.
- Drawing: `ReaderView` makes one texture per tile of the page on screen only (each reserved in `TextureBudget`, S7) and frees
  them on the next page; on desktop a turn to a decoded page takes 1-4 ms to the frame that shows it. The loading spinner is
  drawn from the clock (no borealis animation, so `idle` still comes). The reader's labels never auto-scroll: borealis scrolls
  any label under the focused view whose text is a hair wider than its box, forever, and the app never got `idle`. borealis
  sends touches only to focusable views (`hitTest`), so the menu's and Retry's buttons are focusable and give the focus back to
  the page afterwards.
- Keep the screen on: `platform::keepScreenOn(bool)` = `appletSetMediaPlaybackState` on the Switch (what video players use: no
  dimming and no auto-sleep for this application while set; it ends with the app, nothing on the system changes), on while
  the reader is open. Desktop: nothing.
- Test events: `reader` (what the screen shows once it has settled: `shown` page / page_error / chapter_error / start / end,
  `page`, `of`, direction, fit, menu, pageNumber, background, read, spread and sizes, `turnMs` + `ready` from the key or tap to
  the frame that shows the result, `retries`, `memUsed`), `progress` (every save, also the ones after the reader closed),
  `error` (`where` reader) for a page or chapter that couldn't be loaded.
- Not done: no download percentage under the spinner (the image loader has no download progress; it would need a progress
  callback through net); the stick doesn't pan (it turns, as borealis's NAV buttons); per-manga settings aren't kept apart per
  server; a `config.json` save blocks one frame (SafeFs's fsync on the main thread), and a key press shorter than that frame can
  be missed (seen once on desktop; the scenario waits for `config_saved` before its 100 turns).
Rejected: keeping textures of the neighbouring pages (measure the console first: a turn from a decoded page needs one texture
upload); cancelling a progress save when the reader closes; sending `lastPageRead` on every turn; borealis's `ProgressSpinner`
(a running animation, never idle); deciding the page in the next chapter by the details screen's (possibly old) copy instead of
the server's answer.
