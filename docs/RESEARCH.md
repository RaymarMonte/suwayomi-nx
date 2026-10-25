# Research notes (2026-10-21)

## Existing projects

| Project | What it is | Licence | Use it for |
|---|---|---|---|
| [TooTallNate/sys-autopilot](https://github.com/TooTallNate/sys-autopilot) v1.5.0 (2026-06-28) | Atmosphère sysmodule: HTTP REST + native MCP on port 4150. Screenshots, virtual Pro Controller, SD file read/write/hash, title install, settings, mDNS `<host>.local` | see repo | **Device test driver** (TESTING.md). Its host-testable C split (`buttons.c`, `apiargs.c`, `tests/run.sh`) is a good pattern too |
| [xfangfang/borealis](https://github.com/xfangfang/borealis) (last commit 2026-04) | Switch-style UI lib: nanovg + Yoga flexbox, XML layouts, recycling lists, controller nav, touch, i18n, desktop/Switch/PS4/PSV/Android/iOS | Apache-2.0 | **Our UI framework** (D1) |
| [xfangfang/borealis_template](https://github.com/xfangfang/borealis_template) | Starter project for the above | Apache-2.0 | **Our starting skeleton** (WP-01) |
| [dragonflylee/switchfin](https://github.com/dragonflylee/switchfin) (886★, active 2026-10) | Native Jellyfin client: borealis, curl, libwebp, CMake desktop + Switch + MinGW | Apache-2.0 | **Closest architecture analog.** Study its HTTP wrapper, image loading/cache, settings, CI. Can adapt code with credit |
| [gomprime/mangadex-reader](https://github.com/gomprime/mangadex-reader) | MangaDex reader for Switch: borealis + deko3d, curl, custom mbedTLS 3.6.2 (TLS 1.3), nlohmann/json, disk cache, zoom/pan. Built with Claude's help | MIT | **Closest feature analog.** Reader UX, page cache, the TLS note. Its borealis fork patched two upstream bugs |
| [davidabejon/nxmanga](https://github.com/davidabejon/nxmanga) | Local CBZ/folder reader on Plutonium: paged + cascade (webtoon), portrait mode, touch | none listed | UX ideas only (no licence means we can't reuse its code) |
| [hawktocomet/komga-switch](https://github.com/hawktocomet/komga-switch) | Komga client: SDL2, curl, nlohmann/json, portrait mode, login | GPL-3.0 | UX ideas only (GPL) |
| [xfangfang/wiliwili](https://github.com/xfangfang/wiliwili) | Bilibili client on borealis, very polished | GPL-3.0 | UX/perf ideas only (GPL) |
| [Suwayomi/Suwayomi-Server](https://github.com/Suwayomi/Suwayomi-Server) v2.4.2366 | The server | MPL-2.0 | API (SUWAYOMI-API.md) |
| Suwayomi-WebUI, Suwayomi Client for KOReader, Tachidesk-qtui (C++/Qt) | Other clients | various | Real-world GraphQL query shapes |

No Suwayomi/Tachidesk client for Switch exists (GitHub repo search, 2026-10-21).

## Applet mode vs application mode (researched 2026-10-21)
Since D21 (2026-10-22) the app supports only application mode and refuses applet mode. These notes stay for the later WP-21.
- **Detection** (libnx `applet.h`): `AppletType appletGetAppletType()`. Enum: `Application=0, SystemApplet=1, LibraryApplet=2, OverlayApplet=3, SystemApplication=4`.
  hbmenu's "Applet Mode" warning (`nx-hbmenu/common/menu.c`) uses `at != AppletType_Application && at != AppletType_SystemApplication`.
  xfangfang/borealis has the same check as `SwitchPlatform::isApplicationMode()`.
- **Memory** (libnx `svc.h`): `svcGetInfo(&v, InfoType_TotalMemorySize /*6*/, CUR_PROCESS_HANDLE, 0)` and `InfoType_UsedMemorySize /*7*/`.
  Measured in WP-00: "used" already includes the whole heap libnx reserves at start-up (used = heap + ~1.4 MB), so the app uses the heap size
  and `mallinfo()` instead (D12 amendment).
- **Atmosphère knobs**: `system_settings.ini` `[hbloader] applet_heap_size` (0 = use all available applet memory) and
  `applet_heap_reservation_size` (default `0x8600000`, kept free for other applets). `override_config.ini`: Album → hbmenu unless R is held
  (`override_key_0=!R`), any app → hbmenu when R is held (`override_any_app=true`, `override_any_app_key=R`).
- Applet mode can be opened **while a game is suspended**, which leaves even less memory. An old nx-hbloader issue (#21) reports fatals in that case with some games.
- **Sockets**: borealis `switch_wrapper.c` uses `num_bsd_sessions=12, sb_efficiency=8` in application mode and `2, 1` in applet mode.
- **What others do**: Switchfin pushes a `HintActivity` (a tutorial on opening as an app) at startup in applet mode. Switchfin and wiliwili skip
  loading a custom font in applet mode to save memory. wiliwili opens its main screen directly only in application mode.

## Crash / brick safety (researched 2026-10-21, details in SAFETY.md)
- Atmosphère `stratosphere/creport/source/creport_main.cpp`: after a crash report, no fatal **only if** `IsApplication()` or `IsUserBreak()`,
  otherwise `fatalThrowWithContext(..., FatalPolicy_ErrorScreen, ...)`.
- libnx `nx/source/runtime/diag.c`: `__attribute__((weak)) void diagAbortWithResult(Result res) { svcBreak(BreakReason_Panic, …); }`.
- deko3d `Primer.md` + `source/dk_device.cpp`: release `RaiseError(result)` → `diagAbortWithResult(MAKERESULT(359, result))` on unrecoverable
  system/GPU errors (e.g. memblock init failure in `dk_memblock.cpp`).
- `__libnx_exception_handler` overrides in the wild: DarkMatterCore/nxdumptool (`source/utils/exception_handler.cpp`, GPL-3.0: logs regs + stack
  trace as BASE offsets, `brls::Application::crash()`, `__nx_applet_exit_mode = 1; exit(EXIT_FAILURE)`, 32 KB exception stack), Goldleaf, OpenMW,
  melonDS/Stable-Retro (JIT fault handling), tico (writes report to SD).
- borealis (xfangfang): no `fatalThrow` references.
- exFAT: community consensus (GBAtemp/Reddit, libnx issue #161) is that corruption comes from writes interrupted by crashes, force-close or power loss.
  FAT32 is recommended. Mitigation is fewer, atomic writes and clean exits.

## Emulators (optional, low priority)
Switch emulation in 2026 has settled on community forks: **Eden** (yuzu lineage, [eden-emu.dev](https://eden-emu.dev)), Ryubing (Ryujinx fork), Citron.
Eden runs homebrew `.nro` without keys or firmware. It *could* run our app, but sys-autopilot doesn't work there,
graphics fidelity for deko3d homebrew is unverified, and our desktop build already covers this. **Not in the plan.**
Warning: many GitHub "topic" repos offering emulator downloads are spam or malware. Only use the official site.

## Your machine (checked 2026-10-21)
- Windows 10 IoT LTSC 21H2 (19044), WSL 3.0.3 with WSLg, Ubuntu 24.04 (24 cores, **5 GB RAM limit**, so raise it), g++ present.
- Installed: git, curl, Java 21 (Temurin), Node + pnpm (nvm4w), Rancher Desktop (Docker daemon wasn't responding at check time).
- Missing: devkitPro, CMake, Ninja, Python (not needed), nxlink, Xvfb/xdotool.
