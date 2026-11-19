#pragma once

#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "Fixtures.hpp"
#include "core/SwitchDownloads.hpp"
#include "platform/SafeFs.hpp"

// WP-18: a stand-in SD card for the reader tests: a fresh folder for a SafeFs (removed at the end), and a downloaded chapter's
// page files the way SwitchDownloads writes them (Alpha's JPEG; the pages in `broken` cut short).
struct SdCard {
    std::string parent;
    std::string root;
    SdCard() {
        char pattern[] = "/tmp/snx-sdcard-XXXXXX";
        const char* made = ::mkdtemp(pattern);
        REQUIRE(made != nullptr);
        parent = std::string(made) + "/";
        root = parent + "data/";
        REQUIRE(::mkdir(root.c_str(), 0755) == 0);
    }
    ~SdCard() {
        snx::platform::SafeFs(parent).removeTree("data");
        ::rmdir(parent.c_str());
    }
    SdCard(const SdCard&) = delete;
    SdCard& operator=(const SdCard&) = delete;
};

inline std::vector<std::string> writeDownloadedChapter(const snx::platform::SafeFs& fs, int mangaId, int chapterId, int pages,
                                                       const std::set<int>& broken = {}) {
    const std::string jpeg = readFixture("images/alpha-p1.jpg");
    const std::string dir = snx::core::switchChapterDir("0123456789abcdef", mangaId, chapterId);
    REQUIRE(fs.makeDirs(dir));
    std::vector<std::string> files;
    for (int i = 0; i < pages; ++i) {
        const std::string file = dir + "/" + snx::core::switchPageName(i, snx::core::kPageJpeg);
        REQUIRE(fs.writeAtomic(file, broken.count(i) > 0 ? jpeg.substr(0, jpeg.size() / 3) : jpeg));
        files.push_back(file);
    }
    return files;
}
