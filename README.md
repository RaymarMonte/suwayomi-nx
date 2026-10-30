# suwayomi-nx

A native Nintendo Switch homebrew client (`.nro`) for a [Suwayomi-Server](https://github.com/Suwayomi/Suwayomi-Server)
manga library. C++17, UI built with [xfangfang/borealis](https://github.com/xfangfang/borealis). The same code also builds
as a desktop app (Linux), so most testing happens on a PC.

**Status:** v0.1 (first release): browse your library, see a manga's chapters, read with the paged reader, and your
reading progress goes back to the server. Not there yet: the webtoon (long strip) reader, zoom, updates, history, browsing
sources, downloads. What changed: `CHANGELOG.md`. Plans: `docs/ROADMAP.md`.

## Using it

### What you need
- A Switch that runs homebrew (Atmosphère) and the Homebrew Menu.
- [Suwayomi-Server](https://github.com/Suwayomi/Suwayomi-Server) v2.4.2366 or newer, running on a computer on the **same
  network** as the Switch, with a manga library. The Switch has no Tailscale or VPN, so use the computer's LAN address
  (for example `http://192.168.1.20:4567`). Login: none, basic (username and password) or the web UI's login.

### Install
1. Download `suwayomi-nx.nro` from the [Releases](https://github.com/RaymarMonte/suwayomi-nx/releases) page. You can check
   the download against `SHA256SUMS.txt` there.
2. Copy it to `sdmc:/switch/suwayomi-nx/suwayomi-nx.nro` on the SD card.
3. To update later, copy the new `.nro` over the old one. Your settings stay.

### First run
1. **Open it by holding R while starting a game** (any installed game): keep holding R until the Homebrew Menu appears, then
   choose Suwayomi NX. This is "application mode", which gives the app the memory it needs.
2. The setup screen asks for the server's address (`http://<computer's LAN address>:4567`) and the login. "Test connection"
   checks it; "Continue" saves it and opens the library. Settings → Address changes it later.

### Controls
Everything works by touch (handheld) and with buttons. **+** quits the app on every screen, **B** goes back, and a tap on a
hint at the bottom of the screen does what its button does.

| Screen | Touch | Buttons |
|---|---|---|
| Library | tap a category, Sort, Refresh or a cover; flick to scroll | L / R category, Y sort, X refresh, A open, − library menu |
| Manga | tap Continue, a chapter, its "..." menu, Sort, Show, Refresh; tap the description to open it | A open chapter, X read / unread, Y chapter menu, − menu |
| Reader | tap the left or right side (or swipe) to turn, the middle for the menu; drag a page that is bigger than the screen | L / R and left / right turn, up / down scroll, A menu, X retry, Y rotate, − reader settings |

The reader turns pages right to left by default (manga). Reader settings (− or the menu): direction, fit (screen, width,
height), background colour, the page number and the orientation. Direction and fit are remembered per manga.

**Reading in portrait (held sideways, like a book):** in the reader press **Y** or tap **Rotate** in the menu, then turn
the console a quarter turn to the left, so the left Joy-Con is at the bottom. The page now fills the tall screen, about twice
as big as before. The reader settings' **Orientation** also offers "Portrait, turned right" (the right Joy-Con at the
bottom); after that Y switches between landscape and the way you used last. The choice is kept for every manga and after a
restart. Taps, swipes and the D-pad follow the turned screen (the D-pad's button that points to the right as you hold it
counts as right); L and R keep their sides. On the TV (docked) the reader is always landscape, and your choice comes back
in handheld mode. Only the reader turns: the library and the other screens stay landscape, and the reader settings menu
opens landscape too.

### Safety notes
- **Use the app only in application mode** (hold R while starting a game). The Album (applet mode) isn't supported yet:
  started from there, the app shows a short note and closes.
- **Quit with +** (or the "Exit" hint), not with HOME → Close Software, especially while pages are loading: + lets the app
  finish writing its files.
- **A FAT32 SD card is recommended.** exFAT can lose files when the console crashes or loses power.
- The app writes only to `sdmc:/switch/suwayomi-nx/` (its settings `config.json`, the page cache `cache/`, `logs/`).
  It never changes system files or settings.
- **If the app crashes**, it closes itself, shows "The software was closed because an error occurred", and starts in safe
  mode the next time (smaller caches, no prefetching). It keeps a crash report in `sdmc:/switch/suwayomi-nx/logs/`
  (`crash-*.txt`; older ones in `logs/crashes/`), also listed in Settings → Crash logs. Please attach it to a
  [GitHub issue](https://github.com/RaymarMonte/suwayomi-nx/issues) together with `logs/app.log`.

## Building

Inside Linux (or WSL on Windows) with the tools from `docs/SETUP.md` section A (CMake, Ninja, GCC; devkitPro for the Switch):

```bash
git clone https://github.com/RaymarMonte/suwayomi-nx.git && cd suwayomi-nx
git submodule update --init library/borealis library/libwebp
git -C library/borealis submodule update --init library/lib/extern/glfw
tools/build.sh tests      # unit tests
tools/build.sh desktop    # desktop app: ~/build/suwayomi-nx/desktop/suwayomi-nx
tools/build.sh switch     # Switch app:  ~/build/suwayomi-nx/switch/suwayomi-nx.nro
tools/release.sh          # a release: .nro, zipped .elf for crash reports, SHA256SUMS.txt -> ~/build/suwayomi-nx/release/v<version>/
```

On the Switch, copy `suwayomi-nx.nro` to `sdmc:/switch/suwayomi-nx/` and start it from the Homebrew Menu in application mode
(hold R while starting a game). The app doesn't support the Album (applet mode) yet.

## Licence

Apache-2.0, see `LICENSE`. Third-party code and its licences: `CREDITS.md`.
