#include "core/SourceLanguages.hpp"

#include <algorithm>
#include <map>

namespace snx::core {

namespace {

// ASCII lower case, for comparing names in any case.
std::string lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

bool contains(const std::vector<std::string>& list, std::string_view value) {
    return std::find(list.begin(), list.end(), value) != list.end();
}

// The order of the languages: Local first, then by name (any case), then by code.
bool languageBefore(const std::string& a, const std::string& b) {
    const bool localA = a == kLocalSourceLang;
    const bool localB = b == kLocalSourceLang;
    if (localA != localB) {
        return localA;
    }
    const std::string nameA = lower(languageName(a));
    const std::string nameB = lower(languageName(b));
    if (nameA != nameB) {
        return nameA < nameB;
    }
    return a < b;
}

// The sources grouped by language, each group sorted, the groups in the list's order.
std::vector<std::pair<std::string, std::vector<std::size_t>>> grouped(const std::vector<Source>& sources) {
    std::map<std::string, std::vector<std::size_t>> byLanguage;
    for (std::size_t i = 0; i < sources.size(); ++i) {
        byLanguage[sourceLanguage(sources[i])].push_back(i);
    }
    std::vector<std::pair<std::string, std::vector<std::size_t>>> groups(byLanguage.begin(), byLanguage.end());
    std::sort(groups.begin(), groups.end(), [](const auto& a, const auto& b) { return languageBefore(a.first, b.first); });
    for (auto& group : groups) {
        std::sort(group.second.begin(), group.second.end(), [&](std::size_t a, std::size_t b) {
            const std::string nameA = lower(sources[a].displayName);
            const std::string nameB = lower(sources[b].displayName);
            if (nameA != nameB) {
                return nameA < nameB;
            }
            return sources[a].id < sources[b].id;
        });
    }
    return groups;
}

}  // namespace

const std::vector<std::pair<std::string_view, std::string_view>>& languageTable() {
    // The languages Tachiyomi / Mihon extensions use (their codes), in English.
    static const std::vector<std::pair<std::string_view, std::string_view>> kTable = {
        {kLocalSourceLang, "Local"},
        {"all", "All languages"},
        {"other", "Other"},
        {"af", "Afrikaans"},
        {"am", "Amharic"},
        {"ar", "Arabic"},
        {"az", "Azerbaijani"},
        {"be", "Belarusian"},
        {"bg", "Bulgarian"},
        {"bn", "Bengali"},
        {"br", "Breton"},
        {"bs", "Bosnian"},
        {"ca", "Catalan"},
        {"ceb", "Cebuano"},
        {"cs", "Czech"},
        {"cy", "Welsh"},
        {"da", "Danish"},
        {"de", "German"},
        {"el", "Greek"},
        {"en", "English"},
        {"eo", "Esperanto"},
        {"es", "Spanish"},
        {"es-419", "Spanish (Latin America)"},
        {"et", "Estonian"},
        {"eu", "Basque"},
        {"fa", "Persian"},
        {"fi", "Finnish"},
        {"fil", "Filipino"},
        {"fo", "Faroese"},
        {"fr", "French"},
        {"ga", "Irish"},
        {"gl", "Galician"},
        {"gu", "Gujarati"},
        {"ha", "Hausa"},
        {"he", "Hebrew"},
        {"hi", "Hindi"},
        {"hr", "Croatian"},
        {"ht", "Haitian Creole"},
        {"hu", "Hungarian"},
        {"hy", "Armenian"},
        {"id", "Indonesian"},
        {"ig", "Igbo"},
        {"is", "Icelandic"},
        {"it", "Italian"},
        {"ja", "Japanese"},
        {"jv", "Javanese"},
        {"ka", "Georgian"},
        {"kk", "Kazakh"},
        {"km", "Khmer"},
        {"kn", "Kannada"},
        {"ko", "Korean"},
        {"ku", "Kurdish"},
        {"ky", "Kyrgyz"},
        {"la", "Latin"},
        {"lb", "Luxembourgish"},
        {"lo", "Lao"},
        {"lt", "Lithuanian"},
        {"lv", "Latvian"},
        {"mg", "Malagasy"},
        {"mi", "Maori"},
        {"mk", "Macedonian"},
        {"ml", "Malayalam"},
        {"mn", "Mongolian"},
        {"mr", "Marathi"},
        {"ms", "Malay"},
        {"mt", "Maltese"},
        {"my", "Burmese"},
        {"ne", "Nepali"},
        {"nl", "Dutch"},
        {"no", "Norwegian"},
        {"nb", "Norwegian Bokmål"},
        {"ny", "Chichewa"},
        {"pa", "Punjabi"},
        {"pl", "Polish"},
        {"ps", "Pashto"},
        {"pt", "Portuguese"},
        {"pt-BR", "Portuguese (Brazil)"},
        {"pt-PT", "Portuguese (Portugal)"},
        {"ro", "Romanian"},
        {"ru", "Russian"},
        {"sd", "Sindhi"},
        {"sh", "Serbo-Croatian"},
        {"si", "Sinhala"},
        {"sk", "Slovak"},
        {"sl", "Slovenian"},
        {"sm", "Samoan"},
        {"sn", "Shona"},
        {"so", "Somali"},
        {"sq", "Albanian"},
        {"sr", "Serbian"},
        {"st", "Sesotho"},
        {"su", "Sundanese"},
        {"sv", "Swedish"},
        {"sw", "Swahili"},
        {"ta", "Tamil"},
        {"te", "Telugu"},
        {"tg", "Tajik"},
        {"th", "Thai"},
        {"ti", "Tigrinya"},
        {"tl", "Tagalog"},
        {"tr", "Turkish"},
        {"uk", "Ukrainian"},
        {"ur", "Urdu"},
        {"uz", "Uzbek"},
        {"vi", "Vietnamese"},
        {"xh", "Xhosa"},
        {"yi", "Yiddish"},
        {"yo", "Yoruba"},
        {"zh", "Chinese"},
        {"zh-Hans", "Chinese (Simplified)"},
        {"zh-Hant", "Chinese (Traditional)"},
        {"zu", "Zulu"},
    };
    return kTable;
}

std::string languageName(std::string_view code) {
    for (const auto& [c, name] : languageTable()) {
        if (c == code) {
            return std::string(name);
        }
    }
    return std::string(code);
}

bool isLocalSource(const Source& source) {
    return source.id == kLocalSourceId || source.lang == kLocalSourceLang;
}

std::string sourceLanguage(const Source& source) {
    return isLocalSource(source) ? std::string(kLocalSourceLang) : source.lang;
}

std::vector<SourceRow> sourceRows(const std::vector<Source>& sources, std::string_view lastUsedId,
                                  const std::vector<std::string>& hiddenLanguages) {
    std::vector<SourceRow> rows;
    if (!lastUsedId.empty()) {
        for (std::size_t i = 0; i < sources.size(); ++i) {
            if (sources[i].id == lastUsedId && !contains(hiddenLanguages, sourceLanguage(sources[i]))) {
                rows.push_back(SourceRow{i, "Last used", true});
                break;
            }
        }
    }
    for (const auto& [language, members] : grouped(sources)) {
        if (contains(hiddenLanguages, language)) {
            continue;
        }
        bool first = true;
        for (std::size_t index : members) {
            rows.push_back(SourceRow{index, first ? languageName(language) : std::string(), false});
            first = false;
        }
    }
    return rows;
}

std::vector<std::string> sourceLanguages(const std::vector<Source>& sources) {
    std::vector<std::string> languages;
    for (const auto& group : grouped(sources)) {
        languages.push_back(group.first);
    }
    return languages;
}

std::vector<std::size_t> searchSources(const std::vector<Source>& sources, const std::vector<std::string>& hiddenLanguages,
                                       std::size_t max) {
    std::vector<std::size_t> out;
    for (const SourceRow& row : sourceRows(sources, {}, hiddenLanguages)) {
        if (out.size() >= max) {
            break;
        }
        out.push_back(row.source);
    }
    return out;
}

}  // namespace snx::core
