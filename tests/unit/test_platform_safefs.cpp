// platform::SafeFs (docs/SAFETY.md S4/S5) on the PC, in a fresh temp folder per test. Same doctest suite as test_platform.cpp.

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "platform/SafeFs.hpp"

namespace {

// A temp folder that is removed again at the end of the test. Removing uses SafeFs too, rooted one level up.
struct TempRoot {
    std::string parent;
    std::string root;
    TempRoot() {
        char pattern[] = "/tmp/snx-safefs-XXXXXX";
        const char* made = ::mkdtemp(pattern);
        REQUIRE(made != nullptr);
        parent = std::string(made) + "/";
        root = parent + "data/";
        REQUIRE(::mkdir(root.c_str(), 0755) == 0);
    }
    ~TempRoot() {
        snx::platform::SafeFs(parent).removeTree("data");
        ::rmdir(parent.c_str());
    }
};

bool onDisk(const std::string& path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0;
}

std::string slurp(const std::string& path) {
    std::string data;
    if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
        char buf[256];
        std::size_t n = 0;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
            data.append(buf, n);
        }
        std::fclose(f);
    }
    return data;
}

std::vector<std::string> sorted(std::vector<std::string> names) {
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<std::string> gRefusals;
void recordRefusal(std::string_view path, std::string_view) {
    gRefusals.emplace_back(path);
}

}  // namespace

TEST_SUITE("platform") {

TEST_CASE("SafeFs: folders, atomic writes and backups") {
    TempRoot tmp;
    const snx::platform::SafeFs fs(tmp.root);
    CHECK(fs.root() == tmp.root);
    CHECK(fs.makeDirs(""));
    CHECK(fs.makeDirs("logs/crashes"));
    CHECK(onDisk(tmp.root + "logs/crashes"));
    CHECK(fs.makeDirs("logs/crashes"));  // already there is fine

    CHECK(fs.writeAtomic("config.json", "one"));
    CHECK(slurp(tmp.root + "config.json") == "one");
    CHECK_FALSE(onDisk(tmp.root + "config.json.tmp"));
    CHECK(fs.writeAtomic("config.json", "two", snx::platform::SafeFs::Backup::Keep));
    CHECK(slurp(tmp.root + "config.json") == "two");
    CHECK(slurp(tmp.root + "config.json.bak") == "one");
    CHECK(fs.writeAtomic("config.json", "three"));
    CHECK(slurp(tmp.root + "config.json") == "three");
    CHECK(slurp(tmp.root + "config.json.bak") == "one");  // only Backup::Keep touches the .bak
    CHECK(fs.readFile("config.json", 100) == std::optional<std::string>("three"));
    CHECK_FALSE(fs.readFile("config.json", 2).has_value());  // bigger than allowed
    CHECK_FALSE(fs.readFile("missing.json", 100).has_value());
    CHECK(fs.exists("config.json"));
    CHECK_FALSE(fs.exists("missing.json"));
}

TEST_CASE("SafeFs: append, rename, remove, list") {
    TempRoot tmp;
    const snx::platform::SafeFs fs(tmp.root);
    REQUIRE(fs.makeDirs("test"));
    CHECK(fs.append("test/events.jsonl", "a\n"));
    CHECK(fs.append("test/events.jsonl", "b\n"));
    CHECK(slurp(tmp.root + "test/events.jsonl") == "a\nb\n");

    CHECK(fs.writeAtomic("test/x.txt", "x"));
    CHECK(fs.writeAtomic("test/y.txt", "y"));
    CHECK(fs.renameFile("test/x.txt", "test/y.txt"));  // replaces y
    CHECK(slurp(tmp.root + "test/y.txt") == "x");
    CHECK_FALSE(onDisk(tmp.root + "test/x.txt"));
    CHECK(sorted(fs.listDir("test")) == std::vector<std::string>{"events.jsonl", "y.txt"});
    CHECK(fs.listDir("missing").empty());

    CHECK(fs.removeFile("test/y.txt"));
    CHECK_FALSE(fs.removeFile("test/y.txt"));  // already gone
    CHECK(fs.listDir("test") == std::vector<std::string>{"events.jsonl"});
}

TEST_CASE("SafeFs: removeTree and removeTempFiles") {
    TempRoot tmp;
    const snx::platform::SafeFs fs(tmp.root);
    REQUIRE(fs.makeDirs("cache/a/b"));
    CHECK(fs.writeAtomic("cache/a/b/page.bin", "p"));
    CHECK(fs.writeAtomic("cache/top.bin", "t"));
    CHECK(fs.writeAtomic("keep.json", "k"));
    CHECK(fs.removeTree("cache"));
    CHECK_FALSE(onDisk(tmp.root + "cache"));
    CHECK(onDisk(tmp.root + "keep.json"));
    CHECK(fs.removeTree("cache"));  // not there: nothing to do

    CHECK_FALSE(fs.removeTree(""));  // never the root itself
    CHECK_FALSE(fs.removeTree("."));
    CHECK_FALSE(fs.removeTree(".."));
    CHECK(onDisk(tmp.root + "keep.json"));

    REQUIRE(fs.makeDirs("cache"));
    CHECK(fs.writeAtomic("cache/one.tmp", "1"));
    CHECK(fs.writeAtomic("cache/two.tmp", "2"));
    CHECK(fs.writeAtomic("cache/done.bin", "3"));
    CHECK(fs.removeTempFiles("cache") == 2);
    CHECK(fs.listDir("cache") == std::vector<std::string>{"done.bin"});
}

TEST_CASE("SafeFs: removeTree doesn't follow a link out of the folder") {
    TempRoot tmp;
    const snx::platform::SafeFs fs(tmp.root);
    const snx::platform::SafeFs outside(tmp.parent);
    REQUIRE(outside.makeDirs("precious"));
    REQUIRE(outside.writeAtomic("precious/file.txt", "keep me"));
    REQUIRE(fs.makeDirs("cache"));
    REQUIRE(::symlink((tmp.parent + "precious").c_str(), (tmp.root + "cache/link").c_str()) == 0);
    CHECK(fs.removeTree("cache"));
    CHECK(slurp(tmp.parent + "precious/file.txt") == "keep me");
    CHECK(outside.removeTree("precious"));
}

TEST_CASE("SafeFs: every write outside the root is refused, and reported") {
    TempRoot tmp;
    const snx::platform::SafeFs fs(tmp.root);
    gRefusals.clear();
    snx::platform::SafeFs::setRefusalLogger(recordRefusal);
    CHECK_FALSE(fs.writeAtomic("../escape.txt", "x"));
    CHECK_FALSE(fs.append("../escape.txt", "x"));
    CHECK_FALSE(fs.makeDirs("../escape"));
    CHECK_FALSE(fs.renameFile("a.txt", "../escape.txt"));
    CHECK_FALSE(fs.removeFile("../data/../escape.txt"));
    CHECK_FALSE(fs.removeTree(".."));
    CHECK(fs.openLog("../escape.log") == nullptr);
    CHECK_FALSE(fs.startPieces("../escape.bin").isOpen());
    snx::platform::SafeFs::CrashFile crash;
    CHECK_FALSE(fs.prepareCrashFile(crash, "../escape.tmp", "logs/x.txt"));
    CHECK_FALSE(fs.prepareCrashFile(crash, "logs/x.tmp", "../escape.txt"));
    CHECK_FALSE(fs.resolve("/etc/passwd").has_value());
    CHECK_FALSE(onDisk(tmp.parent + "escape.txt"));
    CHECK_FALSE(onDisk(tmp.parent + "escape"));
    CHECK(gRefusals.size() >= 11);
    snx::platform::SafeFs::setRefusalLogger(nullptr);
}

TEST_CASE("SafeFs: a file written in pieces shows up only when committed") {
    TempRoot tmp;
    const snx::platform::SafeFs fs(tmp.root);
    REQUIRE(fs.makeDirs("cache"));
    {
        auto writer = fs.startPieces("cache/page-1.bin");
        REQUIRE(writer.isOpen());
        CHECK(writer.write("abc"));
        CHECK(writer.write("def"));
        CHECK(onDisk(tmp.root + "cache/page-1.bin.tmp"));
        CHECK_FALSE(onDisk(tmp.root + "cache/page-1.bin"));
        CHECK(writer.commit());
        CHECK_FALSE(writer.isOpen());
    }
    CHECK(slurp(tmp.root + "cache/page-1.bin") == "abcdef");
    CHECK_FALSE(onDisk(tmp.root + "cache/page-1.bin.tmp"));
    {
        auto writer = fs.startPieces("cache/page-2.bin");
        CHECK(writer.write("half"));
        // dropped without commit(): an interrupted write
    }
    CHECK_FALSE(onDisk(tmp.root + "cache/page-2.bin"));
    CHECK_FALSE(onDisk(tmp.root + "cache/page-2.bin.tmp"));
    auto moved = fs.startPieces("cache/page-3.bin");
    snx::platform::SafeFs::PieceWriter other = std::move(moved);
    CHECK_FALSE(moved.isOpen());
    CHECK(other.write("x"));
    other.abandon();
    CHECK_FALSE(onDisk(tmp.root + "cache/page-3.bin.tmp"));
}

TEST_CASE("SafeFs: the crash file is opened early and named only when a crash is written") {
    TempRoot tmp;
    const snx::platform::SafeFs fs(tmp.root);
    REQUIRE(fs.makeDirs("logs"));
    snx::platform::SafeFs::CrashFile file;
    REQUIRE(fs.prepareCrashFile(file, "logs/crash-pending.tmp", "logs/crash-1.txt"));
    CHECK(file.isOpen());
    CHECK(onDisk(tmp.root + "logs/crash-pending.tmp"));
    CHECK(std::string(file.finalPath()) == tmp.root + "logs/crash-1.txt");
    CHECK(file.write("record", 6));
    CHECK(file.finish());
    CHECK_FALSE(file.isOpen());
    CHECK(slurp(tmp.root + "logs/crash-1.txt") == "record");
    CHECK_FALSE(onDisk(tmp.root + "logs/crash-pending.tmp"));
    CHECK_FALSE(file.write("more", 4));
    CHECK_FALSE(file.finish());

    REQUIRE(fs.prepareCrashFile(file, "logs/crash-pending.tmp", "logs/crash-2.txt"));
    file.discard();  // the run ended without a crash
    CHECK_FALSE(onDisk(tmp.root + "logs/crash-pending.tmp"));
    CHECK_FALSE(onDisk(tmp.root + "logs/crash-2.txt"));
}

TEST_CASE("SafeFs: the log file starts empty") {
    TempRoot tmp;
    const snx::platform::SafeFs fs(tmp.root);
    REQUIRE(fs.makeDirs("logs"));
    REQUIRE(fs.writeAtomic("logs/app.log", "old run"));
    std::FILE* log = fs.openLog("logs/app.log");
    REQUIRE(log != nullptr);
    std::fputs("new run\n", log);
    std::fclose(log);
    CHECK(slurp(tmp.root + "logs/app.log") == "new run\n");
}

}  // TEST_SUITE("platform")
