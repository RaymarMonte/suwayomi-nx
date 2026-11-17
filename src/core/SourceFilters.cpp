#include "core/SourceFilters.hpp"

#include <array>
#include <utility>

#include "core/Json.hpp"

namespace snx::core {

namespace {

constexpr std::array<std::pair<FilterKind, std::string_view>, 9> kKindNames = {{
    {FilterKind::Header, "header"},
    {FilterKind::Separator, "separator"},
    {FilterKind::Text, "text"},
    {FilterKind::Select, "select"},
    {FilterKind::CheckBox, "checkbox"},
    {FilterKind::TriState, "tristate"},
    {FilterKind::Sort, "sort"},
    {FilterKind::Group, "group"},
    {FilterKind::Unknown, "unknown"},
}};

// The server's union member names (__typename) -> kinds.
constexpr std::array<std::pair<std::string_view, FilterKind>, 8> kTypeNames = {{
    {"HeaderFilter", FilterKind::Header},
    {"SeparatorFilter", FilterKind::Separator},
    {"TextFilter", FilterKind::Text},
    {"SelectFilter", FilterKind::Select},
    {"CheckBoxFilter", FilterKind::CheckBox},
    {"TriStateFilter", FilterKind::TriState},
    {"SortFilter", FilterKind::Sort},
    {"GroupFilter", FilterKind::Group},
}};

FilterKind kindOf(std::string_view typeName) {
    for (const auto& [name, kind] : kTypeNames) {
        if (name == typeName) {
            return kind;
        }
    }
    return FilterKind::Unknown;
}

// One filter, without its children.
SourceFilter readOne(const nlohmann::json& node, const FilterLimits& limits) {
    SourceFilter filter;
    if (!node.is_object()) {
        return filter;  // Unknown: keeps its place
    }
    filter.kind = kindOf(json::string(node, "__typename").value_or(""));
    filter.name = cutBytes(json::string(node, "name").value_or(""), limits.maxNameBytes);
    if (filter.kind == FilterKind::Select || filter.kind == FilterKind::Sort) {
        if (const nlohmann::json* values = json::array(node, "values")) {
            for (const nlohmann::json& value : *values) {
                if (filter.values.size() >= limits.maxValues) {
                    break;
                }
                filter.values.push_back(cutBytes(value.is_string() ? value.get_ref<const std::string&>() : "",
                                                 limits.maxNameBytes));
            }
        }
    }
    FilterValue& d = filter.defaults;
    switch (filter.kind) {
        case FilterKind::Text:
            d.text = cutBytes(json::string(node, "textDefault").value_or(""), limits.maxNameBytes);
            break;
        case FilterKind::Select:
            d.select = json::int32(node, "selectDefault").value_or(0);
            break;
        case FilterKind::CheckBox:
            d.check = json::boolean(node, "checkDefault").value_or(false);
            break;
        case FilterKind::TriState:
            d.tri = triStateFromName(json::string(node, "triDefault").value_or("")).value_or(TriState::Ignore);
            break;
        case FilterKind::Sort:
            if (const nlohmann::json* sort = json::object(node, "sortDefault")) {
                SortSelection selection;
                selection.index = json::int32(*sort, "index").value_or(0);
                selection.ascending = json::boolean(*sort, "ascending").value_or(false);
                d.sort = selection;
            }
            break;
        default:
            break;
    }
    return filter;
}

// Whether a filter of this kind has a value the user can change.
bool hasValue(FilterKind kind) {
    switch (kind) {
        case FilterKind::Text:
        case FilterKind::Select:
        case FilterKind::CheckBox:
        case FilterKind::TriState:
        case FilterKind::Sort:
            return true;
        default:
            return false;
    }
}

// The state part of a FilterChangeInput for `value` ({textState: ...} etc.), added to `change`.
void addState(nlohmann::json& change, FilterKind kind, const FilterValue& value) {
    switch (kind) {
        case FilterKind::Text:
            change["textState"] = value.text;
            break;
        case FilterKind::Select:
            change["selectState"] = value.select;
            break;
        case FilterKind::CheckBox:
            change["checkBoxState"] = value.check;
            break;
        case FilterKind::TriState:
            change["triState"] = std::string(triStateName(value.tri));
            break;
        case FilterKind::Sort: {
            const SortSelection sort = value.sort.value_or(SortSelection{});
            change["sortState"] = {{"index", sort.index}, {"ascending", sort.ascending}};
            break;
        }
        default:
            break;
    }
}

FilterSetting defaultSetting(const SourceFilter& filter) {
    FilterSetting setting;
    setting.value = filter.defaults;
    for (const SourceFilter& child : filter.children) {
        setting.children.push_back(child.defaults);
    }
    return setting;
}

}  // namespace

std::string_view filterKindName(FilterKind kind) {
    for (const auto& [k, name] : kKindNames) {
        if (k == kind) {
            return name;
        }
    }
    return "unknown";
}

std::string_view triStateName(TriState state) {
    switch (state) {
        case TriState::Include:
            return "INCLUDE";
        case TriState::Exclude:
            return "EXCLUDE";
        case TriState::Ignore:
            break;
    }
    return "IGNORE";
}

std::optional<TriState> triStateFromName(std::string_view name) {
    if (name == "IGNORE") {
        return TriState::Ignore;
    }
    if (name == "INCLUDE") {
        return TriState::Include;
    }
    if (name == "EXCLUDE") {
        return TriState::Exclude;
    }
    return std::nullopt;
}

TriState nextTriState(TriState state) {
    switch (state) {
        case TriState::Ignore:
            return TriState::Include;
        case TriState::Include:
            return TriState::Exclude;
        case TriState::Exclude:
            break;
    }
    return TriState::Ignore;
}

bool sameValue(FilterKind kind, const FilterValue& a, const FilterValue& b) {
    switch (kind) {
        case FilterKind::Text:
            return a.text == b.text;
        case FilterKind::Select:
            return a.select == b.select;
        case FilterKind::CheckBox:
            return a.check == b.check;
        case FilterKind::TriState:
            return a.tri == b.tri;
        case FilterKind::Sort:
            return a.sort == b.sort;
        default:
            return true;
    }
}

std::vector<SourceFilter> readFilterList(const nlohmann::json& list, const FilterLimits& limits) {
    std::vector<SourceFilter> filters;
    if (!list.is_array()) {
        return filters;
    }
    std::size_t budget = limits.maxFilters;
    for (const nlohmann::json& node : list) {
        if (budget == 0) {
            break;  // the rest are left out: the positions of the ones before stay right
        }
        --budget;
        SourceFilter filter = readOne(node, limits);
        if (filter.kind == FilterKind::Group) {
            const nlohmann::json* children = json::array(node, "filters");
            for (const nlohmann::json& childNode : children ? *children : nlohmann::json::array()) {
                if (budget == 0) {
                    break;
                }
                --budget;
                SourceFilter child = readOne(childNode, limits);
                if (child.kind == FilterKind::Group) {
                    child.kind = FilterKind::Unknown;  // two levels only (the document asks for no more)
                }
                filter.children.push_back(std::move(child));
            }
        }
        filters.push_back(std::move(filter));
    }
    return filters;
}

FilterSettings defaultSettings(const std::vector<SourceFilter>& filters) {
    FilterSettings settings;
    settings.reserve(filters.size());
    for (const SourceFilter& filter : filters) {
        settings.push_back(defaultSetting(filter));
    }
    return settings;
}

FilterSettings fitSettings(const std::vector<SourceFilter>& filters, FilterSettings settings) {
    const std::size_t had = settings.size();
    settings.resize(filters.size());
    for (std::size_t i = had; i < filters.size(); ++i) {
        settings[i] = defaultSetting(filters[i]);
    }
    for (std::size_t i = 0; i < filters.size(); ++i) {
        const SourceFilter& filter = filters[i];
        FilterSetting& setting = settings[i];
        if (setting.children.size() != filter.children.size()) {
            const std::size_t hadChildren = setting.children.size();
            setting.children.resize(filter.children.size());
            for (std::size_t c = hadChildren; c < filter.children.size(); ++c) {
                setting.children[c] = filter.children[c].defaults;
            }
        }
    }
    return settings;
}

int changedCount(const std::vector<SourceFilter>& filters, const FilterSettings& settings) {
    int count = 0;
    for (std::size_t i = 0; i < filters.size() && i < settings.size(); ++i) {
        const SourceFilter& filter = filters[i];
        if (filter.kind == FilterKind::Group) {
            for (std::size_t c = 0; c < filter.children.size() && c < settings[i].children.size(); ++c) {
                const SourceFilter& child = filter.children[c];
                if (hasValue(child.kind) && !sameValue(child.kind, child.defaults, settings[i].children[c])) {
                    ++count;
                }
            }
        } else if (hasValue(filter.kind) && !sameValue(filter.kind, filter.defaults, settings[i].value)) {
            ++count;
        }
    }
    return count;
}

nlohmann::json filterChanges(const std::vector<SourceFilter>& filters, const FilterSettings& settings) {
    nlohmann::json changes = nlohmann::json::array();
    for (std::size_t i = 0; i < filters.size() && i < settings.size(); ++i) {
        const SourceFilter& filter = filters[i];
        const int position = static_cast<int>(i);
        if (filter.kind == FilterKind::Group) {
            for (std::size_t c = 0; c < filter.children.size() && c < settings[i].children.size(); ++c) {
                const SourceFilter& child = filter.children[c];
                const FilterValue& value = settings[i].children[c];
                if (!hasValue(child.kind) || sameValue(child.kind, child.defaults, value)) {
                    continue;
                }
                nlohmann::json inner = {{"position", static_cast<int>(c)}};
                addState(inner, child.kind, value);
                changes.push_back({{"position", position}, {"groupChange", std::move(inner)}});
            }
        } else if (hasValue(filter.kind) && !sameValue(filter.kind, filter.defaults, settings[i].value)) {
            nlohmann::json change = {{"position", position}};
            addState(change, filter.kind, settings[i].value);
            changes.push_back(std::move(change));
        }
    }
    return changes;
}

std::string cutBytes(std::string_view text, std::size_t maxBytes) {
    if (text.size() <= maxBytes) {
        return std::string(text);
    }
    std::size_t cut = maxBytes;
    // Back off continuation bytes (10xxxxxx) so the cut lands before a character's first byte.
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
        --cut;
    }
    return std::string(text.substr(0, cut));
}

}  // namespace snx::core
