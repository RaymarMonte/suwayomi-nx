// core's safety parts (docs/SAFETY.md): the write jail's path rules (S4), the crash record and log ring (S1), the test config's
// drills. Same doctest suite as test_core.cpp.

#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "core/CrashRecord.hpp"
#include "core/SafePath.hpp"
#include "core/TestConfig.hpp"
#include "core/TestEvents.hpp"

namespace {

constexpr const char* kSwitchRoot = "sdmc:/switch/suwayomi-nx/";
constexpr const char* kDesktopRoot = "./data/";

std::string accepted(const char* root, const std::string& path, bool allowRoot = false) {
    const auto result = snx::core::jailPath(root, path, allowRoot);
    INFO("path: ", path, "  refused because: ", result.why);
    REQUIRE(result.path.has_value());
    CHECK(result.why.empty());
    return *result.path;
}

void refused(const char* root, const std::string& path) {
    const auto result = snx::core::jailPath(root, path);
    INFO("path: ", path, "  became: ", result.path.value_or("(nothing)"));
    CHECK_FALSE(result.path.has_value());
    CHECK_FALSE(result.why.empty());
}

}  // namespace

TEST_SUITE("core") {

TEST_CASE("write jail: paths inside the app's folder are accepted and spelled out in full") {
    struct Case {
        const char* path;
        const char* expected;  // after the root
    };
    const std::vector<Case> cases = {
        {"logs/app.log", "logs/app.log"},
        {"./logs//app.log", "logs/app.log"},
        {"logs/../logs/./app.log", "logs/app.log"},
        {"cache/a/b/../c.bin", "cache/a/c.bin"},
        {"sdmc:/switch/suwayomi-nx/logs/app.log", "logs/app.log"},
        {"SDMC:/SWITCH/Suwayomi-NX/logs/app.log", "logs/app.log"},  // FAT32/exFAT: the same folder
        {"/switch/suwayomi-nx/config.json", "config.json"},          // no device = sdmc:
        {"sdmc:/switch/suwayomi-nx/cache/../config.json", "config.json"},
        {"sdmc://switch///suwayomi-nx/./x", "x"},
        {"sdmc:/switch/other/../suwayomi-nx/x", "x"},
        {"logs/crash-20261025-153000-1.txt", "logs/crash-20261025-153000-1.txt"},
        {"config.json.bak", "config.json.bak"},
        {"cache/caf\xc3\xa9.jpg", "cache/caf\xc3\xa9.jpg"},  // UTF-8 names are fine
    };
    for (const Case& c : cases) {
        CHECK(accepted(kSwitchRoot, c.path) == std::string(kSwitchRoot) + c.expected);
    }
}

TEST_CASE("write jail: anything outside the app's folder is refused") {
    const std::vector<std::string> cases = {
        // Atmosphère, the bootloader, system config, hbmenu (SAFETY.md S4)
        "sdmc:/atmosphere/config/system_settings.ini",
        "sdmc:/atmosphere/contents/0100000000000000/exefs.nsp",
        "sdmc:/bootloader/hekate_ipl.ini",
        "sdmc:/config/sys-autopilot/config.ini",
        "sdmc:/hbmenu.nro",
        "/atmosphere/package3",
        "sdmc:/",
        // going up with ".."
        "../x",
        "logs/../../x",
        "..",
        "sdmc:/switch/suwayomi-nx/../../atmosphere/x",
        "sdmc:/switch/suwayomi-nx/../suwayomi-nx-evil/x",
        "sdmc:/../switch/suwayomi-nx/x",
        // look-alike folders
        "sdmc:/switch/suwayomi-nx-evil/x",
        "sdmc:/switch/suwayomi-nx.bak/x",
        "sdmc:/switch/suwayomi/x",
        "sdmc:/suwayomi-nx/x",
        // other devices and broken device names
        "romfs:/x",
        "save:/x",
        "sdmc2:/switch/suwayomi-nx/x",
        "sdmc:x",
        ":/x",
        "c:\\windows",
        // characters that mean something else on FAT or in Horizon's FS
        "logs\\..\\..\\x",
        "logs/a\nb",
        std::string("logs/a\0b", 8),
        "logs/a:b",
        "logs/a?b",
        "logs/a*b",
        "logs/a\"b",
        "logs/a|b",
        "logs/<x>",
        "logs/x.",
        "logs/x ",
        "logs/...",
        // the root itself is not a file
        "",
        ".",
        "./",
        "logs/..",
        "sdmc:/switch/suwayomi-nx",
        "sdmc:/switch/suwayomi-nx/",
        // the app never rewrites itself (D15)
        "suwayomi-nx.nro",
        "sdmc:/switch/suwayomi-nx/suwayomi-nx.NRO",
        "cache/other.nro",
        // sizes
        "logs/" + std::string(256, 'a'),
        std::string(770, 'a'),
    };
    for (const std::string& path : cases) {
        refused(kSwitchRoot, path);
    }
}

TEST_CASE("write jail: a long path is fine up to the limits") {
    CHECK(accepted(kSwitchRoot, "logs/" + std::string(255, 'a')).size() == std::strlen(kSwitchRoot) + 5 + 255);
    std::string deep;
    for (int i = 0; i < 30; i++) {
        deep += "folder" + std::to_string(i) + "/";
    }
    accepted(kSwitchRoot, deep + "file.bin");
}

TEST_CASE("write jail: the root itself only when asked for") {
    CHECK(accepted(kSwitchRoot, "", true) == kSwitchRoot);
    CHECK(accepted(kSwitchRoot, "sdmc:/switch/suwayomi-nx", true) == kSwitchRoot);
    CHECK(accepted(kSwitchRoot, "logs/..", true) == kSwitchRoot);
    CHECK_FALSE(snx::core::jailPath(kSwitchRoot, "..", true).path.has_value());
}

TEST_CASE("write jail: the desktop's relative ./data/ root") {
    CHECK(accepted(kDesktopRoot, "logs/app.log") == "./data/logs/app.log");
    CHECK(accepted(kDesktopRoot, "test/../test/events.jsonl") == "./data/test/events.jsonl");
    refused(kDesktopRoot, "../data/x");
    refused(kDesktopRoot, "/etc/passwd");               // an absolute path can't point into a relative root
    refused(kDesktopRoot, "/home/someone/data/x");
    refused(kDesktopRoot, "sdmc:/switch/suwayomi-nx/x");
}

TEST_CASE("write jail: an absolute desktop root (the unit tests' temp folders)") {
    const char* root = "/tmp/snx-test/";
    CHECK(accepted(root, "a/b") == "/tmp/snx-test/a/b");
    CHECK(accepted(root, "/tmp/snx-test/a/b") == "/tmp/snx-test/a/b");
    refused(root, "/tmp/other/a");
    refused(root, "/tmp/snx-test-evil/a");
}

TEST_CASE("write jail: a root without the trailing slash is a programming error and refuses everything") {
    CHECK_FALSE(snx::core::jailPath("sdmc:/switch/suwayomi-nx", "logs/x").path.has_value());
}

TEST_CASE("crash record: fixed text never writes past its buffer") {
    char buffer[8];
    snx::core::crashrecord::FixedText t(buffer, sizeof(buffer));
    t.text("hello").text(" world");
    CHECK(t.view() == "hello wo");
    CHECK(t.truncated());

    char big[64];
    snx::core::crashrecord::FixedText n(big, sizeof(big));
    n.dec(0).text(" ").dec(18446744073709551615ULL).text(" ").hex(0x2a).text(" ").hex(0x2a, 4).text(" ").clean("a\nb\x1b[0m");
    CHECK(n.view() == "0 18446744073709551615 0x2a 0x002a a b [0m");
    CHECK_FALSE(n.truncated());
}

TEST_CASE("crash record: addresses inside the app become BASE + offset") {
    char buffer[64];
    snx::core::crashrecord::FixedText t(buffer, sizeof(buffer));
    t.address(0x1000 + 0x234, 0x1000, 0x500).text(" ").address(0x2000, 0x1000, 0x500).text(" ").address(0x10, 0, 0);
    CHECK(t.view() == "BASE + 0x234 0x0000000000002000 0x0000000000000010");
}

TEST_CASE("log ring: keeps the newest lines in order and cuts long ones") {
    static snx::core::crashrecord::LogRing ring;  // static: it's 13 KB
    CHECK(ring.size() == 0);
    CHECK(ring.lineAt(0).empty());
    for (int i = 0; i < 100; i++) {
        ring.push("line " + std::to_string(i));
    }
    REQUIRE(ring.size() == snx::core::crashrecord::LogRing::kLines);
    CHECK(ring.lineAt(0) == "line 36");
    CHECK(ring.lineAt(63) == "line 99");
    CHECK(ring.lineAt(64).empty());
    ring.push(std::string(500, 'x'));
    CHECK(ring.lineAt(63).size() == snx::core::crashrecord::LogRing::kLineBytes);
}

TEST_CASE("crash record: the format, and reading it back") {
    namespace cr = snx::core::crashrecord;
    static cr::LogRing ring;
    ring.push("first line");
    ring.push("second\nline");  // a newline in a log line must not break the record's layout
    const std::uint64_t stack[] = {0x10000 + 0x40, 0x10000 + 0x80, 0xdead0000};
    cr::CrashInfo info;
    info.type = "cpu_exception";
    info.what = "data abort at 0x0 (null pointer?)";
    info.thread = "main";
    info.version = "0.0.1";
    info.flavor = "test";
    info.platform = "switch";
    info.mode = "application";
    info.buildId = "44401c13a4476f8343f5a4209d657ccd2c803b40";
    info.uptimeMs = 1234;
    info.hasResult = true;
    info.result = 0x2c3b67;  // MAKERESULT(359, 5661)
    info.hasRegisters = true;
    info.pc = 0x10000 + 0x20;
    info.lr = 0x10000 + 0x40;
    info.sp = 0x7ff000;
    info.far = 0;
    info.esr = 0x92000046;
    info.moduleBase = 0x10000;
    info.moduleSize = 0x1000;
    info.stack = stack;
    info.stackCount = 3;
    info.log = &ring;

    static char buffer[16384];
    const std::size_t n = cr::formatCrashRecord(info, buffer, sizeof(buffer));
    const std::string text(buffer, n);
    CHECK(text.rfind("suwayomi-nx crash record 1\n", 0) == 0);
    CHECK(text.find("type: cpu_exception\n") != std::string::npos);
    CHECK(text.find("version: 0.0.1 (test build)\n") != std::string::npos);
    CHECK(text.find("result: 0x002c3b67 (module 359, description 5661)\n") != std::string::npos);
    CHECK(text.find("module: base 0x0000000000010000 size 0x1000\n") != std::string::npos);
    CHECK(text.find("  pc: BASE + 0x20\n") != std::string::npos);
    CHECK(text.find("  #0 BASE + 0x40\n") != std::string::npos);
    CHECK(text.find("  #2 0x00000000dead0000\n") != std::string::npos);
    CHECK(text.find("  second line\n") != std::string::npos);
    CHECK(text.size() > 20);
    CHECK(text.substr(text.size() - 20) == "end of crash record\n");

    const auto summary = cr::parseCrashRecord(text);
    REQUIRE(summary.has_value());
    CHECK(summary->type == "cpu_exception");
    CHECK(summary->what == "data abort at 0x0 (null pointer?)");
    CHECK(summary->thread == "main");
    CHECK(summary->platform == "switch");
    CHECK(summary->buildId == "44401c13a4476f8343f5a4209d657ccd2c803b40");
    CHECK(summary->complete);

    // Cut short (the SD card filled up, or the console lost power): it's still a record, but not a complete one.
    const auto cut = cr::parseCrashRecord(text.substr(0, 100));
    REQUIRE(cut.has_value());
    CHECK_FALSE(cut->complete);
    // A buffer too small for everything gets no footer, so it can't claim to be complete.
    char small[200];
    const std::size_t m = cr::formatCrashRecord(info, small, sizeof(small));
    CHECK(m == sizeof(small));
    CHECK_FALSE(cr::parseCrashRecord(std::string(small, m))->complete);

    CHECK_FALSE(cr::parseCrashRecord("hello").has_value());
    CHECK_FALSE(cr::parseCrashRecord("").has_value());
}

TEST_CASE("crash record: result code parts and the GPU module") {
    namespace cr = snx::core::crashrecord;
    const std::uint32_t gpu = (5661u << 9) | 359u;  // MAKERESULT(359, 5661)
    CHECK(cr::resultModule(gpu) == cr::kGpuResultModule);
    CHECK(cr::resultDescription(gpu) == 5661);
    CHECK(cr::resultModule(0x3d59) == 345);  // libnx's "sm init failed": not the GPU
}

TEST_CASE("crash record: file names") {
    namespace cr = snx::core::crashrecord;
    std::tm utc{};
    utc.tm_year = 2026 - 1900;
    utc.tm_mon = 9;
    utc.tm_mday = 25;
    utc.tm_hour = 15;
    utc.tm_min = 30;
    utc.tm_sec = 7;
    CHECK(cr::crashFileName(utc, 1) == "crash-20261025-153007-1.txt");
    CHECK(cr::isCrashFileName("crash-20261025-153007-1.txt"));
    CHECK_FALSE(cr::isCrashFileName("crash-pending.tmp"));
    CHECK_FALSE(cr::isCrashFileName("app.log"));
    CHECK_FALSE(cr::isCrashFileName("crash-.txt"));
}

TEST_CASE("test config: drills by name") {
    namespace tc = snx::core::testconfig;
    for (const auto& [drill, name] : tc::drills()) {
        CHECK(tc::drillFromName(name) == drill);
        CHECK(tc::drillName(drill) == name);
        CHECK(tc::parse(std::string(R"({"drill":")") + std::string(name) + "\"}").drill == drill);
    }
    CHECK(tc::drills().size() == 10);
    CHECK(tc::drillName(tc::Drill::None) == "none");
    CHECK(tc::drillFromName("no-such-drill") == tc::Drill::None);
}

TEST_CASE("test config: bad input never throws") {
    namespace tc = snx::core::testconfig;
    CHECK(tc::parse("{}").drill == tc::Drill::None);
    CHECK(tc::parse("{}").problems.empty());
    CHECK(tc::parse(R"({"server":{"url":"http://x"}})").problems.empty());  // other settings are WP-08's
    CHECK(tc::parse("not json").problems.size() == 1);
    CHECK(tc::parse("[1,2]").problems.size() == 1);
    CHECK(tc::parse(R"({"drill":5})").problems.size() == 1);
    const auto unknown = tc::parse(R"({"drill":"format-the-sd-card"})");
    CHECK(unknown.drill == tc::Drill::None);
    REQUIRE(unknown.problems.size() == 1);
    CHECK(unknown.problems[0].find("format-the-sd-card") != std::string::npos);
}

TEST_CASE("app_start reports safe mode and the crash behind it") {
    snx::core::testevents::AppStart info;
    info.safeMode = true;
    info.lastCrash = "logs/crashes/crash-20261025-153007-1.txt";
    const std::string line = snx::core::testevents::appStart(5, info);
    CHECK(line.find(R"("safeMode":true)") != std::string::npos);
    CHECK(line.find(R"("lastCrash":"logs/crashes/crash-20261025-153007-1.txt")") != std::string::npos);
}

}  // TEST_SUITE("core")
