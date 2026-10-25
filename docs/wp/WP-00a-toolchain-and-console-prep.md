# WP-00a Toolchain + console prep

Model: **Opus** · Size: M · Needs: – · Owner: **present** (sudo password in WSL, SD card, console settings)

## Goal
After this session the PC can build Switch homebrew and system modules, and the console is backed up and checked,
so WP-00b can safely install our own sys-autopilot build. **Nothing gets installed on the console in this WP.**

## Read first
`docs/SETUP.md` (sections A and B), `docs/SAFETY.md` (backups and SD format parts), D19 in `docs/DECISIONS.md`.

## Deliverables
- [x] **First, backups** (SETUP.md B.9). The owner mounts the SD on the PC (Hekate → Tools → USB Tools → SD Card, or a card reader).
      Claude copies `atmosphere/`, `bootloader/`, `switch/`, `config/` to `C:\Users\JohnDoe\SwitchBackup\<date>\` (outside the repo) and checks the copy
      (file count + total size match). The owner confirms a sysMMC NAND backup exists, and backs up the emuMMC
      (file-based: copy the `emuMMC/` folder too; partition-based: Hekate eMMC backup with its emuMMC option on)
- [x] Record in STATUS.md "Console facts": SD format (FAT32/exFAT), SD size and free space, emuMMC type, Hekate version, how the console boots
      (RCM payload or modchip), and **every installed sysmodule**: each `atmosphere/contents/<id>/` with `flags/boot2.flag`, with its name if known.
      Flag any that are heavy or known to clash on FW 22 (they share the sysmodule memory pool with sys-autopilot-nx)
- [x] SD is **read-only** in this WP except for copying backups *off* it. The owner ejects it properly ("Safely remove") before unplugging
      (the only write was Hekate's own emuMMC backup into `backup/`, as asked above)
- [x] SETUP.md A: WSL memory raised, apt packages, devkitPro pacman + `switch-dev` and the Switch libraries, Node-in-WSL decision.
      Exact package lists and versions written back into SETUP.md
- [x] Toolchain proof 1: a minimal libnx hello-world `.nro` builds in WSL (`~/build/suwayomi-nx/spike/`)
- [x] Toolchain proof 2: upstream sys-autopilot, cloned at the commit WP-00b will pin, builds **unmodified** with `make` (gives `sys-autopilot.nsp`),
      and its host tests pass (`./tests/run.sh`). This proves WP-00b's build works before we change anything. Record the commit hash in STATUS
- [x] SETUP.md B.5–B.6: auto-sleep off while testing, DHCP reservation for the Switch. Record its IP in STATUS
- [x] If the SD is exFAT: don't reformat. Write it down, and add a note for the owner in STATUS on the trade-off (SAFETY.md)
      (not needed: the SD is FAT32)

## Acceptance
Upstream sys-autopilot builds in WSL and its host tests pass. The backup folder exists and matches the SD (file count, size).
STATUS.md lists SD format, emuMMC type, boot method and installed sysmodules.

## Sonnet-safe chunks
None. It's installs and hardware facts.

## Gotchas
- Downloads: the devkitPro installer and upstream sys-autopilot come from their official sources only (devkitpro.org, github.com/TooTallNate/sys-autopilot).
  Tell the owner what's being downloaded before doing it.
- Hekate's USB mass-storage mode holds the console in Hekate. The owner exits it from the console after ejecting on Windows.
- A Windows "scan and fix this drive?" prompt on the SD: the owner presses **Cancel**. Don't let Windows "repair" a Switch SD.
- Build dirs go in `~/build/suwayomi-nx/...` inside WSL (faster than `/mnt/c`).
