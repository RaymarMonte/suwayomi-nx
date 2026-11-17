// WP-14 (D56): the browse rules (filters, languages, the list order), the browse config and profile fields, and the
// browse GraphQL requests and answers.
#include <set>
#include <string>
#include <vector>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "core/AppConfig.hpp"
#include "core/RuntimeProfile.hpp"
#include "core/SourceFilters.hpp"
#include "core/SourceLanguages.hpp"
#include "core/graphql/Documents.hpp"
#include "core/graphql/Gql.hpp"
#include "core/graphql/Operations.hpp"
#include "core/models/FromJson.hpp"

using namespace snx::core;

namespace {

GqlResponse answer(const std::string& body) {
    auto r = parseGqlResponse(body);
    REQUIRE(r.ok());
    return r.value();
}

// SNX Test's filter list as the SourceFilters document gets it (tools/test-source Filters.java).
const char* kSnxFilters = R"({"data":{"source":{"id":"1105946461371740061","filters":[
  {"__typename":"HeaderFilter","name":"Filters also work without a search text"},
  {"__typename":"TextFilter","name":"Author","textDefault":""},
  {"__typename":"SelectFilter","name":"Status","values":["Any","Ongoing","Completed","On hiatus"],"selectDefault":0},
  {"__typename":"SeparatorFilter","name":""},
  {"__typename":"CheckBoxFilter","name":"In colour","checkDefault":false},
  {"__typename":"GroupFilter","name":"Genres","filters":[
    {"__typename":"TriStateFilter","name":"Action","triDefault":"IGNORE"},
    {"__typename":"TriStateFilter","name":"Comedy","triDefault":"IGNORE"},
    {"__typename":"TriStateFilter","name":"Drama","triDefault":"IGNORE"},
    {"__typename":"TriStateFilter","name":"Fantasy","triDefault":"IGNORE"},
    {"__typename":"TriStateFilter","name":"Romance","triDefault":"IGNORE"},
    {"__typename":"TriStateFilter","name":"Sci-Fi","triDefault":"IGNORE"},
    {"__typename":"TriStateFilter","name":"Long Strip","triDefault":"IGNORE"}]},
  {"__typename":"GroupFilter","name":"Type","filters":[
    {"__typename":"CheckBoxFilter","name":"Manga","checkDefault":false},
    {"__typename":"CheckBoxFilter","name":"Manhwa","checkDefault":false},
    {"__typename":"CheckBoxFilter","name":"Webtoon","checkDefault":false}]},
  {"__typename":"SortFilter","name":"Sort by","values":["Popularity","Title","Latest chapter"],
   "sortDefault":{"index":0,"ascending":false}}]}}})";

std::vector<SourceFilter> snxFilters() {
    auto r = gql::readSourceFilters(answer(kSnxFilters), FilterLimits{});
    REQUIRE(r.ok());
    return r.value();
}

Source src(std::string id, std::string displayName, std::string lang) {
    Source s;
    s.id = std::move(id);
    s.name = displayName;
    s.displayName = std::move(displayName);
    s.lang = std::move(lang);
    return s;
}

// What the test server lists (Local + SNX Test en/ja) plus a few more languages.
std::vector<Source> manySources() {
    return {src("11", "SNX Test (JA)", "ja"), src("0", "Local source", "localsourcelang"), src("12", "SNX Test (EN)", "en"),
            src("13", "alpha (EN)", "en"), src("14", "Leitor (PT-BR)", "pt-BR"), src("15", "Zeta", "xx-unknown"),
            src("16", "Every", "all")};
}

std::vector<std::string> names(const std::vector<Source>& sources, const std::vector<SourceRow>& rows) {
    std::vector<std::string> out;
    for (const SourceRow& row : rows) {
        out.push_back((row.label.empty() ? "" : "[" + row.label + "] ") + sources[row.source].displayName);
    }
    return out;
}

}  // namespace

TEST_SUITE("core") {

TEST_CASE("browse: SNX Test's filters read into the tree") {
    const auto filters = snxFilters();
    REQUIRE(filters.size() == 8);
    CHECK(filters[0].kind == FilterKind::Header);
    CHECK(filters[1].kind == FilterKind::Text);
    CHECK(filters[1].name == "Author");
    CHECK(filters[2].kind == FilterKind::Select);
    CHECK(filters[2].values == std::vector<std::string>{"Any", "Ongoing", "Completed", "On hiatus"});
    CHECK(filters[3].kind == FilterKind::Separator);
    CHECK(filters[4].kind == FilterKind::CheckBox);
    CHECK(filters[5].kind == FilterKind::Group);
    REQUIRE(filters[5].children.size() == 7);
    CHECK(filters[5].children[0].kind == FilterKind::TriState);
    CHECK(filters[5].children[6].name == "Long Strip");
    CHECK(filters[6].children.size() == 3);
    CHECK(filters[6].children[2].kind == FilterKind::CheckBox);
    CHECK(filters[7].kind == FilterKind::Sort);
    REQUIRE(filters[7].defaults.sort.has_value());
    CHECK(filters[7].defaults.sort->index == 0);
    CHECK_FALSE(filters[7].defaults.sort->ascending);
    CHECK(filterKindName(FilterKind::TriState) == "tristate");
}

TEST_CASE("browse: nothing changed sends nothing") {
    const auto filters = snxFilters();
    const FilterSettings settings = defaultSettings(filters);
    CHECK(changedCount(filters, settings) == 0);
    CHECK(filterChanges(filters, settings) == nlohmann::json::array());
}

TEST_CASE("browse: each filter type becomes its change") {
    const auto filters = snxFilters();
    FilterSettings s = defaultSettings(filters);
    s[1].value.text = "Aiko";
    s[2].value.select = 2;                         // Status = Completed
    s[4].value.check = true;                       // In colour
    s[5].children[0].tri = TriState::Include;      // Action
    s[5].children[2].tri = TriState::Exclude;      // Drama
    s[6].children[2].check = true;                 // Webtoon
    s[7].value.sort = SortSelection{1, true};      // Title ascending
    CHECK(changedCount(filters, s) == 7);
    const nlohmann::json expected = nlohmann::json::parse(R"([
        {"position":1,"textState":"Aiko"},
        {"position":2,"selectState":2},
        {"position":4,"checkBoxState":true},
        {"position":5,"groupChange":{"position":0,"triState":"INCLUDE"}},
        {"position":5,"groupChange":{"position":2,"triState":"EXCLUDE"}},
        {"position":6,"groupChange":{"position":2,"checkBoxState":true}},
        {"position":7,"sortState":{"index":1,"ascending":true}}])");
    CHECK(filterChanges(filters, s) == expected);

    // Back to the default: no longer sent.
    s[2].value.select = 0;
    s[7].value.sort = SortSelection{0, false};
    CHECK(changedCount(filters, s) == 5);
    CHECK(defaultSettings(filters)[2].value.select == 0);
}

TEST_CASE("browse: a sort without a default, unknown kinds and a group in a group") {
    const char* body = R"({"data":{"source":{"filters":[
        {"__typename":"SortFilter","name":"Order","values":["A","B"],"sortDefault":null},
        {"__typename":"FancyFilter","name":"New kind"},
        "not an object",
        {"__typename":"GroupFilter","name":"Outer","filters":[
            {"__typename":"GroupFilter","name":"Inner"},
            {"__typename":"TextFilter","name":"Text","textDefault":"x"}]}]}}})";
    auto r = gql::readSourceFilters(answer(body), FilterLimits{});
    REQUIRE(r.ok());
    const auto& filters = r.value();
    REQUIRE(filters.size() == 4);
    CHECK_FALSE(filters[0].defaults.sort.has_value());
    CHECK(filters[1].kind == FilterKind::Unknown);
    CHECK(filters[2].kind == FilterKind::Unknown);
    REQUIRE(filters[3].children.size() == 2);
    CHECK(filters[3].children[0].kind == FilterKind::Unknown);  // two levels only
    CHECK(filters[3].children[1].defaults.text == "x");

    FilterSettings s = defaultSettings(filters);
    CHECK(changedCount(filters, s) == 0);
    s[0].value.sort = SortSelection{1, false};
    s[3].children[1].text = "y";
    // The unknown filters keep their places: the group is still position 3.
    CHECK(filterChanges(filters, s) == nlohmann::json::parse(R"([
        {"position":0,"sortState":{"index":1,"ascending":false}},
        {"position":3,"groupChange":{"position":1,"textState":"y"}}])"));
}

TEST_CASE("browse: filter limits (S6)") {
    FilterLimits limits;
    limits.maxFilters = 9;  // SNX Test has 8 + 10 children
    limits.maxValues = 2;
    limits.maxNameBytes = 4;
    auto r = gql::readSourceFilters(answer(kSnxFilters), limits);
    REQUIRE(r.ok());
    const auto& filters = r.value();
    // 1 header, text, select, separator, checkbox, Genres + 3 of its children = 9.
    REQUIRE(filters.size() == 6);
    CHECK(filters[5].children.size() == 3);
    CHECK(filters[2].values.size() == 2);
    CHECK(filters[1].name == "Auth");
    CHECK(cutBytes("日本", 4) == "日");  // 3-byte characters: never half a one
    CHECK(cutBytes("abc", 10) == "abc");
}

TEST_CASE("browse: fitSettings and the tristate cycle") {
    const auto filters = snxFilters();
    FilterSettings s;
    s = fitSettings(filters, s);
    REQUIRE(s.size() == filters.size());
    CHECK(s[5].children.size() == 7);
    CHECK(changedCount(filters, s) == 0);
    CHECK(nextTriState(TriState::Ignore) == TriState::Include);
    CHECK(nextTriState(TriState::Include) == TriState::Exclude);
    CHECK(nextTriState(TriState::Exclude) == TriState::Ignore);
    CHECK(triStateFromName("EXCLUDE") == TriState::Exclude);
    CHECK_FALSE(triStateFromName("maybe").has_value());
}

TEST_CASE("browse: language names") {
    CHECK(languageName("en") == "English");
    CHECK(languageName("ja") == "Japanese");
    CHECK(languageName("pt-BR") == "Portuguese (Brazil)");
    CHECK(languageName("all") == "All languages");
    CHECK(languageName("other") == "Other");
    CHECK(languageName(kLocalSourceLang) == "Local");
    CHECK(languageName("xx-unknown") == "xx-unknown");
    std::set<std::string_view> codes;
    for (const auto& [code, name] : languageTable()) {
        CHECK_MESSAGE(codes.insert(code).second, "listed twice: ", code);
        CHECK_FALSE(name.empty());
    }
}

TEST_CASE("browse: the list's order, labels, Last used and hidden languages") {
    const auto sources = manySources();
    CHECK(names(sources, sourceRows(sources, "", {})) ==
          std::vector<std::string>{"[Local] Local source", "[All languages] Every", "[English] alpha (EN)", "SNX Test (EN)",
                                   "[Japanese] SNX Test (JA)", "[Portuguese (Brazil)] Leitor (PT-BR)",
                                   "[xx-unknown] Zeta"});
    const auto withLast = sourceRows(sources, "11", {});
    REQUIRE(withLast.size() == 8);
    CHECK(withLast[0].lastUsed);
    CHECK(withLast[0].label == "Last used");
    CHECK(sources[withLast[0].source].id == "11");
    CHECK_FALSE(withLast[1].lastUsed);

    // Hidden: its rows go, and a Last used source of a hidden language isn't shown either.
    const auto hidden = sourceRows(sources, "11", {"ja", kLocalSourceLang.data()});
    CHECK(names(sources, hidden) == std::vector<std::string>{"[All languages] Every", "[English] alpha (EN)", "SNX Test (EN)",
                                                             "[Portuguese (Brazil)] Leitor (PT-BR)", "[xx-unknown] Zeta"});
    // An unknown Last used id: no row.
    CHECK(sourceRows(sources, "999", {}).size() == 7);

    CHECK(sourceLanguages(sources) ==
          std::vector<std::string>{"localsourcelang", "all", "en", "ja", "pt-BR", "xx-unknown"});
    const auto searched = searchSources(sources, {"ja"}, 3);
    REQUIRE(searched.size() == 3);
    CHECK(sources[searched[0]].id == "0");
    CHECK(sources[searched[2]].id == "13");

    // Local is the Local source by id even with another language.
    CHECK(isLocalSource(src("0", "Local", "en")));
    CHECK(sourceLanguage(src("0", "Local", "en")) == "localsourcelang");
}

TEST_CASE("browse: config section") {
    AppConfig fresh;
    CHECK(fresh.browse.askCategories);
    CHECK(fresh.browse.hiddenLanguages.empty());
    CHECK(fresh.browse.lastSourceId.empty());

    AppConfig config;
    config.browse.hiddenLanguages = {"ja", "fr"};
    config.browse.askCategories = false;
    config.browse.lastSourceId = "1105946461371740061";
    const ConfigLoad back = loadAppConfig(saveAppConfig(config));
    CHECK(back.problems.empty());
    CHECK(back.config.browse.hiddenLanguages == std::vector<std::string>{"ja", "fr"});
    CHECK_FALSE(back.config.browse.askCategories);
    CHECK(back.config.browse.lastSourceId == "1105946461371740061");

    // Old files have no section: the defaults. Bad entries are skipped.
    CHECK(loadAppConfig(R"({"schemaVersion":1})").config.browse.askCategories);
    const ConfigLoad bad = loadAppConfig(
        R"({"schemaVersion":1,"browse":{"hiddenLanguages":["ja",5,"","ja","0123456789012345678901234567890123"]}})");
    CHECK(bad.config.browse.hiddenLanguages == std::vector<std::string>{"ja"});
    CHECK_FALSE(bad.problems.empty());
    CHECK_FALSE(loadAppConfig(R"({"schemaVersion":1,"browse":{"hiddenLanguages":"ja"}})").problems.empty());
}

TEST_CASE("browse: profile limits") {
    const RuntimeProfile full = makeRuntimeProfile(LaunchMode::Application, 3184 * kMiB, 100 * kMiB);
    CHECK(full.browseMaxItems == 1000);
    CHECK(full.sourcesMax == 500);
    CHECK(full.sourceFiltersMax == 300);
    CHECK(full.filterValuesMax == 500);
    CHECK(full.filterNameMaxBytes == 200);
    CHECK(full.filterTextMaxChars == 64);
    CHECK(full.globalSearchConcurrency == 3);
    CHECK(full.globalSearchCovers == 5);
    CHECK(full.globalSearchSourcesMax == 100);
    const RuntimeProfile applet = makeRuntimeProfile(LaunchMode::Applet, 400 * kMiB, 100 * kMiB);
    CHECK(applet.globalSearchConcurrency == 1);
    CHECK(applet.browseMaxItems < full.browseMaxItems);
}

TEST_CASE("browse: documents and requests") {
    for (const char* name : {"Sources", "SourceFilters", "BrowseSource", "LibraryDuplicates", "AddToLibrary",
                             "RemoveFromLibrary", "SetCategoryDefault"}) {
        CHECK_MESSAGE(!gql::document(name).text.empty(), name);
    }
    CHECK_FALSE(gql::sources().mutation);
    CHECK_FALSE(gql::sourceFilters("1").mutation);
    CHECK(gql::sourceFilters("1").variables["id"] == "1");

    const nlohmann::json changes = nlohmann::json::parse(R"([{"position":2,"selectState":2}])");
    const GqlRequest popular = gql::browseSource("12", gql::BrowseKind::Popular, 0, "comet", changes);
    CHECK(popular.mutation);  // fetchSourceManga makes rows: never repeated
    CHECK(popular.variables["type"] == "POPULAR");
    CHECK(popular.variables["page"] == 1);  // pages start at 1
    CHECK_FALSE(popular.variables.contains("query"));
    CHECK_FALSE(popular.variables.contains("filters"));
    const GqlRequest search = gql::browseSource("12", gql::BrowseKind::Search, 3, "comet", changes);
    CHECK(search.variables["type"] == "SEARCH");
    CHECK(search.variables["page"] == 3);
    CHECK(search.variables["query"] == "comet");
    CHECK(search.variables["filters"] == changes);
    const GqlRequest bare = gql::browseSource("12", gql::BrowseKind::Search, 1, "", nlohmann::json::array());
    CHECK_FALSE(bare.variables.contains("query"));
    CHECK_FALSE(bare.variables.contains("filters"));
    CHECK(gql::browseSource("12", gql::BrowseKind::Latest, 1, "", {}).variables["type"] == "LATEST");

    CHECK(gql::libraryDuplicates("100%_x").variables["title"] == "100\\%\\_x");
    const GqlRequest add = gql::addToLibrary(7, {0, 2, 3});
    CHECK(add.mutation);
    CHECK(add.variables["categories"] == nlohmann::json::array({2, 3}));
    CHECK(gql::addToLibrary(7, {}).variables["categories"] == nlohmann::json::array());
    CHECK(gql::removeFromLibrary(7).variables["id"] == 7);
    CHECK(gql::setCategoryDefault(2, true).variables["on"] == true);
}

TEST_CASE("browse: answers") {
    auto sources = gql::readSources(answer(R"j({"data":{"sources":{"nodes":[
        {"id":"0","name":"Local source","displayName":"Local source","lang":"localsourcelang","iconUrl":"/api/v1/extension/icon/local",
         "supportsLatest":true,"isConfigurable":false,"contentWarning":"SAFE"},
        {"id":"6796814882383758416","name":"SNX Mature","displayName":"SNX Mature (EN)","lang":"en","iconUrl":"/i",
         "supportsLatest":true,"isConfigurable":false,"contentWarning":"NSFW"},
        {"name":"no id"}],"totalCount":3}}})j"), 10);
    REQUIRE(sources.ok());
    REQUIRE(sources.value().size() == 2);
    CHECK(sources.value()[0].lang == "localsourcelang");
    CHECK(sources.value()[1].contentWarning == ContentWarning::Nsfw);
    CHECK(sources.value()[1].supportsLatest);
    CHECK(contentWarningFromName("MIXED") == ContentWarning::Mixed);
    CHECK(contentWarningName(ContentWarning::Nsfw) == "NSFW");
    CHECK(gql::readSources(answer(R"({"data":{"sources":{"nodes":[{"id":"1"},{"id":"2"}]}}})"), 1).value().size() == 1);

    auto page = gql::readBrowseSource(answer(R"({"data":{"fetchSourceManga":{"mangas":[
        {"id":158,"title":"Comet Courier","thumbnailUrl":"/api/v1/manga/158/thumbnail","inLibrary":false,"initialized":false},
        {"id":159,"title":"Blank","thumbnailUrl":null,"inLibrary":true,"initialized":true}],"hasNextPage":true}}})"));
    REQUIRE(page.ok());
    REQUIRE(page.value().manga.size() == 2);
    CHECK_FALSE(page.value().manga[0].initialized);
    CHECK(page.value().manga[1].thumbnailUrl.empty());
    CHECK(page.value().manga[1].inLibrary);
    CHECK(page.value().hasNextPage);

    // A source failure: data null, the error cut to its message.
    auto failed = gql::readBrowseSource(answer(
        R"({"data":{"fetchSourceManga":null},"errors":[{"message":"Exception while fetching data (/fetchSourceManga) : SNX test failure: search\njava.io.IOException: SNX test failure: search\n\tat snx.Main","path":["fetchSourceManga"]}]})"));
    REQUIRE_FALSE(failed.ok());
    CHECK(failed.error().kind == ErrorKind::Server);
    CHECK(failed.error().message == "SNX test failure: search");

    auto dupes = gql::readLibraryDuplicates(answer(R"j({"data":{"mangas":{"nodes":[
        {"id":3,"title":"Paper Garden","source":{"id":"1","name":"SNX Test","displayName":"SNX Test (EN)"}}]}}})j"));
    REQUIRE(dupes.ok());
    REQUIRE(dupes.value().size() == 1);
    CHECK(dupes.value()[0].source->displayName == "SNX Test (EN)");

    // The trap: the second part's inLibrary may say false; inLibrary comes from the first.
    auto added = gql::readAddToLibrary(answer(R"({"data":{
        "added":{"manga":{"id":158,"inLibrary":true,"inLibraryAt":1763370000}},
        "categories":{"manga":{"id":158,"inLibrary":false,"categories":{"nodes":[{"id":1,"name":"Reading","order":1}]}}}}})"));
    REQUIRE(added.ok());
    CHECK(added.value().inLibrary);
    CHECK(added.value().inLibraryAt == 1763370000);
    REQUIRE(added.value().categories.size() == 1);
    CHECK(added.value().categories[0].name == "Reading");
    CHECK_FALSE(gql::readAddToLibrary(answer(R"({"data":{"added":{"manga":{"id":158,"inLibrary":true}},"categories":null},
        "errors":[{"message":"boom"}]})")).ok());

    auto removed = gql::readRemoveFromLibrary(answer(R"({"data":{"updateManga":{"manga":{"id":158,"inLibrary":false}}}})"));
    REQUIRE(removed.ok());
    CHECK_FALSE(removed.value().inLibrary);
    auto flagged = gql::readSetCategoryDefault(
        answer(R"({"data":{"updateCategory":{"category":{"id":2,"name":"Long series","order":2,"default":true}}}})"));
    REQUIRE(flagged.ok());
    CHECK(flagged.value().isDefault);

    // Manga::initialized: true unless a query says otherwise.
    CHECK(fromJson<Manga>(nlohmann::json::parse(R"({"id":1})")).value().initialized);
    CHECK_FALSE(fromJson<Manga>(nlohmann::json::parse(R"({"id":1,"initialized":false})")).value().initialized);
}

}  // TEST_SUITE
