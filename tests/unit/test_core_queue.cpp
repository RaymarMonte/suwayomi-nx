// WP-15b: the waiting-save queue's rules and file (core/ProgressQueue.hpp), and the base ProgressSync keeps for it.
#include <algorithm>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "core/ProgressQueue.hpp"
#include "core/ProgressSync.hpp"
#include "core/StripMath.hpp"

using namespace snx::core;

namespace {

WaitingSave waiting(int chapterId, int page, ServerProgress base = {}) {
    WaitingSave e;
    e.serverUrl = "http://server:4567";
    e.chapterId = chapterId;
    e.mangaId = 1;
    e.page = page;
    e.base = base;
    e.mangaTitle = "Alpha";
    e.chapterName = "Chapter " + std::to_string(chapterId);
    e.madeAt = 1794534794;
    return e;
}

ServerProgress server(int page, bool read, std::int64_t lastReadAt, std::string note = {}) {
    ServerProgress s;
    s.page = page;
    s.read = read;
    s.lastReadAt = lastReadAt;
    s.spotNote = std::move(note);
    return s;
}

ProgressQueueLoad load(const std::string& text, std::size_t max = 100) {
    return loadProgressQueue(text, JsonLimits{64 * 1024, 64}, max);
}

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("ProgressQueue: one entry per chapter; a newer save replaces it, keeps the older base and moves to the end") {
        std::vector<WaitingSave> entries;
        CHECK(addWaitingSave(entries, waiting(3, 4, server(2, false, 100)), 100).empty());
        CHECK(addWaitingSave(entries, waiting(5, 1, server(0, false, 50)), 100).empty());
        WaitingSave newer = waiting(3, 7, server(4, false, 200));  // its base: after a save that worked meanwhile
        newer.errors = 2;
        CHECK(addWaitingSave(entries, newer, 100).empty());
        REQUIRE(entries.size() == 2);
        CHECK(entries[0].chapterId == 5);
        CHECK(entries[1].chapterId == 3);
        CHECK(entries[1].page == 7);
        CHECK(entries[1].base.page == 2);  // what the server had before the first of the two saves
        CHECK(entries[1].base.lastReadAt == 100);
        CHECK(entries[1].errors == 2);
        // Another server's (or user's) chapter 3 is another entry.
        WaitingSave other = waiting(3, 9);
        other.user = "snx";
        CHECK(addWaitingSave(entries, other, 100).empty());
        CHECK(entries.size() == 3);
    }

    TEST_CASE("ProgressQueue: the cap drops the oldest") {
        std::vector<WaitingSave> entries;
        for (int id = 1; id <= 3; ++id) {
            CHECK(addWaitingSave(entries, waiting(id, id), 3).empty());
        }
        const std::vector<WaitingSave> dropped = addWaitingSave(entries, waiting(4, 4), 3);
        REQUIRE(dropped.size() == 1);
        CHECK(dropped[0].chapterId == 1);
        REQUIRE(entries.size() == 3);
        CHECK(entries.front().chapterId == 2);
        CHECK(entries.back().chapterId == 4);
    }

    TEST_CASE("ProgressQueue: the file round trip keeps every field, one entry per line") {
        std::vector<WaitingSave> entries;
        WaitingSave strip = waiting(460, 5, server(2, false, 1794534794));
        strip.user = "snx";
        strip.read = true;
        strip.spot = 0.4375;
        strip.errors = 1;
        strip.mangaTitle = "Épsilon — 週刊";
        addWaitingSave(entries, waiting(1, 0), 100);
        addWaitingSave(entries, strip, 100);
        const std::string text = saveProgressQueue(entries);
        CHECK(text.find("\"version\": 1") != std::string::npos);
        CHECK(std::count(text.begin(), text.end(), '\n') == 4);  // "{...[", two entries, "]}"
        const ProgressQueueLoad loaded = load(text);
        CHECK(loaded.usable);
        CHECK(loaded.problems.empty());
        REQUIRE(loaded.entries.size() == 2);
        const WaitingSave& back = loaded.entries[1];
        CHECK(back.serverUrl == strip.serverUrl);
        CHECK(back.user == "snx");
        CHECK(back.chapterId == 460);
        CHECK(back.mangaId == 1);
        CHECK(back.page == 5);
        CHECK(back.read);
        REQUIRE(back.spot);
        CHECK(*back.spot == 0.4375);
        CHECK(back.base.page == 2);
        CHECK_FALSE(back.base.read);
        CHECK(back.base.lastReadAt == 1794534794);
        CHECK(back.mangaTitle == "Épsilon — 週刊");
        CHECK(back.chapterName == "Chapter 460");
        CHECK(back.madeAt == 1794534794);
        CHECK(back.errors == 1);
        CHECK_FALSE(loaded.entries[0].spot);
        CHECK(saveProgressQueue(loaded.entries) == text);
        // An empty queue is a usable file too.
        const ProgressQueueLoad empty = load(saveProgressQueue({}));
        CHECK(empty.usable);
        CHECK(empty.entries.empty());
    }

    TEST_CASE("ProgressQueue: a broken entry is skipped, the rest stays; a broken file isn't usable") {
        const std::string text = R"({"version": 1, "entries": [
            {"server": "http://s", "chapterId": 3, "page": 4, "base": {"page": 2, "read": false, "lastReadAt": "77"}},
            {"server": "", "chapterId": 4, "page": 1},
            {"server": "http://s", "page": 1},
            {"server": "http://s", "chapterId": 5, "page": -1},
            "not an entry",
            {"server": "http://s", "chapterId": 6, "page": 0, "spot": 7.5, "errors": 999, "unknown": true},
            {"server": "http://s", "chapterId": 3, "page": 9}
        ]})";
        const ProgressQueueLoad loaded = load(text);
        CHECK(loaded.usable);
        CHECK(loaded.problems.size() == 5);  // four skipped entries, one repeated chapter
        REQUIRE(loaded.entries.size() == 2);
        CHECK(loaded.entries[0].chapterId == 6);
        CHECK_FALSE(loaded.entries[0].spot);  // outside 0..1
        CHECK(loaded.entries[0].errors == 100);
        CHECK(loaded.entries[1].chapterId == 3);
        CHECK(loaded.entries[1].page == 9);  // the later one of the repeat

        CHECK_FALSE(load("").usable);
        CHECK_FALSE(load(R"({"version": 1, "entr)").usable);  // cut short (a power cut)
        CHECK_FALSE(load("[1, 2]").usable);
        CHECK_FALSE(load(R"({"version": 1})").usable);
        // Another version: read as version 1, with a note in the log.
        const ProgressQueueLoad newer = load(R"({"version": 2, "entries": []})");
        CHECK(newer.usable);
        CHECK(newer.problems.size() == 1);
    }

    TEST_CASE("ProgressQueue: limits (S6): the file's size, long texts, the entry cap at load") {
        // A file over the size limit isn't read at all.
        std::string big = R"({"version": 1, "entries": [)";
        big += std::string(70 * 1024, ' ');
        big += "]}";
        CHECK_FALSE(load(big).usable);
        // Long titles are cut at a character's start; a long URL is refused.
        WaitingSave longText = waiting(1, 0);
        longText.mangaTitle = std::string(300, 'a') + "é";
        longText.chapterName = std::string(255, 'b') + "é";  // 'é' is 2 bytes: it would end past 256
        std::vector<WaitingSave> entries;
        addWaitingSave(entries, longText, 100);
        CHECK(entries[0].mangaTitle.size() == kWaitingTextMaxBytes);
        CHECK(entries[0].chapterName == std::string(255, 'b'));
        const std::string url(300, 'u');
        CHECK(load(R"({"version": 1, "entries": [{"server": ")" + url + R"(", "chapterId": 1, "page": 0}]})").entries.empty());
        // More entries than the cap: the newest stay.
        std::vector<WaitingSave> five;
        for (int id = 1; id <= 5; ++id) {
            addWaitingSave(five, waiting(id, 0), 100);
        }
        const ProgressQueueLoad capped = load(saveProgressQueue(five), 3);
        REQUIRE(capped.entries.size() == 3);
        CHECK(capped.entries.front().chapterId == 3);
        CHECK(capped.problems.size() == 1);
        // fitProgressQueue drops the oldest until the file fits.
        const std::size_t one = saveProgressQueue({five[0]}).size();
        const std::vector<WaitingSave> gone = fitProgressQueue(five, one);
        CHECK(gone.size() == 4);
        REQUIRE(five.size() == 1);
        CHECK(five[0].chapterId == 5);
    }

    TEST_CASE("ProgressQueue: the decision: already, send (still at the base), the server moved on") {
        const ServerProgress base = server(2, false, 100);
        WaitingSave entry = waiting(3, 4, base);
        CHECK(decideWaitingSave(entry, base) == WaitingDecision::Send);
        CHECK(decideWaitingSave(entry, server(4, false, 150)) == WaitingDecision::Already);  // the save got through after all
        CHECK(decideWaitingSave(entry, server(4, true, 150)) == WaitingDecision::Already);   // and read: we never un-read
        CHECK(decideWaitingSave(entry, server(6, false, 150)) == WaitingDecision::ServerMoved);  // read on, elsewhere
        CHECK(decideWaitingSave(entry, server(2, false, 150)) == WaitingDecision::ServerMoved);  // same page, saved again
        CHECK(decideWaitingSave(entry, server(2, true, 100)) == WaitingDecision::ServerMoved);   // marked read elsewhere
        // Ours marks it read: the server must be read too to count as "already".
        entry.read = true;
        CHECK(decideWaitingSave(entry, server(4, false, 150)) == WaitingDecision::ServerMoved);
        CHECK(decideWaitingSave(entry, server(4, true, 150)) == WaitingDecision::Already);
        // A spot (the strip): "already" needs the note too.
        WaitingSave strip = waiting(3, 4, base);
        strip.spot = 0.25;
        CHECK(decideWaitingSave(strip, server(4, false, 150, stripSpotValue(4, 0.25))) == WaitingDecision::Already);
        CHECK(decideWaitingSave(strip, server(4, false, 150, stripSpotValue(4, 0.5))) == WaitingDecision::ServerMoved);
        CHECK(decideWaitingSave(strip, server(4, false, 150)) == WaitingDecision::ServerMoved);
        ServerProgress baseWithNote = base;
        baseWithNote.spotNote = stripSpotValue(2, 0.9);  // the base ignores the note
        CHECK(decideWaitingSave(strip, baseWithNote) == WaitingDecision::Send);
        // What goes out.
        const ReadProgress sent = progressOf(strip);
        CHECK(sent.chapterId == 3);
        CHECK(sent.lastPageRead == 4);
        CHECK_FALSE(sent.isRead);
        CHECK(sent.spot == 0.25);
        CHECK(std::string(waitingDecisionName(WaitingDecision::ServerMoved)) == "server moved");
    }

    TEST_CASE("ProgressSync: keeps the base across saves and failures, and hands out what isn't saved with it") {
        ProgressSync sync(3, 2, false, 5000, 60000, 0);
        sync.setServerLastReadAt(100);
        CHECK(sync.base().page == 2);
        CHECK(sync.base().lastReadAt == 100);
        CHECK_FALSE(sync.notSaved());

        sync.shown(4, false);
        auto progress = sync.due(6000);
        REQUIRE(progress);
        sync.sending(*progress, 6000);
        REQUIRE(sync.notSaved());  // on its way still counts as not saved
        ReadProgress answer = *progress;
        answer.lastReadAt = 200;
        sync.saved(*progress);
        sync.serverAnswered(answer);
        CHECK(sync.base().page == 4);
        CHECK(sync.base().lastReadAt == 200);
        CHECK_FALSE(sync.notSaved());

        sync.shown(6, false);
        progress = sync.due(12000);
        REQUIRE(progress);
        sync.sending(*progress, 12000);
        sync.failed(12000);
        const auto left = sync.notSaved();
        REQUIRE(left);
        CHECK(left->progress.lastPageRead == 6);
        CHECK(left->base.page == 4);  // the failure leaves the base where the last good answer put it
        CHECK(left->base.lastReadAt == 200);
    }

    TEST_CASE("ProgressSync: adopt makes a waiting save its own unsaved progress") {
        ProgressSync sync(3, 2, false, 5000, 60000, 0);
        sync.adopt(5, true, 0.5);
        const auto due = sync.due(0);  // isRead doesn't wait
        REQUIRE(due);
        CHECK(due->lastPageRead == 5);
        CHECK(due->isRead);
        CHECK(due->spot == 0.5);
    }
}
