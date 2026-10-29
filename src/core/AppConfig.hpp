#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/Json.hpp"
#include "core/models/Category.hpp"

// The app's settings (config.json in the app's folder, D10): server, login, reader preferences. Core only turns them into text
// and back; platform::SafeFs writes the file atomically and keeps a .bak (SAFETY.md S5), WP-08 debounces the saves.
// The password and tokens are stored as plain text on the SD card (every homebrew app does this; the setup screen says so).
namespace snx::core {

// The config format's version. Bump it when a field moves or changes meaning, and add a migration step (configMigrations()).
inline constexpr int kAppConfigVersion = 1;

// server.authMode on the Suwayomi side (SUWAYOMI-API.md "Auth"). simple_login isn't supported.
enum class AuthMode { None, Basic, UiLogin };
std::string_view authModeName(AuthMode mode);  // "none", "basic", "ui_login"
std::optional<AuthMode> authModeFromName(std::string_view name);

enum class ReadingDirection { LeftToRight, RightToLeft, Vertical };
std::string_view readingDirectionName(ReadingDirection direction);  // "ltr", "rtl", "vertical"
std::optional<ReadingDirection> readingDirectionFromName(std::string_view name);

enum class PageFit { Screen, Width, Height, Original };
std::string_view pageFitName(PageFit fit);  // "screen", "width", "height", "original"
std::optional<PageFit> pageFitFromName(std::string_view name);

// The library grid's order (WP-09). Title: A to Z, as the server sends it. Unread: most unread chapters first. LastRead: the
// manga read most recently first, never-read ones last.
enum class LibrarySort { Title, Unread, LastRead };
std::string_view librarySortName(LibrarySort sort);  // "title", "unread", "last_read"
std::optional<LibrarySort> librarySortFromName(std::string_view name);

struct ServerSettings {
    std::string url;  // as the user typed it, e.g. "http://192.168.1.10:4567" (may include a sub-path)
    AuthMode auth = AuthMode::None;
    std::string username;      // basic and ui_login
    std::string password;      // basic: sent with every request. ui_login: only to log in again when the refresh token expired
    std::string accessToken;   // ui_login
    std::string refreshToken;  // ui_login
};

struct ReaderSettings {
    ReadingDirection direction = ReadingDirection::RightToLeft;  // like Mihon's default; per-manga choices come later
    PageFit fit = PageFit::Screen;
    bool serverConvertsToJpeg = false;  // ask the server for image/jpeg pages (D6, off by default)
};

// The library screen, as the user left it (WP-09). A new section, so old files simply don't have it (no migration).
struct LibrarySettings {
    int categoryId = kDefaultCategoryId;  // the category shown last; one that no longer exists means the first tab
    LibrarySort sort = LibrarySort::Title;
};

struct AppConfig {
    int schemaVersion = kAppConfigVersion;
    ServerSettings server;
    ReaderSettings reader;
    LibrarySettings library;
};

struct ConfigLoad {
    AppConfig config;                   // always usable: what couldn't be read keeps its default
    std::vector<std::string> problems;  // in plain words, for the log
};

// One migration step: changes the JSON of version N so it reads as version N+1.
using ConfigMigration = void (*)(nlohmann::json& doc);

// The app's steps: step i turns version i+1 into i+2. Empty while kAppConfigVersion is 1. (Its size + 1 == kAppConfigVersion.)
const std::vector<ConfigMigration>& configMigrations();

// Reads config.json. Never throws and never fails: empty text, bad JSON or wrong types give defaults plus `problems`.
// Older versions are migrated first. A newer version (the app was downgraded) is read as far as it is understood.
ConfigLoad loadAppConfig(std::string_view text, const JsonLimits& limits = {});
// The same with other migration steps, for tests: the current version is then steps.size() + 1.
ConfigLoad loadAppConfig(std::string_view text, const JsonLimits& limits, const std::vector<ConfigMigration>& steps);

// The file's text: pretty JSON, schemaVersion first, ending in a newline.
std::string saveAppConfig(const AppConfig& config);

}  // namespace snx::core
