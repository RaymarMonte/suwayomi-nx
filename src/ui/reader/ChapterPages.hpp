#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "api/SuwayomiClient.hpp"
#include "core/models/Chapter.hpp"
#include "image/ImageError.hpp"
#include "net/CancelToken.hpp"

// WP-18 (D59): where a chapter's pages come from. A chapter downloaded to the Switch opens from its files on the SD card,
// without fetchChapterPages; any other chapter opens as before. The page reader and the strip both open (and prefetch / join)
// chapters through openChapterPages(). No borealis: unit-tested in test_ui_reader.cpp and test_ui_strip.cpp.
namespace snx::ui {

struct PageSource {
    // The downloaded chapter's page files (SwitchDownloads::pageFiles): one per page, "" for a page the server listed in a
    // form the app can't use. Nothing: the chapter isn't on the Switch. Empty function: nothing is ever on the Switch.
    std::function<std::optional<std::vector<std::string>>(int chapterId)> files;
    // How long a downloaded chapter waits for its progress (chapterState) before it opens with what the chapter list said
    // (RuntimeProfile::switchOpenWaitMs). 0 or no `later`: it waits for the answer (or its error).
    int openWaitMs = 3000;
    std::function<void(int ms, std::function<void()>)> later;  // the wait's timer, on the main thread (brls::delay)
    // A downloaded page that is missing, cut short or won't decode (SwitchDownloads::markDamaged).
    std::function<void(int chapterId, int page)> onDamaged;
    // WP-18b (D61), a reader opened offline: a downloaded chapter opens at once at the Switch's merged position (what it
    // knows of the server's progress plus a save waiting in the queue, SwitchDownloads::offlineChapter), strip spot included,
    // without chapterState. Nothing from it: the listed chapter's progress. Empty: online, as above.
    std::function<std::optional<core::Chapter>(int chapterId)> merged;
};

// A page from the SD card is "file:" + its path inside the app's folder (image::loadId's name for it).
bool isDownloadedPage(const std::string& page);
std::string downloadedPagePath(const std::string& page);  // the path, "" if it isn't one

// What a downloaded page's failure shows ("This page's download is damaged."), and whether a failure means that.
extern const char* const kDamagedPageMessage;
bool isDamagedPageError(const image::ImageError& error);

// The chapter's page list. On the Switch: its files ("file:" + path), the listed chapter with the server's progress from
// chapterState (lastPageRead, isRead, lastReadAt, the strip spot, isBookmarked, isDownloaded), or, without an answer within
// openWaitMs or with an error, the listed chapter's lastPageRead and isRead (no strip spot); a later answer is ignored. No
// syncConflict. Offline (`merged` set): at once, from `merged`, no request. Else client.pages(). `listed` is the chapter as the reader's list knows it (only its id may be set).
api::ApiFuture<core::PageList> openChapterPages(api::SuwayomiClient& client, const PageSource& source,
                                                const core::Chapter& listed, api::PageFormat format,
                                                const net::CancelToken& cancel, net::Priority priority);

}  // namespace snx::ui
