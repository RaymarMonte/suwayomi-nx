#include "core/SafePath.hpp"

#include <vector>

namespace snx::core {

namespace {

constexpr std::size_t kMaxPathBytes = 768;  // Horizon's FS limit is 769 bytes with the device name: stay under it
constexpr std::size_t kMaxNameBytes = 255;  // FAT32/exFAT name limit

struct SplitPath {
    std::string device;  // "sdmc" in "sdmc:/x", empty if there is none
    bool absolute = false;
    std::vector<std::string_view> names;  // still with ".." in them, "." and empty parts already dropped
};

char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool sameNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); i++) {
        if (lower(a[i]) != lower(b[i])) {
            return false;
        }
    }
    return true;
}

bool endsWithNoCase(std::string_view text, std::string_view end) {
    return text.size() >= end.size() && sameNoCase(text.substr(text.size() - end.size()), end);
}

// Why a single file or folder name can't be used, or empty if it's fine.
std::string badName(std::string_view name) {
    if (name.size() > kMaxNameBytes) {
        return "a name is longer than 255 bytes";
    }
    for (const char c : name) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte == 0x7f) {
            return "the path has a control character in it";
        }
        if (c == '<' || c == '>' || c == ':' || c == '"' || c == '|' || c == '?' || c == '*') {
            return std::string("the path has a '") + c + "' in a name";
        }
    }
    if (name != ".." && (name.back() == '.' || name.back() == ' ')) {
        return "a name ends with '.' or a space";
    }
    return "";
}

// "dev:/a//b/./c" → device "dev", absolute, names a b c. Returns why it was refused, or empty.
std::string split(std::string_view path, SplitPath& out) {
    if (path.find('\\') != std::string_view::npos) {
        return "the path has a backslash in it";
    }
    const std::size_t colon = path.find(':');
    const std::size_t slash = path.find('/');
    if (colon != std::string_view::npos && (slash == std::string_view::npos || colon < slash)) {
        out.device = std::string(path.substr(0, colon));
        path.remove_prefix(colon + 1);
        if (out.device.empty() || path.empty() || path.front() != '/') {
            return "a device name must look like \"sdmc:/\"";
        }
    }
    out.absolute = !path.empty() && path.front() == '/';
    std::size_t start = 0;
    while (start <= path.size()) {
        std::size_t end = path.find('/', start);
        if (end == std::string_view::npos) {
            end = path.size();
        }
        const std::string_view name = path.substr(start, end - start);
        if (!name.empty() && name != ".") {
            const std::string why = badName(name);
            if (!why.empty()) {
                return why;
            }
            out.names.push_back(name);
        }
        start = end + 1;
    }
    return "";
}

// Applies "..": each one removes the name before it. Returns false if a ".." would go above the start.
bool resolveDots(const std::vector<std::string_view>& names, std::vector<std::string_view>& out) {
    for (const std::string_view name : names) {
        if (name == "..") {
            if (out.empty()) {
                return false;
            }
            out.pop_back();
        } else {
            out.push_back(name);
        }
    }
    return true;
}

JailResult refuse(std::string why) {
    return JailResult{std::nullopt, std::move(why)};
}

}  // namespace

JailResult jailPath(std::string_view root, std::string_view path, bool allowRoot) {
    if (root.empty() || root.back() != '/') {
        return refuse("the root folder must end with '/'");
    }
    if (path.size() > kMaxPathBytes) {
        return refuse("the path is longer than 768 bytes");
    }
    SplitPath rootSplit;
    if (!split(root, rootSplit).empty()) {
        return refuse("the root folder itself is not a valid path");
    }
    SplitPath wanted;
    const std::string why = split(path, wanted);
    if (!why.empty()) {
        return refuse(why);
    }

    std::vector<std::string_view> inside;  // the names below the root
    if (!wanted.device.empty() || wanted.absolute) {
        // An absolute path: it must name the root's device and then walk through the root's folders.
        if (!rootSplit.absolute) {
            return refuse("an absolute path can't point into a relative root folder");
        }
        if (!wanted.device.empty() && !sameNoCase(wanted.device, rootSplit.device)) {
            return refuse("only the \"" + rootSplit.device + ":\" device is allowed, not \"" + wanted.device + ":\"");
        }
        if (wanted.device.empty() && !rootSplit.device.empty() && !sameNoCase(rootSplit.device, "sdmc")) {
            return refuse("a path without a device means sdmc:, but the root is on " + rootSplit.device + ":");
        }
        std::vector<std::string_view> full;
        if (!resolveDots(wanted.names, full)) {
            return refuse("the path goes above the top folder of the device");
        }
        std::vector<std::string_view> rootNames;
        resolveDots(rootSplit.names, rootNames);
        if (full.size() < rootNames.size()) {
            return refuse("the path is outside the app's folder");
        }
        for (std::size_t i = 0; i < rootNames.size(); i++) {
            if (!sameNoCase(full[i], rootNames[i])) {
                return refuse("the path is outside the app's folder");
            }
        }
        inside.assign(full.begin() + static_cast<std::ptrdiff_t>(rootNames.size()), full.end());
    } else if (!resolveDots(wanted.names, inside)) {
        return refuse("the path leaves the app's folder through \"..\"");
    }

    if (inside.empty()) {
        if (!allowRoot) {
            return refuse("the path is the app's folder itself, not a file in it");
        }
        return JailResult{std::string(root), ""};
    }
    if (endsWithNoCase(inside.back(), ".nro")) {
        return refuse("the app never writes .nro files (no self-update, D15)");
    }
    std::string result(root);
    for (std::size_t i = 0; i < inside.size(); i++) {
        if (i > 0) {
            result += '/';
        }
        result += inside[i];
    }
    if (result.size() > kMaxPathBytes) {
        return refuse("the full path is longer than 768 bytes");
    }
    return JailResult{std::move(result), ""};
}

}  // namespace snx::core
