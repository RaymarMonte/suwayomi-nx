# third_party

Single-header libraries copied in unchanged. To update one: download the new file from the same place, check its SHA-256,
update this table and `CREDITS.md`, run `tools/build.sh tests`.

| File | Version | From | SHA-256 | Licence |
|---|---|---|---|---|
| `doctest/doctest.h` | 2.5.3 | `https://raw.githubusercontent.com/doctest/doctest/v2.5.3/doctest/doctest.h` | `cfd518a3ef90f67e1f3ba514df23fb3627437de1a2feeba78cf5062a40021421` | MIT, `doctest/LICENSE.txt` |
| `nlohmann/json.hpp` | 3.12.0 | `https://github.com/nlohmann/json/releases/download/v3.12.0/json.hpp` (hash matches the release notes) | `aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63` | MIT, `nlohmann/LICENSE.MIT` |
| `httplib/httplib.h` | 0.60.1 | `https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.60.1/httplib.h` (host tests only: a local HTTP server for the curl client, WP-05) | `e932d7771bdb93150868987e30112d9a31e24bef592d6d7a07e973d5845cf8d6` | MIT, `httplib/LICENSE` |

**Two copies of nlohmann/json (D26).** borealis bundles its own older copy (3.11.2) and puts it on the include path of everything
that links borealis. Our targets put `third_party/` first, so `#include <nlohmann/json.hpp>` in our code always means 3.12.0.
borealis uses its copy only inside its own `.cpp` files (no borealis header exposes json types), and the two versions live in
different inline namespaces (`json_abi_v3_11_2` vs `json_abi_v3_12_0`), so they can't get mixed up at link time.
