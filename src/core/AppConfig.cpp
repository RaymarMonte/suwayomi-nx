#include "core/AppConfig.hpp"

#include <array>
#include <utility>

namespace snx::core {

namespace {

constexpr std::array<std::pair<AuthMode, std::string_view>, 3> kAuthNames = {{
    {AuthMode::None, "none"},
    {AuthMode::Basic, "basic"},
    {AuthMode::UiLogin, "ui_login"},
}};
constexpr std::array<std::pair<ReadingDirection, std::string_view>, 3> kDirectionNames = {{
    {ReadingDirection::LeftToRight, "ltr"},
    {ReadingDirection::RightToLeft, "rtl"},
    {ReadingDirection::Vertical, "vertical"},
}};
constexpr std::array<std::pair<PageFit, std::string_view>, 4> kFitNames = {{
    {PageFit::Screen, "screen"},
    {PageFit::Width, "width"},
    {PageFit::Height, "height"},
    {PageFit::Original, "original"},
}};
constexpr std::array<std::pair<LibrarySort, std::string_view>, 3> kSortNames = {{
    {LibrarySort::Title, "title"},
    {LibrarySort::Unread, "unread"},
    {LibrarySort::LastRead, "last_read"},
}};

template <typename E, std::size_t N>
std::string_view nameOf(const std::array<std::pair<E, std::string_view>, N>& names, E value) {
    for (const auto& [v, name] : names) {
        if (v == value) {
            return name;
        }
    }
    return names[0].second;
}

template <typename E, std::size_t N>
std::optional<E> valueOf(const std::array<std::pair<E, std::string_view>, N>& names, std::string_view name) {
    for (const auto& [v, n] : names) {
        if (n == name) {
            return v;
        }
    }
    return std::nullopt;
}

// Reads the fields of one section ("server", "reader"), noting what it can't use.
class SectionReader {
  public:
    SectionReader(const nlohmann::json& root, std::string_view section, std::vector<std::string>& problems)
        : section_(section), problems_(problems) {
        if (const auto it = root.find(section); it != root.end()) {
            if (it->is_object()) {
                node_ = &*it;
            } else if (!it->is_null()) {
                problem(std::string(section) + " is not an object");
            }
        }
    }

    void text(std::string_view key, std::string& out) {
        if (const nlohmann::json* value = present(key)) {
            if (value->is_string()) {
                out = value->get_ref<const std::string&>();
            } else {
                wrongType(key, "text");
            }
        }
    }

    void flag(std::string_view key, bool& out) {
        if (const nlohmann::json* value = present(key)) {
            if (value->is_boolean()) {
                out = value->get<bool>();
            } else {
                wrongType(key, "true or false");
            }
        }
    }

    void number(std::string_view key, int& out) {
        if (const nlohmann::json* value = present(key)) {
            if (const auto parsed = json::asInt32(*value)) {
                out = *parsed;
            } else {
                wrongType(key, "a whole number");
            }
        }
    }

    template <typename E, std::size_t N>
    void choice(std::string_view key, const std::array<std::pair<E, std::string_view>, N>& names, E& out) {
        const nlohmann::json* value = present(key);
        if (!value) {
            return;
        }
        if (value->is_string()) {
            if (const auto parsed = valueOf(names, value->get_ref<const std::string&>())) {
                out = *parsed;
                return;
            }
        }
        problem(std::string(section_) + "." + std::string(key) + " has an unknown value " + dumpJson(*value) + ", using \"" +
                std::string(nameOf(names, out)) + "\"");
    }

  private:
    const nlohmann::json* present(std::string_view key) {
        return node_ ? json::field(*node_, key) : nullptr;
    }
    void wrongType(std::string_view key, std::string_view expected) {
        problem(std::string(section_) + "." + std::string(key) + " must be " + std::string(expected) + ", ignored");
    }
    void problem(std::string text) {
        problems_.push_back("config: " + std::move(text));
    }

    std::string_view section_;
    std::vector<std::string>& problems_;
    const nlohmann::json* node_ = nullptr;
};

}  // namespace

std::string_view authModeName(AuthMode mode) {
    return nameOf(kAuthNames, mode);
}
std::optional<AuthMode> authModeFromName(std::string_view name) {
    return valueOf(kAuthNames, name);
}
std::string_view readingDirectionName(ReadingDirection direction) {
    return nameOf(kDirectionNames, direction);
}
std::optional<ReadingDirection> readingDirectionFromName(std::string_view name) {
    return valueOf(kDirectionNames, name);
}
std::string_view pageFitName(PageFit fit) {
    return nameOf(kFitNames, fit);
}
std::optional<PageFit> pageFitFromName(std::string_view name) {
    return valueOf(kFitNames, name);
}

std::string_view librarySortName(LibrarySort sort) {
    return nameOf(kSortNames, sort);
}
std::optional<LibrarySort> librarySortFromName(std::string_view name) {
    return valueOf(kSortNames, name);
}

const std::vector<ConfigMigration>& configMigrations() {
    // Version 1 is the first format: nothing to migrate yet. Add steps here, oldest first.
    static const std::vector<ConfigMigration> kSteps;
    return kSteps;
}

ConfigLoad loadAppConfig(std::string_view text, const JsonLimits& limits) {
    return loadAppConfig(text, limits, configMigrations());
}

ConfigLoad loadAppConfig(std::string_view text, const JsonLimits& limits, const std::vector<ConfigMigration>& steps) {
    const int current = static_cast<int>(steps.size()) + 1;
    ConfigLoad load;
    load.config.schemaVersion = current;
    std::vector<std::string>& problems = load.problems;

    Result<nlohmann::json> parsed = parseJson(text, limits);
    if (!parsed.ok()) {
        problems.push_back("config: " + parsed.error().message + ", using defaults");
        return load;
    }
    nlohmann::json& root = parsed.value();
    if (!root.is_object()) {
        problems.push_back("config: not a JSON object, using defaults");
        return load;
    }

    int version = 1;
    if (const auto stored = json::int32(root, "schemaVersion")) {
        version = *stored;
        if (version < 1) {
            problems.push_back("config: schemaVersion " + std::to_string(version) + " is not valid, read as version 1");
            version = 1;
        }
    } else {
        problems.push_back("config: no schemaVersion, read as version 1");
    }
    if (version > current) {
        problems.push_back("config: written by a newer app (version " + std::to_string(version) + ", this app knows " +
                           std::to_string(current) + "), settings it added are ignored");
    }
    for (int v = version; v < current; ++v) {
        steps[static_cast<std::size_t>(v - 1)](root);
        if (!root.is_object()) {  // a broken step must not take the app down
            problems.push_back("config: migration to version " + std::to_string(v + 1) + " failed, using defaults");
            return load;
        }
    }

    ServerSettings& server = load.config.server;
    SectionReader s(root, "server", problems);
    s.text("url", server.url);
    s.choice("auth", kAuthNames, server.auth);
    s.text("username", server.username);
    s.text("password", server.password);
    s.text("accessToken", server.accessToken);
    s.text("refreshToken", server.refreshToken);

    ReaderSettings& reader = load.config.reader;
    SectionReader r(root, "reader", problems);
    r.choice("direction", kDirectionNames, reader.direction);
    r.choice("fit", kFitNames, reader.fit);
    r.flag("serverConvertsToJpeg", reader.serverConvertsToJpeg);

    LibrarySettings& library = load.config.library;
    SectionReader l(root, "library", problems);
    l.number("categoryId", library.categoryId);
    l.choice("sort", kSortNames, library.sort);
    return load;
}

std::string saveAppConfig(const AppConfig& config) {
    nlohmann::ordered_json root;
    root["schemaVersion"] = config.schemaVersion;
    root["server"] = {
        {"url", config.server.url},
        {"auth", authModeName(config.server.auth)},
        {"username", config.server.username},
        {"password", config.server.password},
        {"accessToken", config.server.accessToken},
        {"refreshToken", config.server.refreshToken},
    };
    root["reader"] = {
        {"direction", readingDirectionName(config.reader.direction)},
        {"fit", pageFitName(config.reader.fit)},
        {"serverConvertsToJpeg", config.reader.serverConvertsToJpeg},
    };
    root["library"] = {
        {"categoryId", config.library.categoryId},
        {"sort", librarySortName(config.library.sort)},
    };
    return root.dump(2, ' ', false, nlohmann::ordered_json::error_handler_t::replace) + "\n";
}

}  // namespace snx::core
