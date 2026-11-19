#include "ui/reader/ChapterPages.hpp"

#include <utility>

namespace snx::ui {

namespace {
constexpr const char* kPrefix = "file:";
constexpr std::size_t kPrefixLength = 5;
}  // namespace

const char* const kDamagedPageMessage = "This page's download is damaged.";

bool isDownloadedPage(const std::string& page) {
    return page.size() > kPrefixLength && page.compare(0, kPrefixLength, kPrefix) == 0;
}

std::string downloadedPagePath(const std::string& page) {
    return isDownloadedPage(page) ? page.substr(kPrefixLength) : std::string();
}

bool isDamagedPageError(const image::ImageError& error) {
    // A file the loader read (no network involved): missing or empty (Corrupt), over the limits (TooLarge), not an image
    // the app decodes (Unsupported), or broken (Corrupt).
    return error.kind == image::ImageErrorKind::Corrupt || error.kind == image::ImageErrorKind::TooLarge ||
           error.kind == image::ImageErrorKind::Unsupported;
}

api::ApiFuture<core::PageList> openChapterPages(api::SuwayomiClient& client, const PageSource& source,
                                                const core::Chapter& listed, api::PageFormat format,
                                                const net::CancelToken& cancel, net::Priority priority) {
    std::optional<std::vector<std::string>> files = source.files ? source.files(listed.id) : std::nullopt;
    if (!files) {
        return client.pages(listed.id, format, cancel, priority);
    }
    core::PageList list;
    list.chapter = listed;
    list.chapter.pageCount = static_cast<int>(files->size());
    list.chapter.stripSpotNote.clear();  // only the server's answer has a trustworthy spot
    list.pages.reserve(files->size());
    for (const std::string& file : *files) {
        list.pages.push_back(file.empty() ? std::string() : kPrefix + file);
    }
    // The first of: the progress answer, the wait's end, an error. Promise::set keeps the first value only.
    net::Promise<api::ApiResult<core::PageList>> promise;
    api::ApiFuture<core::PageList> future = promise.future();
    client.chapterState(listed.id, cancel, priority)
        .then([promise, list](const api::ApiResult<core::ServerProgress>& answer) mutable {
            core::PageList withProgress = list;
            if (answer.ok()) {
                const core::ServerProgress& p = answer.value();
                withProgress.chapter.lastPageRead = p.page;
                withProgress.chapter.isRead = p.read;
                withProgress.chapter.lastReadAt = p.lastReadAt;
                withProgress.chapter.stripSpotNote = p.spotNote;
                withProgress.chapter.isBookmarked = p.bookmarked;
                withProgress.chapter.isDownloaded = p.downloaded;
            } else if (answer.error().kind == api::ApiErrorKind::Cancelled) {
                promise.set(answer.error());  // the reader left: it ignores this anyway
                return;
            }
            promise.set(std::move(withProgress));
        });
    if (source.later && source.openWaitMs > 0) {
        source.later(source.openWaitMs, [promise, list]() mutable { promise.set(list); });
    }
    return future;
}

}  // namespace snx::ui
