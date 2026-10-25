# Setup checklist

WP-00a (PC tools, backups, console checks), WP-00b (our sys-autopilot build) and WP-00 (borealis deps) walk through this together with Claude.
Items marked 👤 need you (hardware, admin prompts, SD card). Claude runs everything else. Versions are pinned by those WPs and written back here.

## A. Windows PC

1. 👤 **WSL memory** ✅ done 2026-10-21 (WP-00a). `%UserProfile%\.wslconfig` now has `memory=12GB` and `processors=12` under `[wsl2]`
   (it was 6 GB). The owner's `[experimental] autoMemoryReclaim=gradual` stays, so idle RAM still goes back to Windows. Then `wsl --shutdown`.
2. **Inside WSL Ubuntu** ✅ done 2026-10-21 (WP-00a). Claude runs commands with `wsl.exe -d Ubuntu --exec bash -lc '…'`.
   Use `--exec`, not `--`: with `--`, WSL passes the line through a shell first, so `$VARS` inside it are expanded too early (to nothing).
   The sudo steps ran as one script in a Terminal tab, so the owner typed the password once 👤.
   - apt (Ubuntu 24.04): `sudo apt-get install -y build-essential cmake ninja-build pkg-config git ccache gcovr xvfb xdotool imagemagick libglfw3-dev libcurl4-openssl-dev libwebp-dev libjpeg-turbo8-dev libpng-dev zlib1g-dev python3 jq zip unzip`
     → gcc 13.3.0, cmake 3.28.3, ninja 1.11.1, ccache 4.9.1, gcovr 7.0, xvfb 21.1.12, libglfw3-dev 3.3.10, libcurl 8.5.0, python3 3.12.3
     (the extra desktop deps borealis needs were added in WP-00, next item)
   - **WP-00 additions** (borealis's own CI list, `tools/spike/wp00-install-deps.sh`, one sudo password, ✅ 2026-10-25):
     apt `libdbus-1-dev` 1.14.10-4ubuntu4.1, `libxkbcommon-dev` 1.6.0-1build1, `wayland-protocols` 1.45-1~ubuntu0.24.04.2, `xorg-dev` 1:7.7+23ubuntu3
     (pulls in the X11 `-dev` libraries), and devkitPro `switch-glm` 0.9.9.7-2 (deko3d's nanovg renderer includes it).
     borealis builds with these: commit `5f08b28` (xfangfang/borealis main, 2026-04-25; bundled glfw `3.4-23-g892256c3`, SDL `release-2.28.5`).
     Desktop = GLFW + OpenGL 3, Switch = deko3d, 0 warnings (`tools/spike/build-demo.sh`).
   - **devkitPro**: their official Debian installer. The server answers 403 unless you use their documented user agent:
     `wget -U "dkp-apt" https://apt.devkitpro.org/install-devkitpro-pacman` then `sudo ./install-devkitpro-pacman` (it adds devkitPro's apt key + repo
     and installs `devkitpro-pacman` 6.0.1). `/etc/mtab` already exists on this WSL, so no symlink was needed. Then
     `sudo dkp-pacman -Syu` and `sudo dkp-pacman -S --needed switch-dev switch-examples switch-glfw switch-mesa switch-curl switch-libwebp switch-libjpeg-turbo switch-libpng switch-zlib deko3d`.
     Installed: libnx 4.12.0, devkitA64 r30 (devkita64-gcc 16.1.0, binutils 2.46.0, newlib 4.6.0.20260123), switch-tools 1.13.1
     (`nxlink`, `npdmtool`, `build_pfs0`, `elf2nro`), deko3d 0.5.0, uam 1.1.0, switch-mesa 20.1.0, switch-glfw 3.3.4, switch-curl 7.69.1,
     switch-libwebp 1.0.2, switch-libjpeg-turbo 2.1.2, switch-libpng 1.6.48, switch-zlib 1.3.1, switch-cmake 1.5.2. 28 packages in total.
     `DEVKITPRO=/opt/devkitpro` comes from `/etc/profile.d/devkit-env.sh`, so use a login shell (`bash -l`). borealis's Switch build also needs `switch-glm` (WP-00, below).
   - **Node: inside WSL via nvm (D20)**. nvm v0.40.8 installed the "manual" way (`git clone --branch v0.40.8 https://github.com/nvm-sh/nvm ~/.nvm`),
     loaded from `~/.profile` (not `~/.bashrc`, which stops early in non-interactive shells). `nvm install 22` → Node v22.23.4, npm 10.9.9, default alias 22.
   - Toolchain proofs (WP-00a): the libnx `templates/application` example builds to `hello.nro` in `~/build/suwayomi-nx/spike/hello/`.
     Upstream sys-autopilot (commit in STATUS) builds unmodified with `make` → `sys-autopilot.nsp`, `make -C app` → `sys-autopilot-app.nro`,
     and `./tests/run.sh` passes (C host tests plus its Node test). Clone: `~/build/suwayomi-nx/sys-autopilot-upstream/`.
3. **Java 21** is already on Windows (Temurin 21.0.11). It runs the Suwayomi *test* server (D11, D29).
4. 👤 **Windows Firewall** (done 2026-10-25, WP-02). The PC's Ethernet network is classed *Public*, and WSL's virtual network
   counts as Public too, so a "Private networks only" answer to Windows' Java pop-up doesn't help. Two steps, in an admin PowerShell:
   - Allow TCP 4568 from the home network only, on every profile:
     `New-NetFirewallRule -DisplayName "suwayomi-nx test server (TCP 4568, LAN only)" -Direction Inbound -Action Allow -Protocol TCP -LocalPort 4568 -RemoteAddress LocalSubnet -Profile Any`
   - Switch off the old "OpenJDK Platform binary" **Block** rules (TCP + UDP, Public) for this Java, which an earlier pop-up had
     made. Block rules beat allow rules. Switched off, not deleted:
     `Get-NetFirewallApplicationFilter | Where-Object { $_.Program -like '*jdk-21.0.11.10-hotspot*' } | Get-NetFirewallRule | Where-Object { $_.Action -eq 'Block' } | Disable-NetFirewallRule`
   - Inbound stays blocked for everything else (the profile's default). After a Java update (new folder name), check for a new
     Block rule the same way if the test server stops being reachable from WSL, the phone or the Switch.
   - Check: a phone on the home Wi-Fi opens `http://<pc-ip>:4568/api/v1/settings/about` and shows a line with the server version.
5. Optional: fix Rancher Desktop's Docker daemon (it timed out). This is only needed if we prefer containers to the jar locally.
   CI uses containers either way.

## B. Nintendo Switch (👤 all of these)

1. Atmosphère (current) with hbmenu working. Note firmware + Atmosphère versions in `docs/STATUS.md`.
2. **sys-autopilot-nx**, our slim build of sys-autopilot with touch (D19, built in WP-00b, `tools/sys-autopilot-nx/README.md`).
   ⚠️ Never install the stock v1.5.0 zip on this console: it has crash reports on FW 22, both at boot and on `/screenshot`.
   **Install or update** (a sysmodule only (re)loads at boot):
   1. Claude builds it in WSL: `tools/sys-autopilot-nx/build.sh --log` (`--log` for the first boots, without it afterwards). The files for the
      card land in `\\wsl.localhost\Ubuntu\home\johndoe\build\suwayomi-nx\sys-autopilot-nx\sd\`.
   2. 👤 Turn the Switch **off** (hold POWER → Power Options → Turn Off; sleep is not enough). Put the SD card in the PC, or connect USB and
      use Hekate → Tools → USB Tools → SD Card. Tell Claude the drive letter.
   3. Claude first lists what's there (the sysmodules = folders in `atmosphere/contents/` with `flags/boot2.flag`, plus
      `atmosphere/crash_reports/` and `atmosphere/fatal_errors/` as the "before" picture), then copies exactly these and checks the SHA-256 on the card:
      `atmosphere/contents/4200000000004150/exefs.nsp`, `atmosphere/contents/4200000000004150/flags/boot2.flag`, `config/sys-autopilot/config.ini`.
      Nothing else on the card is touched.
   4. 👤 Eject the card in Windows, put it back, boot as usual (RCM payload → emuMMC). Claude checks `/status`, then lists
      `crash_reports/` and `fatal_errors/` again (through `/files` when the server answers, otherwise on the card).
   - **Undo / if the console crashes at boot**: hold POWER for 12 s to switch it off, put the SD card in the PC (or mount it with
     Hekate → Tools → USB Tools → SD Card), and delete `atmosphere/contents/4200000000004150/`. That removes it completely.
   - **Switch it off without removing it**: delete `atmosphere/contents/4200000000004150/flags/boot2.flag` (it then stays on the card but doesn't start).
   - ✅ 2026-10-25 (WP-00b): `1.5.0-nx.1` installed, three clean boots, all checks passed (`tools/sys-autopilot-nx/README.md` → "Console results").
     Your setup: HOME → Power Options → **Restart** lands in Hekate (`reboot_payload.bin`, autoboot off), then Hekate → Launch → **CFW (EMUMMC)**.
     The SD card shows up in Windows as **SWITCH SD** (FAT32); the second drive is the emuMMC partition: if Windows offers to format it, click Cancel.
     Make sure the console isn't in airplane mode, or nothing can reach it.
3. `sdmc:/config/sys-autopilot/config.ini` is written by `build.sh`, so don't edit it by hand:
   - `token =` comes from `tests/e2e/device/.env` (git-ignored). Claude generated it on 2026-10-22 (64 random hex characters). It never goes in docs or chat.
     Without a token our build refuses every request.
   - `hostname = switch-dev` (gives `switch-dev.local` over mDNS)
   - `log = true` only for the first boots (`--log`); the log is `config/sys-autopilot/log.txt`
4. Check from WSL: `tools/sys-autopilot-nx/scripts/sa.sh GET /status`. It reads host, port and token from `.env`, so the token never
   shows up on a command line. `SWITCH_HOST=switch-dev.local tools/sys-autopilot-nx/scripts/sa.sh GET /status` tries mDNS instead of the IP.
   ✅ 2026-10-25: the IP works from WSL and Windows. `switch-dev.local` resolves on Windows but not inside WSL, so scripts use the IP.
   Later the same day (WP-00) `switch-dev.local` did resolve inside WSL too, but took ~1.1 s per request against 0.1 s by IP. Scripts keep the IP.
5. System Settings → Sleep Mode → **Auto-Sleep: Never** (while testing). Sleep drops Wi-Fi and the server goes away.
   ✅ 2026-10-22: Never for handheld and TV (on emuMMC).
6. Router: give the Switch a **DHCP reservation** (fixed IP). mDNS (`.local`) may not resolve from inside WSL.
   ✅ 2026-10-22: done in the router's admin page (DHCP binding: an entry "Switch" for its MAC address → a fixed LAN IP). That IP is
   `SWITCH_HOST` in `tests/e2e/device/.env` and is written `<switch-ip>` in these docs (the repo is public, D23). Ping works from Windows and WSL.
7. Controllers: ✅ 2026-10-25 (WP-00): **not needed**. HOME, hbmenu and borealis react to the virtual Pro Controller with the Joy-Cons attached
   (handheld) and with them detached, so leave them on. Turning off *other* wireless controllers is still a good idea, so nobody presses anything mid-run.
8. **Application mode must work.** The app runs only in application mode (D21):
   - Application mode: hold **R** while launching any installed title, which needs at least one installed game/app (a free demo is fine).
     This is Atmosphère's default `override_any_app=true`, `override_any_app_key=R` in `atmosphere/config/override_config.ini`.
     The owner's title is **Spelunky 2** (if it's a cartridge, the cartridge must be inserted). The game itself doesn't start. hbmenu takes its place.
     ✅ 2026-10-25 (WP-00): works with the virtual controller holding R. Spelunky 2 first asks "Who is using the software?" (A on the preselected
     first user), and hbmenu appears ~3.7 s later. The test harness expects Spelunky 2 as the **first tile** on HOME: if you played another game,
     start Spelunky 2 once (or hold R + A on it once) before a device test run. Details: D24, `tests/e2e/device/profile.json`.
   - Applet mode: tap the Album icon. hbmenu opens because Atmosphère's default overrides the Album *unless* R is held. Our app only shows
     a "can't run from the Album yet" screen there, so this path is used just to test that screen. (WP-00: hbmenu then shows a red
     "Applet Mode" badge; PLUS doesn't close it there, HOME does.)
   - Leave `[hbloader] applet_heap_size` / `applet_heap_reservation_size` in `system_settings.ini` at their defaults, so our
     measurements match what normal users get.

9. **Safety prep** (see `docs/SAFETY.md`):
   - Have a **NAND backup** (Hekate → Tools → Backup eMMC) and a copy of your SD card's `atmosphere/`, `bootloader/`, `switch/` folders on the PC.
     This is standard advice before any homebrew development. Our app shouldn't touch any of it, but backups make every risk recoverable.
     ✅ 2026-10-22 (WP-00a): sysMMC NAND backup is on the owner's external backup drive. The PC copy of the SD folders and the Hekate emuMMC backup
     is in `C:\Users\JohnDoe\SwitchBackup\2026-10-22\` (`sd\` = atmosphere, bootloader, switch, config, emuMMC without installed games, root files;
     `hekate-backup\` = BOOT0, BOOT1, rawnand.bin.00–14 plus `SHA256SUMS.txt`). Both copies were checked file by file with SHA-256.
     To restore the emuMMC (only if it ever gets damaged): copy those files into `backup\9178a073\restore\emummc\` on the SD (Hekate restores from the
     `restore` folder, which it created empty), then Hekate → Tools → Restore eMMC with the emuMMC switch on. Ask Claude to walk you through it first.
   - The owner boots **emuMMC** (the "E" after the Atmosphère version), so the homebrew system lives on the SD card and the internal system
     (sysMMC) is never touched by our testing. Back up the emuMMC too. If it's file-based, copy the `emuMMC/` folder. If it's partition-based, use
     Hekate's eMMC backup with its emuMMC option turned on.
   - A v1 Erista that boots by RCM payload can always be recovered from RCM, even after the worst software mistake.
   - Check your SD card's format (FAT32 or exFAT). FAT32 is the community recommendation because exFAT corrupts more easily on crashes.
     Don't reformat now. If it's exFAT, we'll talk it through first (it needs a full backup/restore of the card).
   - sys-autopilot is a system module, so if *it* ever crashes, that's a system fatal. Use our slim build (D19), not stock v1.5.0. Optionally remove its
     `atmosphere/contents/4200000000004150/flags/boot2.flag` when you aren't testing, so it doesn't run in everyday use.

## C. Accounts / services
- GitHub: public repo `RaymarMonte/suwayomi-nx` with CI (D23, created in WP-01). The `gh` CLI v2.103.2 is installed in WSL
  (`~/.local/bin/gh`, the official release tarball with its SHA-256 checked, no sudo) and logged in with scopes `repo`, `workflow`,
  `read:org`, `gist`. Its token sits in `~/.config/gh/hosts.yml` (plain text, WSL has no keyring). `gh auth setup-git` makes WSL's
  git use it, so pushes run from WSL: `wsl.exe -d Ubuntu --exec bash -lc 'cd <repo> && git push'`.
  This repo's commits use the GitHub noreply address (set in the repo's own git config, D23), not the global one.
- Your real Suwayomi server is never used by tests. Only the test server on port 4568 is.

## D. Add sys-autopilot to Claude Code (for interactive sessions)
```bash
claude mcp add --transport http switch http://<switch-ip>:4150/mcp --header "Authorization: Bearer <token>"
```
`<token>` is `SWITCH_TOKEN` from `tests/e2e/device/.env`. Type it in yourself, and don't paste it into a chat. Our build (sys-autopilot-nx) has no
OAuth login, so the bearer token is the only way in. Tools it offers: screenshot, buttons, sticks, `touch`, `swipe`, files, status.
