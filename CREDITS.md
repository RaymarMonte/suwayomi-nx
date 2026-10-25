# Credits

Code from other projects that suwayomi-nx includes or adapts. Only MIT/Apache-licensed code is copied in (see `CLAUDE.md`).

| What | Where in this repo | From | License |
|---|---|---|---|
| borealis (UI library), a git submodule pinned at commit `5f08b286f3df737f3321d2247a6fe633fcead03c` (D25). Not copied: the build compiles it from the submodule, takes its runtime resources (icon font, system images, desktop fonts, its `hints.json` texts) from there, and links its `switch_wrapper.c` | `library/borealis/` | natinusala, xfangfang and contributors, https://github.com/xfangfang/borealis | Apache-2.0 (`library/borealis/LICENSE`) |
| The top-level CMake setup (options, toolchain include, `.nro` packing) follows borealis_template | `CMakeLists.txt`, `src/app/CMakeLists.txt` | xfangfang, https://github.com/xfangfang/borealis_template | Apache-2.0 |
| doctest 2.5.3 (unit-test framework, single header) | `third_party/doctest/` | Viktor Kirilov, https://github.com/doctest/doctest | MIT (`third_party/doctest/LICENSE.txt`) |
| nlohmann/json 3.12.0 (JSON, single header) | `third_party/nlohmann/` | Niels Lohmann, https://github.com/nlohmann/json | MIT (`third_party/nlohmann/LICENSE.MIT`) |
| sys-autopilot (sysmodule: REST + MCP remote control for the Switch), copied at commit `6ca6aac6722ff40be206118d3f93ade84234c762` and slimmed down, with touch input added (our fork "sys-autopilot-nx") | `tools/sys-autopilot-nx/` | Nathan Rajlich, https://github.com/TooTallNate/sys-autopilot | MIT (`tools/sys-autopilot-nx/LICENSE`) |
| jsmn (minimal JSON tokenizer, bundled by sys-autopilot) | `tools/sys-autopilot-nx/lib/jsmn/` | Serge A. Zaitsev, https://github.com/zserge/jsmn | MIT (`tools/sys-autopilot-nx/lib/jsmn/LICENSE`) |
