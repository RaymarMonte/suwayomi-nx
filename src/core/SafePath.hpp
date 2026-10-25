#pragma once

#include <optional>
#include <string>
#include <string_view>

// The pure part of the write jail (docs/SAFETY.md S4, D14): decides whether a path is inside the app's own folder and spells
// it out in full. platform::SafeFs does the actual file work and asks this first, for every write.
namespace snx::core {

struct JailResult {
    std::optional<std::string> path;  // the full path inside the root, or nothing if the path was refused
    std::string why;                  // why it was refused, in plain words (empty when it was accepted)
};

// root: the app's folder with a trailing slash, e.g. "sdmc:/switch/suwayomi-nx/" (Switch) or "./data/" (desktop). Trusted.
// path: what the caller wants to write, either relative to the root ("logs/app.log") or absolute ("sdmc:/switch/suwayomi-nx/x",
//       "/switch/suwayomi-nx/x"). An absolute path is accepted only if it points into the root.
// The result is always the root's own spelling plus the remaining parts, so it can never leave the root:
//   - "." and empty parts ("//") are dropped, ".." goes up one folder but never above the root;
//   - folder and device names are compared case-insensitively (the SD card is FAT32/exFAT: "SWITCH" is the same folder);
//   - only the "sdmc:" device is accepted (and "/..." means "sdmc:/..." when the root is on sdmc);
//   - refused: backslashes, control characters, the characters <>:"|?* inside a name, names ending in "." or " ",
//     names over 255 bytes, paths over 768 bytes, and any file ending in ".nro" (the app never rewrites itself, D15);
//   - the root itself is refused unless allowRoot is true (makeDirs("") may name it, a file never can).
JailResult jailPath(std::string_view root, std::string_view path, bool allowRoot = false);

}  // namespace snx::core
