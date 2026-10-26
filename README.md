# suwayomi-nx

A native Nintendo Switch homebrew client (`.nro`) for a [Suwayomi-Server](https://github.com/Suwayomi/Suwayomi-Server)
manga library. C++17, UI built with [xfangfang/borealis](https://github.com/xfangfang/borealis). The same code also builds
as a desktop app (Linux), so most testing happens on a PC.

**Status:** early development. The app shows a "Hello Suwayomi" screen; nothing to read yet. See `docs/STATUS.md` and
`docs/ROADMAP.md`.

## Building

Inside Linux (or WSL on Windows) with the tools from `docs/SETUP.md` section A (CMake, Ninja, GCC; devkitPro for the Switch):

```bash
git clone https://github.com/RaymarMonte/suwayomi-nx.git && cd suwayomi-nx
git submodule update --init library/borealis library/libwebp
git -C library/borealis submodule update --init library/lib/extern/glfw
tools/build.sh tests      # unit tests
tools/build.sh desktop    # desktop app: ~/build/suwayomi-nx/desktop/suwayomi-nx
tools/build.sh switch     # Switch app:  ~/build/suwayomi-nx/switch/suwayomi-nx.nro
```

On the Switch, copy `suwayomi-nx.nro` to `sdmc:/switch/suwayomi-nx/` and start it from the Homebrew Menu in application mode
(hold R while starting a game). The app doesn't support the Album (applet mode) yet.

## Licence

Apache-2.0, see `LICENSE`. Third-party code and its licences: `CREDITS.md`.
