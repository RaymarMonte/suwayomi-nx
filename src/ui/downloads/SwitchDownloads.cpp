#include "ui/downloads/SwitchDownloads.hpp"

#include <algorithm>
#include <utility>

#include "image/Decode.hpp"

namespace snx::ui {

namespace {

// retryMs, doubled per failure in a row, at most retryMaxMs (D45's back-off).
int backoffMs(int retryMs, int failures, int retryMaxMs) {
    long long wait = std::max(retryMs, 1);
    for (int i = 1; i < failures && wait < retryMaxMs; ++i) {
        wait *= 2;
    }
    return static_cast<int>(std::min<long long>(wait, std::max(retryMaxMs, 1)));
}

// A server error's first line ("Exception while fetching data (/fetchChapterPages) : SNX test failure: pages" and then a
// long Java stack): what a row may show.
std::string firstLine(const std::string& text) {
    const std::size_t end = text.find('\n');
    std::string line = end == std::string::npos ? text : text.substr(0, end);
    if (line.size() > core::kSwitchTextMaxBytes) {
        line.resize(core::kSwitchTextMaxBytes);
    }
    return line;
}

char formatLetter(image::ImageFormat format) {
    switch (format) {
        case image::ImageFormat::Jpeg: return core::kPageJpeg;
        case image::ImageFormat::Png: return core::kPagePng;
        case image::ImageFormat::Webp: return core::kPageWebp;
        case image::ImageFormat::Gif: return core::kPageGif;
        case image::ImageFormat::Unknown: break;
    }
    return core::kPageUnusable;
}

bool serverAwayKind(api::ApiErrorKind kind) {
    return kind == api::ApiErrorKind::Offline || kind == api::ApiErrorKind::Timeout;
}

constexpr const char* kWriteError = "Can't write to the SD card.";

}  // namespace

SwitchDownloads::Limits SwitchDownloads::limitsFrom(const core::RuntimeProfile& profile) {
    Limits limits;
    limits.pagesAtOnce = std::max(profile.switchDownloadPagesAtOnce, 1);
    limits.pageTries = std::max(profile.switchDownloadPageTries, 1);
    limits.retryMs = profile.switchDownloadRetryMs;
    limits.serverRetryMs = profile.progressSyncMs;
    limits.serverRetryMaxMs = profile.progressRetryMaxMs;
    limits.authRetryMs = profile.imageAuthRetryMs;
    limits.maxImageBytes = profile.maxImageBytes;
    limits.imageTimeoutMs = profile.httpImageTimeoutMs;
    limits.maxImageSide = profile.maxImageSide;
    limits.queueMax = static_cast<std::size_t>(std::max(profile.switchQueueMax, 1));
    limits.saveDelayMs = profile.configSaveDelayMs;
    limits.minSdFree = profile.minSdFreeBytes;
    limits.defaultLimit = profile.switchDownloadLimitBytes;
    return limits;
}

const char* SwitchDownloads::chapterStateName(ChapterState state) {
    switch (state) {
        case ChapterState::None: return "none";
        case ChapterState::Queued: return "queued";
        case ChapterState::Downloading: return "downloading";
        case ChapterState::Paused: return "paused";
        case ChapterState::Failed: return "failed";
        case ChapterState::OnSwitch: return "on_switch";
        case ChapterState::Damaged: return "damaged";
    }
    return "none";
}

const char* SwitchDownloads::actionName(Action action) {
    switch (action) {
        case Action::Enqueue: return "enqueue";
        case Action::Remove: return "remove";
        case Action::Top: return "top";
        case Action::Bottom: return "bottom";
        case Action::Pause: return "pause";
        case Action::Resume: return "resume";
        case Action::Retry: return "retry";
        case Action::RetryFailed: return "retry_failed";
        case Action::Clear: return "clear";
        case Action::Delete: return "delete";
        case Action::DeleteManga: return "delete_manga";
        case Action::DeleteAfterReading: return "delete_after_reading";
    }
    return "enqueue";
}

SwitchDownloads::SwitchDownloads(api::SuwayomiClient& client, net::HttpClient& http, DownloadStore& store, Limits limits,
                                 Hooks hooks)
    : client_(client), http_(http), store_(store), limits_(limits), hooks_(std::move(hooks)), limit_(limits.defaultLimit) {}

SwitchDownloads::~SwitchDownloads() {
    *alive_ = false;
    cancel_.cancel();
}

void SwitchDownloads::say(bool warning, const std::string& line) const {
    if (hooks_.log) {
        hooks_.log(warning, line);
    }
}

int SwitchDownloads::addListener(Listener listener) {
    const int id = nextListener_++;
    listeners_.emplace_back(id, std::move(listener));
    return id;
}

void SwitchDownloads::removeListener(int id) {
    listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(), [id](const auto& e) { return e.first == id; }),
                     listeners_.end());
}

void SwitchDownloads::notify(const Change& change) {
    const auto copy = listeners_;  // a listener may remove itself
    for (const auto& entry : copy) {
        entry.second(change);
    }
}

void SwitchDownloads::changed() {
    Change change;
    change.state = true;
    notify(change);
}

void SwitchDownloads::report(Action action, int chapters, bool ok, const std::string& error) {
    Change change;
    change.state = true;
    change.action = action;
    change.actionChapters = chapters;
    change.actionOk = ok;
    change.actionError = error;
    notify(change);
}

void SwitchDownloads::queueChanged() {
    if (key_.empty()) {
        return;
    }
    store_.setQueue(key_, queue_);
    if (queueWriteScheduled_) {
        return;  // the write already waiting takes the newest text
    }
    queueWriteScheduled_ = true;
    const std::weak_ptr<bool> alive = alive_;
    hooks_.later(limits_.saveDelayMs, [this, alive] {
        if (alive.expired() || !*alive.lock()) {
            return;
        }
        queueWriteScheduled_ = false;
        store_.writeQueueSoon();
    });
}

// --- The app's life ---

void SwitchDownloads::setServer(const std::string& url) {
    const std::string normal = core::downloadServerUrl(url);
    if (normal == url_) {
        return;
    }
    stopRunning();
    store_.flush();  // the old server's queue as it was
    url_ = normal;
    key_.clear();
    urls_.clear();
    serverWritten_ = false;
    loaded_ = false;
    connected_ = false;
    queue_ = {};
    manga_.clear();
    damagedManga_.clear();
    partial_.clear();
    progress_.clear();
    pendingManga_.clear();
    covers_.clear();
    coverTried_.clear();
    wait_ = core::SwitchWait::None;
    serverFailures_ = 0;
    freeMeasured_.reset();
    writtenSince_ = 0;
    changed();
    if (url_.empty()) {
        return;
    }
    const std::uint64_t ticket = ++scanTicket_;
    const std::weak_ptr<bool> alive = alive_;
    store_.scan(url_, [this, alive, ticket](DownloadStore::Scan scan) {
        if (alive.expired() || !*alive.lock() || ticket != scanTicket_) {
            return;
        }
        key_ = scan.key;
        urls_ = scan.urls;
        manga_ = std::move(scan.manga);
        damagedManga_ = std::move(scan.damagedManga);
        queue_ = std::move(scan.queue);
        partial_ = std::move(scan.partial);
        covers_ = std::move(scan.covers);
        if (scan.freeKnown) {
            freeMeasured_ = scan.freeBytes;
        }
        freeMs_ = scan.freeMs;
        for (const std::string& problem : scan.problems) {
            say(true, problem);
        }
        std::size_t chapters = 0;
        for (const core::SwitchManga& m : manga_) {
            chapters += m.chapters.size();
        }
        say(false, "switch downloads: " + std::to_string(chapters) + " chapters of " + std::to_string(manga_.size()) +
                       " manga on this Switch, " + std::to_string(queue_.entries.size()) + " queued" +
                       (queue_.paused ? " (paused)" : "") + ", " + core::bytesText(used()) + " used, folder " + key_ +
                       (scan.orphansRemoved > 0 ? ", " + std::to_string(scan.orphansRemoved) + " leftovers deleted" : "") +
                       "; freeBytes() took " + std::to_string(scan.freeMs) + " ms");
        loaded_ = true;
        changed();
        kick();
    });
}

void SwitchDownloads::connected() {
    connected_ = true;
    if (wait_ == core::SwitchWait::Server || wait_ == core::SwitchWait::Login) {
        wait_ = core::SwitchWait::None;
    }
    serverFailures_ = 0;
    changed();
    kick();
}

void SwitchDownloads::setFocused(bool focused) {
    if (focused == focused_) {
        return;
    }
    focused_ = focused;
    if (!focused) {
        stopRunning();  // S5: nothing written in the background; those pages come again
        store_.setWritesAllowed(false);
        store_.flush();
    } else if (!stopped_) {
        store_.setWritesAllowed(true);
    }
    changed();
    kick();
}

void SwitchDownloads::setLimit(std::uint64_t bytes) {
    limit_ = bytes;
    if (wait_ == core::SwitchWait::LimitFull) {
        wait_ = core::SwitchWait::None;
    }
    changed();
    kick();
}

void SwitchDownloads::shutdown() {
    stopped_ = true;
    stopRunning();
    store_.setWritesAllowed(false);
    store_.flush();
}

// --- What screens read ---

const core::SwitchQueueEntry* SwitchDownloads::entry(int chapterId) const {
    return core::findEntry(queue_, chapterId);
}

core::SwitchQueueEntry* SwitchDownloads::entry(int chapterId) {
    for (core::SwitchQueueEntry& e : queue_.entries) {
        if (e.chapter.id == chapterId) {
            return &e;
        }
    }
    return nullptr;
}

const core::SwitchManga* SwitchDownloads::mangaOf(int mangaId) const {
    for (const core::SwitchManga& m : manga_) {
        if (m.id == mangaId) {
            return &m;
        }
    }
    return nullptr;
}

core::SwitchManga* SwitchDownloads::mangaOf(int mangaId) {
    for (core::SwitchManga& m : manga_) {
        if (m.id == mangaId) {
            return &m;
        }
    }
    return nullptr;
}

const core::SwitchManga* SwitchDownloads::manga(int mangaId) const {
    return mangaOf(mangaId);
}

bool SwitchDownloads::anyQueuedFor(int mangaId) const {
    return std::any_of(queue_.entries.begin(), queue_.entries.end(),
                       [mangaId](const core::SwitchQueueEntry& e) { return e.mangaId == mangaId; });
}

SwitchDownloads::ChapterStatus SwitchDownloads::chapterState(int chapterId) const {
    ChapterStatus status;
    if (const core::SwitchQueueEntry* e = entry(chapterId)) {
        status.pages = e->pageCount;
        if (const auto found = progress_.find(chapterId); found != progress_.end()) {
            status.pagesDone = found->second.first;
            status.pages = found->second.second;
        }
        if (e->failed) {
            status.state = ChapterState::Failed;
            status.failedPage = e->failedPage;
        } else if (running_ && running_->chapterId == chapterId) {
            status.state = ChapterState::Downloading;
        } else if (queue_.paused && status.pagesDone > 0) {
            status.state = ChapterState::Paused;
        } else {
            status.state = ChapterState::Queued;
        }
        return status;
    }
    for (const core::SwitchManga& m : manga_) {
        if (const core::SwitchChapter* c = m.chapter(chapterId)) {
            status.state = c->damaged.empty() ? ChapterState::OnSwitch : ChapterState::Damaged;
            status.pages = c->pages();
            status.pagesDone = c->pages();
            return status;
        }
    }
    return status;
}

int SwitchDownloads::failedCount() const {
    return static_cast<int>(std::count_if(queue_.entries.begin(), queue_.entries.end(),
                                          [](const core::SwitchQueueEntry& e) { return e.failed; }));
}

core::SwitchWait SwitchDownloads::wait() const {
    if (!focused_) {
        return core::SwitchWait::Background;
    }
    if (wait_ != core::SwitchWait::None) {
        return wait_;
    }
    if (!connected_ && !queue_.entries.empty()) {
        return core::SwitchWait::Server;  // until the main screen's server check worked
    }
    return core::SwitchWait::None;
}

core::SwitchQueueView SwitchDownloads::view() const {
    return core::switchQueueView(static_cast<int>(queue_.entries.size()), failedCount(), queue_.paused, wait());
}

std::vector<SwitchDownloads::MangaRow> SwitchDownloads::downloadedManga() const {
    std::vector<MangaRow> rows;
    for (const core::SwitchManga& m : manga_) {
        if (m.chapters.empty()) {
            continue;
        }
        MangaRow row;
        row.mangaId = m.id;
        row.title = m.title;
        row.thumbnailUrl = m.thumbnailUrl;
        if (m.coverBytes > 0) {
            row.coverFile = core::switchMangaDir(key_, m.id) + "/" + core::kSwitchCoverFile;
        }
        row.chapters = static_cast<int>(m.chapters.size());
        for (const core::SwitchChapter& c : m.chapters) {
            row.damagedChapters += c.damaged.empty() ? 0 : 1;
        }
        row.bytes = m.bytes();
        rows.push_back(std::move(row));
    }
    for (int id : damagedManga_) {
        MangaRow row;
        row.mangaId = id;
        row.title = "Manga " + std::to_string(id);
        for (const core::SwitchQueueEntry& e : queue_.entries) {
            if (e.mangaId == id && !e.mangaTitle.empty()) {
                row.title = e.mangaTitle;
                break;
            }
        }
        row.damaged = true;
        rows.push_back(std::move(row));
    }
    std::stable_sort(rows.begin(), rows.end(), [](const MangaRow& a, const MangaRow& b) { return a.title < b.title; });
    return rows;
}

std::string SwitchDownloads::pagePath(int mangaId, int chapterId, int index, char format) const {
    return core::switchChapterDir(key_, mangaId, chapterId) + "/" + core::switchPageName(index, format);
}

std::optional<std::vector<std::string>> SwitchDownloads::pageFiles(int chapterId) const {
    for (const core::SwitchManga& m : manga_) {
        if (const core::SwitchChapter* c = m.chapter(chapterId)) {
            std::vector<std::string> files;
            files.reserve(c->formats.size());
            for (std::size_t i = 0; i < c->formats.size(); ++i) {
                const char format = c->formats[i];
                files.push_back(format == core::kPageUnusable ? std::string()
                                                              : pagePath(m.id, c->id, static_cast<int>(i), format));
            }
            return files;
        }
    }
    return std::nullopt;
}

std::set<int> SwitchDownloads::skipIds() const {
    std::set<int> ids;
    for (const core::SwitchManga& m : manga_) {
        for (const core::SwitchChapter& c : m.chapters) {
            ids.insert(c.id);
        }
    }
    for (const core::SwitchQueueEntry& e : queue_.entries) {
        ids.insert(e.chapter.id);
    }
    return ids;
}

std::uint64_t SwitchDownloads::used() const {
    std::uint64_t total = 0;
    for (const core::SwitchManga& m : manga_) {
        total += m.bytes();
    }
    for (const auto& [id, bytes] : partial_) {
        total += bytes;
    }
    for (const auto& [id, m] : pendingManga_) {
        total += m.coverBytes;
    }
    for (const auto& [id, bytes] : covers_) {
        total += bytes;
    }
    return total;
}

std::optional<std::uint64_t> SwitchDownloads::freeNow() const {
    if (!freeMeasured_) {
        return std::nullopt;
    }
    return *freeMeasured_ - std::min(writtenSince_, *freeMeasured_);
}

SwitchDownloads::Space SwitchDownloads::space() const {
    return Space{used(), limit_, freeNow()};
}

std::optional<std::string> SwitchDownloads::takeError() {
    std::optional<std::string> error = std::move(pendingError_);
    pendingError_.reset();
    return error;
}

// --- The downloader ---

bool SwitchDownloads::holdsBack() const {
    return !loaded_ || stopped_ || !focused_ || !connected_ || queue_.paused || wait_ == core::SwitchWait::Server ||
           wait_ == core::SwitchWait::Login;
}

void SwitchDownloads::stopRunning() {
    if (!running_) {
        return;
    }
    ++run_;
    cancel_.cancel();
    cancel_ = net::CancelToken();
    running_.reset();
}

void SwitchDownloads::scheduleKick(int ms) {
    const std::uint64_t ticket = ++kickTicket_;
    const std::weak_ptr<bool> alive = alive_;
    hooks_.later(ms, [this, alive, ticket] {
        if (alive.expired() || !*alive.lock() || ticket != kickTicket_) {
            return;
        }
        if (wait_ == core::SwitchWait::Server || wait_ == core::SwitchWait::Memory) {
            wait_ = core::SwitchWait::None;
        }
        if (wait_ == core::SwitchWait::SpaceUnknown) {
            measureFree();  // asks again; its answer kicks
            return;
        }
        kick();
    });
}

void SwitchDownloads::kick() {
    if (holdsBack()) {
        return;
    }
    if (wait_ == core::SwitchWait::LimitFull || wait_ == core::SwitchWait::SdFull) {
        if (!spaceFor(0)) {
            return;  // still full: a delete or a higher limit kicks again
        }
    }
    if (running_) {
        if (running_->listed) {
            pump(running_->run);
        }
        return;
    }
    for (const core::SwitchQueueEntry& e : queue_.entries) {
        if (!e.failed) {
            startChapter(e);
            return;
        }
    }
}

void SwitchDownloads::measureFree() {
    const std::weak_ptr<bool> alive = alive_;
    store_.freeBytes([this, alive](std::optional<std::uint64_t> free, int ms) {
        if (alive.expired() || !*alive.lock()) {
            return;
        }
        freeMeasured_ = free;
        writtenSince_ = 0;
        freeMs_ = ms;
        if (wait_ == core::SwitchWait::SpaceUnknown || wait_ == core::SwitchWait::SdFull ||
            wait_ == core::SwitchWait::LimitFull) {
            wait_ = core::SwitchWait::None;  // checked again before the next page
            changed();
            kick();
        }
    });
}

bool SwitchDownloads::spaceFor(std::uint64_t bytes) {
    const core::SpaceCheck check = core::checkSpace(used(), bytes, limit_, freeNow(), limits_.minSdFree);
    core::SwitchWait wait = core::SwitchWait::None;
    switch (check) {
        case core::SpaceCheck::Fits: break;
        case core::SpaceCheck::LimitFull: wait = core::SwitchWait::LimitFull; break;
        case core::SpaceCheck::SdFull: wait = core::SwitchWait::SdFull; break;
        case core::SpaceCheck::Unknown: wait = core::SwitchWait::SpaceUnknown; break;
    }
    if (wait == core::SwitchWait::None) {
        if (wait_ == core::SwitchWait::LimitFull || wait_ == core::SwitchWait::SdFull ||
            wait_ == core::SwitchWait::SpaceUnknown) {
            wait_ = core::SwitchWait::None;
            changed();
        }
        return true;
    }
    if (wait_ != wait) {
        wait_ = wait;
        say(false, std::string("switch downloads: waiting (") + core::switchWaitName(wait) + "), " +
                       core::bytesText(used()) + " used");
        changed();
        if (wait == core::SwitchWait::SpaceUnknown) {
            scheduleKick(limits_.serverRetryMs);  // asks the SD card again later
        }
    }
    return false;
}

void SwitchDownloads::startChapter(const core::SwitchQueueEntry& e) {
    Running running;
    running.chapterId = e.chapter.id;
    running.mangaId = e.mangaId;
    running.run = run_;
    running.startedMs = hooks_.clockMs();
    running_ = std::move(running);
    measureFree();  // once per chapter (D59): what was written since is counted
    const std::uint64_t run = run_;
    if (mangaOf(e.mangaId) || pendingManga_.count(e.mangaId) > 0) {
        afterManga(run);
        changed();
        return;
    }
    // A manga with nothing on the Switch yet (after a restart): its details once, for manga.json.
    const std::weak_ptr<bool> alive = alive_;
    const int mangaId = e.mangaId;
    core::SwitchManga fallback;
    fallback.id = e.mangaId;
    fallback.title = e.mangaTitle;
    fallback.thumbnailUrl = e.thumbnailUrl;
    client_.manga(mangaId, cancel_, net::Priority::Background)
        .then([this, alive, run, mangaId, fallback](const api::ApiResult<core::Manga>& r) {
            if (alive.expired() || !*alive.lock() || !current(run)) {
                return;
            }
            if (!r.ok() && serverAwayKind(r.error().kind)) {
                serverAway(false);
                return;
            }
            if (!r.ok() && r.error().kind == api::ApiErrorKind::Auth) {
                serverAway(true);
                return;
            }
            putPending(r.ok() ? core::switchMangaFrom(r.value()) : fallback);
            afterManga(run);
        });
    changed();
}

void SwitchDownloads::ensureFolder() {
    if (!serverWritten_) {
        serverWritten_ = true;
        store_.writeServer(key_, urls_.empty() ? std::vector<std::string>{url_} : urls_, {});  // before any file (FIFO)
    }
}

void SwitchDownloads::afterManga(std::uint64_t run) {
    if (!current(run)) {
        return;
    }
    const int mangaId = running_->mangaId;
    core::SwitchManga* known = mangaOf(mangaId);
    core::SwitchManga* pending = pendingManga_.count(mangaId) > 0 ? &pendingManga_[mangaId] : nullptr;
    const core::SwitchManga* info = known ? known : pending;
    if (!info || info->coverBytes > 0 || info->thumbnailUrl.empty() || coverTried_.count(mangaId) > 0) {
        requestPages(run);
        return;
    }
    // The cover, once per manga and run: a cover that fails is no cover, never a failed chapter.
    coverTried_.insert(mangaId);
    net::Request request;
    request.url = client_.absoluteUrl(info->thumbnailUrl);
    request.maxBodyBytes = limits_.maxImageBytes;
    request.timeoutMs = limits_.imageTimeoutMs;
    const std::weak_ptr<bool> alive = alive_;
    auto post = hooks_.post;
    http_.submit(std::move(request), net::Priority::Background, cancel_)
        .then([this, alive, post, run, mangaId](const net::HttpResult& result) {
            post([this, alive, run, mangaId, result] {
                if (alive.expired() || !*alive.lock() || !current(run)) {
                    return;
                }
                const bool good = result.ok() && result.value().status == 200 && !result.value().body.empty() &&
                                  image::sniffFormat(result.value().body) != image::ImageFormat::Unknown &&
                                  image::readHeader(result.value().body).ok();
                if (!good || !spaceFor(result.value().body.size())) {
                    requestPages(run);
                    return;
                }
                auto bytes = std::make_shared<const std::string>(result.value().body);
                ensureFolder();
                const std::uint64_t size = bytes->size();
                store_.writeFile(core::switchMangaDir(key_, mangaId) + "/" + core::kSwitchCoverFile, bytes,
                                 [this, alive, run, mangaId, size](bool ok) {
                                     if (alive.expired() || !*alive.lock()) {
                                         return;
                                     }
                                     if (ok) {
                                         writtenSince_ += size;
                                         if (core::SwitchManga* m = mangaOf(mangaId)) {
                                             m->coverBytes = size;
                                             store_.writeManga(key_, *m, {});
                                         } else if (pendingManga_.count(mangaId) > 0) {
                                             pendingManga_[mangaId].coverBytes = size;
                                         }
                                     }
                                     if (current(run)) {
                                         requestPages(run);
                                     }
                                 });
            });
        });
}

void SwitchDownloads::requestPages(std::uint64_t run) {
    if (!current(run)) {
        return;
    }
    const std::weak_ptr<bool> alive = alive_;
    client_.pages(running_->chapterId, format_, cancel_, net::Priority::Background)
        .then([this, alive, run](const api::ApiResult<core::PageList>& r) {
            if (alive.expired() || !*alive.lock()) {
                return;
            }
            onPageList(run, r);
        });
}

void SwitchDownloads::onPageList(std::uint64_t run, const api::ApiResult<core::PageList>& result) {
    if (!current(run)) {
        return;
    }
    const int chapterId = running_->chapterId;
    if (!result.ok()) {
        if (serverAwayKind(result.error().kind)) {
            serverAway(false);
        } else if (result.error().kind == api::ApiErrorKind::Auth) {
            serverAway(true);
        } else if (result.error().kind != api::ApiErrorKind::Cancelled) {
            failChapter(chapterId, -1, firstLine(result.error().message));
        }
        return;
    }
    const std::vector<std::string>& pages = result.value().pages;
    const int count = static_cast<int>(pages.size());
    if (count == 0) {
        failChapter(chapterId, -1, "The server lists no pages for this chapter.");
        return;
    }
    if (count > core::kSwitchPagesMax) {
        failChapter(chapterId, -1, "The chapter has more pages than the app keeps.");
        return;
    }
    core::SwitchQueueEntry* e = entry(chapterId);
    if (!e) {
        return;
    }
    const std::string dir = core::switchChapterDir(key_, running_->mangaId, chapterId);
    if (e->pageCount != 0 && e->pageCount != count) {
        // The server's page list changed length since the pages so far: they may not match, so it starts over.
        say(false, "switch downloads: chapter " + std::to_string(chapterId) + " now has " + std::to_string(count) +
                       " pages (was " + std::to_string(e->pageCount) + "), starting it over");
        store_.removeTree(dir);
        partial_[chapterId] = 0;
    }
    if (e->pageCount != count) {
        e->pageCount = count;
        queueChanged();
    }
    running_->urls.clear();
    for (const std::string& page : pages) {
        running_->urls.push_back(page.empty() ? std::string() : client_.absoluteUrl(page));
    }
    running_->pages.assign(pages.size(), Page{});
    const std::weak_ptr<bool> alive = alive_;
    store_.listPages(dir, [this, alive, run](const std::map<int, DownloadStore::PageFile>& onCard) {
        if (alive.expired() || !*alive.lock()) {
            return;
        }
        onListed(run, onCard);
    });
}

void SwitchDownloads::onListed(std::uint64_t run, const std::map<int, DownloadStore::PageFile>& onCard) {
    if (!current(run)) {
        return;
    }
    std::uint64_t bytes = 0;
    int done = 0;
    for (std::size_t i = 0; i < running_->pages.size(); ++i) {
        Page& page = running_->pages[i];
        if (running_->urls[i].empty()) {
            page.done = true;
            page.format = core::kPageUnusable;
            ++done;
        } else if (const auto found = onCard.find(static_cast<int>(i)); found != onCard.end()) {
            page.done = true;  // kept from before (a restart, a pause): not fetched again
            page.format = found->second.format;
            page.bytes = found->second.bytes;
            bytes += page.bytes;
            ++done;
        }
    }
    partial_[running_->chapterId] = bytes;
    progress_[running_->chapterId] = {done, static_cast<int>(running_->pages.size())};
    running_->listed = true;
    serverFailures_ = 0;
    changed();
    pump(run);
}

void SwitchDownloads::pump(std::uint64_t run) {
    if (!current(run) || holdsBack() || running_->committing) {
        return;
    }
    const bool allDone = std::all_of(running_->pages.begin(), running_->pages.end(), [](const Page& p) { return p.done; });
    if (allDone) {
        if (running_->inFlight == 0) {
            commit(run);
        }
        return;
    }
    // current(run) each time: a page's answer may stop the chapter before fetchPage returns.
    while (current(run) && running_->inFlight < limits_.pagesAtOnce) {
        int next = -1;
        for (std::size_t i = 0; i < running_->pages.size(); ++i) {
            if (!running_->pages[i].done && !running_->pages[i].asked) {
                next = static_cast<int>(i);
                break;
            }
        }
        if (next < 0) {
            return;
        }
        if (hooks_.memoryLow && hooks_.memoryLow()) {
            if (wait_ != core::SwitchWait::Memory) {
                wait_ = core::SwitchWait::Memory;
                changed();
            }
            scheduleKick(limits_.retryMs);
            return;
        }
        if (wait_ == core::SwitchWait::Memory) {
            wait_ = core::SwitchWait::None;
            changed();
        }
        // The page's size is known only once it is in (chunked answers): the biggest page so far is the guess here, the
        // real size is checked again before the write.
        std::uint64_t guess = 0;
        for (const Page& p : running_->pages) {
            guess = std::max(guess, p.bytes);
        }
        if (!spaceFor(guess)) {
            return;
        }
        fetchPage(run, next);
    }
}

void SwitchDownloads::fetchPage(std::uint64_t run, int index) {
    Page& page = running_->pages[static_cast<std::size_t>(index)];
    page.asked = true;
    running_->inFlight += 1;
    net::Request request;
    request.url = running_->urls[static_cast<std::size_t>(index)];
    request.maxBodyBytes = limits_.maxImageBytes;  // stopped while it streams in if it's bigger (S6)
    request.timeoutMs = limits_.imageTimeoutMs;
    const std::weak_ptr<bool> alive = alive_;
    auto post = hooks_.post;
    http_.submit(std::move(request), net::Priority::Background, cancel_)
        .then([this, alive, post, run, index](const net::HttpResult& result) {
            post([this, alive, run, index, result] {
                if (alive.expired() || !*alive.lock()) {
                    return;
                }
                onPage(run, index, result);
            });
        });
}

void SwitchDownloads::onPage(std::uint64_t run, int index, const net::HttpResult& result) {
    if (!current(run)) {
        return;
    }
    Page& page = running_->pages[static_cast<std::size_t>(index)];
    const auto release = [this, &page] {
        page.asked = false;
        running_->inFlight -= 1;
    };
    if (!result.ok()) {
        const core::ErrorKind kind = result.error().kind;
        if (kind == core::ErrorKind::Cancelled) {
            release();
            return;
        }
        if (kind == core::ErrorKind::TooLarge) {
            pageFailed(run, index, "the page is bigger than an image may be");
            return;
        }
        release();
        serverAway(false);  // no answer at all: no try is counted
        return;
    }
    const net::Response& response = result.value();
    if (response.status == 401 && !page.authRetried) {
        // ui_login: the client saw the 401 too and is renewing the token (D38). Once more in a moment, like the loader.
        page.authRetried = true;
        running_->inFlight -= 1;
        const std::weak_ptr<bool> alive = alive_;
        hooks_.later(limits_.authRetryMs, [this, alive, run, index] {
            if (alive.expired() || !*alive.lock() || !current(run)) {
                return;
            }
            fetchPage(run, index);
        });
        return;
    }
    if (response.status == 401) {
        release();
        serverAway(true);
        return;
    }
    if (response.status != 200) {
        pageFailed(run, index, "the server answered HTTP " + std::to_string(response.status));
        return;
    }
    // S6: what is written must be an image the reader can open.
    const image::ImageFormat format = image::sniffFormat(response.body);
    if (response.body.empty() || format == image::ImageFormat::Unknown) {
        pageFailed(run, index, "the server's answer isn't an image");
        return;
    }
    const auto header = image::readHeader(response.body);
    if (!header.ok()) {
        pageFailed(run, index, "the page's image is broken");
        return;
    }
    if (header.value().size.width > limits_.maxImageSide || header.value().size.height > limits_.maxImageSide) {
        pageFailed(run, index, "the page's image is too big");
        return;
    }
    if (!spaceFor(response.body.size())) {
        release();  // fetched again once there is room
        return;
    }
    const char letter = formatLetter(format);
    const std::uint64_t size = response.body.size();
    auto bytes = std::make_shared<const std::string>(response.body);
    ensureFolder();
    const int chapterId = running_->chapterId;
    const std::weak_ptr<bool> alive = alive_;
    store_.writeFile(pagePath(running_->mangaId, chapterId, index, letter), bytes,
                     [this, alive, run, index, letter, size, chapterId](bool ok) {
                         if (alive.expired() || !*alive.lock()) {
                             return;
                         }
                         if (ok) {
                             writtenSince_ += size;
                             partial_[chapterId] += size;
                         }
                         if (!current(run)) {
                             return;  // stopped meanwhile: the page is on the SD card and counts next time
                         }
                         Page& p = running_->pages[static_cast<std::size_t>(index)];
                         p.asked = false;
                         running_->inFlight -= 1;
                         if (!ok) {
                             if (!store_.writesAllowed()) {
                                 return;  // HOME / sleep: it comes again
                             }
                             pendingError_ = kWriteError;
                             failChapter(chapterId, index, kWriteError);
                             return;
                         }
                         p.done = true;
                         p.format = letter;
                         p.bytes = size;
                         auto& progress = progress_[chapterId];
                         progress.first = static_cast<int>(std::count_if(
                             running_->pages.begin(), running_->pages.end(), [](const Page& q) { return q.done; }));
                         progress.second = static_cast<int>(running_->pages.size());
                         changed();
                         pump(run);
                     });
}

void SwitchDownloads::pageFailed(std::uint64_t run, int index, const std::string& why) {
    Page& page = running_->pages[static_cast<std::size_t>(index)];
    page.tries += 1;
    running_->inFlight -= 1;
    if (page.tries >= limits_.pageTries) {
        say(true, "switch downloads: chapter " + std::to_string(running_->chapterId) + " page " + std::to_string(index) +
                      " failed " + std::to_string(page.tries) + " times: " + why);
        page.asked = false;
        // "Failed at page N" names the first page that failed, not the one that ran out of tries first: two pages download
        // at once, and which of two failing pages gives up first is down to timing (WP-18c).
        int first = index;
        for (int i = 0; i < index; ++i) {
            const Page& earlier = running_->pages[static_cast<std::size_t>(i)];
            if (!earlier.done && earlier.tries > 0) {
                first = i;
                break;
            }
        }
        failChapter(running_->chapterId, first, why);
        return;
    }
    // Asked again after retryMs, doubled per try; it keeps its place (asked) meanwhile.
    const std::weak_ptr<bool> alive = alive_;
    hooks_.later(backoffMs(limits_.retryMs, page.tries, limits_.retryMs * 64), [this, alive, run, index] {
        if (alive.expired() || !*alive.lock() || !current(run)) {
            return;
        }
        fetchPage(run, index);
    });
}

void SwitchDownloads::serverAway(bool login) {
    stopRunning();
    if (login) {
        wait_ = core::SwitchWait::Login;  // until a login works (connected())
        say(true, "switch downloads: can't log in to the server, waiting");
    } else {
        wait_ = core::SwitchWait::Server;
        serverFailures_ += 1;
        const int ms = backoffMs(limits_.serverRetryMs, serverFailures_, limits_.serverRetryMaxMs);
        say(false, "switch downloads: the server doesn't answer, trying again in " + std::to_string(ms) + " ms");
        scheduleKick(ms);
    }
    changed();
}

void SwitchDownloads::failChapter(int chapterId, int failedPage, const std::string& why) {
    core::SwitchQueueEntry* e = entry(chapterId);
    if (running_ && running_->chapterId == chapterId) {
        Change change;
        change.state = true;
        change.finished = Change::Finished{chapterId, false, static_cast<int>(running_->pages.size()), partial_[chapterId],
                                           hooks_.clockMs() - running_->startedMs, why};
        stopRunning();
        notify(change);
    }
    if (e) {
        e->failed = true;
        e->failedPage = failedPage;
        e->error = why;
        queueChanged();
    }
    changed();
    kick();  // the next chapter
}

void SwitchDownloads::putPending(core::SwitchManga manga) {
    if (const auto cover = covers_.find(manga.id); cover != covers_.end()) {
        manga.coverBytes = cover->second;
        covers_.erase(cover);
    }
    const int id = manga.id;
    pendingManga_[id] = std::move(manga);
}

void SwitchDownloads::putManga(core::SwitchManga manga) {
    if (core::SwitchManga* known = mangaOf(manga.id)) {
        *known = std::move(manga);
    } else {
        manga_.push_back(std::move(manga));
    }
}

void SwitchDownloads::commit(std::uint64_t run) {
    running_->committing = true;
    const int chapterId = running_->chapterId;
    const int mangaId = running_->mangaId;
    const core::SwitchQueueEntry* e = entry(chapterId);
    if (!e) {
        stopRunning();
        return;
    }
    core::SwitchChapter chapter = e->chapter;
    chapter.formats.clear();
    chapter.bytes = 0;
    for (const Page& p : running_->pages) {
        chapter.formats.push_back(p.format);
        chapter.bytes += p.bytes;
    }
    chapter.downloadedAt = hooks_.nowSeconds ? hooks_.nowSeconds() : 0;
    chapter.damaged.clear();
    core::SwitchManga manga;
    if (const core::SwitchManga* known = mangaOf(mangaId)) {
        manga = *known;
    } else if (pendingManga_.count(mangaId) > 0) {
        manga = pendingManga_[mangaId];
    } else {
        manga.id = mangaId;
        manga.title = e->mangaTitle;
        manga.thumbnailUrl = e->thumbnailUrl;
    }
    core::putChapter(manga, chapter);
    ensureFolder();
    const std::weak_ptr<bool> alive = alive_;
    const std::int64_t ms = hooks_.clockMs() - running_->startedMs;
    store_.writeManga(key_, manga, [this, alive, run, manga, chapter, ms](bool ok) {
        if (alive.expired() || !*alive.lock()) {
            return;
        }
        if (!ok) {
            if (!current(run)) {
                return;  // stopped meanwhile: it is committed when it runs again
            }
            running_->committing = false;
            if (store_.writesAllowed()) {
                pendingError_ = kWriteError;
                failChapter(chapter.id, -1, kWriteError);
            }
            return;
        }
        if (!entry(chapter.id)) {
            return;  // its manga was deleted meanwhile (that delete ran after this write)
        }
        // Committed: the chapter is on this Switch (also when a pause stopped it meanwhile: manga.json has it).
        putManga(manga);
        pendingManga_.erase(manga.id);
        partial_.erase(chapter.id);
        progress_.erase(chapter.id);
        queue_.entries.erase(std::remove_if(queue_.entries.begin(), queue_.entries.end(),
                                            [&](const core::SwitchQueueEntry& q) { return q.chapter.id == chapter.id; }),
                             queue_.entries.end());
        if (current(run)) {
            running_.reset();
        }
        say(false, "switch downloads: chapter " + std::to_string(chapter.id) + " (" + std::to_string(chapter.pages()) +
                       " pages, " + core::bytesText(chapter.bytes) + ") in " + std::to_string(ms) + " ms");
        queueChanged();
        Change change;
        change.state = true;
        change.finished = Change::Finished{chapter.id, true, chapter.pages(), chapter.bytes, ms, {}};
        notify(change);
        kick();
    });
}

// --- Actions ---

int SwitchDownloads::enqueue(const core::Manga& manga, const std::vector<core::Chapter>& chapters, bool* capped) {
    if (capped) {
        *capped = false;
    }
    if (!loaded_ || manga.id <= 0) {
        return 0;
    }
    const std::set<int> skip = skipIds();
    int added = 0;
    for (const core::Chapter& chapter : chapters) {
        if (chapter.id <= 0 || skip.count(chapter.id) > 0) {
            continue;
        }
        if (queue_.entries.size() >= limits_.queueMax) {
            if (capped) {
                *capped = true;
            }
            break;
        }
        core::SwitchQueueEntry e;
        e.mangaId = manga.id;
        e.mangaTitle = core::switchMangaFrom(manga).title;
        e.thumbnailUrl = core::switchMangaFrom(manga).thumbnailUrl;
        e.chapter = core::switchChapterFrom(chapter);
        queue_.entries.push_back(std::move(e));
        ++added;
    }
    if (added == 0) {
        return 0;
    }
    if (!mangaOf(manga.id) && pendingManga_.count(manga.id) == 0) {
        putPending(core::switchMangaFrom(manga));  // no details call needed for it
    }
    queueChanged();
    report(Action::Enqueue, added, true);
    kick();
    return added;
}

void SwitchDownloads::dropMangaIfEmpty(int mangaId) {
    const core::SwitchManga* known = mangaOf(mangaId);
    if ((known && !known->chapters.empty()) || anyQueuedFor(mangaId)) {
        return;
    }
    // Nothing of it is wanted any more: its folder (a cover, pages) goes.
    if (known) {
        manga_.erase(std::remove_if(manga_.begin(), manga_.end(), [mangaId](const core::SwitchManga& m) { return m.id == mangaId; }),
                     manga_.end());
    }
    pendingManga_.erase(mangaId);
    covers_.erase(mangaId);
    coverTried_.erase(mangaId);
    store_.removeTree(core::switchMangaDir(key_, mangaId));
}

bool SwitchDownloads::removeFromQueue(int chapterId) {
    const core::SwitchQueueEntry* e = entry(chapterId);
    if (!e) {
        return false;
    }
    if (running_ && running_->chapterId == chapterId) {
        if (running_->committing) {
            return false;  // it is being saved as downloaded right now
        }
        stopRunning();
    }
    const int mangaId = e->mangaId;
    queue_.entries.erase(std::remove_if(queue_.entries.begin(), queue_.entries.end(),
                                        [chapterId](const core::SwitchQueueEntry& q) { return q.chapter.id == chapterId; }),
                         queue_.entries.end());
    partial_.erase(chapterId);
    progress_.erase(chapterId);
    store_.removeTree(core::switchChapterDir(key_, mangaId, chapterId));
    dropMangaIfEmpty(mangaId);
    queueChanged();
    measureFree();
    report(Action::Remove, 1, true);
    kick();
    return true;
}

bool SwitchDownloads::retry(int chapterId) {
    if (core::SwitchQueueEntry* e = entry(chapterId)) {
        if (!e->failed) {
            return false;
        }
        e->failed = false;
        e->failedPage = -1;
        e->error.clear();
        queueChanged();
        report(Action::Retry, 1, true);
        kick();
        return true;
    }
    // A damaged chapter: its broken pages go, it leaves the index and is queued again; its good pages stay and count.
    for (const core::SwitchManga& m : manga_) {
        const core::SwitchChapter* c = m.chapter(chapterId);
        if (!c || c->damaged.empty()) {
            continue;
        }
        if (queue_.entries.size() >= limits_.queueMax) {
            return false;
        }
        const core::SwitchChapter chapter = *c;
        core::SwitchManga copy = m;
        for (int page : chapter.damaged) {
            if (page >= 0 && page < chapter.pages()) {
                store_.removeFile(pagePath(m.id, chapter.id, page, chapter.formats[static_cast<std::size_t>(page)]));
            }
        }
        core::removeChapter(copy, chapterId);
        core::SwitchQueueEntry e;
        e.mangaId = m.id;
        e.mangaTitle = m.title;
        e.thumbnailUrl = m.thumbnailUrl;
        e.chapter = chapter;
        e.chapter.formats.clear();
        e.chapter.bytes = 0;
        e.chapter.damaged.clear();
        e.pageCount = chapter.pages();
        queue_.entries.push_back(std::move(e));
        partial_[chapterId] = 0;  // counted again when it starts
        store_.writeManga(key_, copy, {});
        if (copy.chapters.empty()) {
            pendingManga_[copy.id] = copy;
            manga_.erase(std::remove_if(manga_.begin(), manga_.end(), [&](const core::SwitchManga& x) { return x.id == copy.id; }),
                         manga_.end());
        } else {
            putManga(copy);
        }
        queueChanged();
        report(Action::Retry, 1, true);
        kick();
        return true;
    }
    return false;
}

bool SwitchDownloads::retryFailed() {
    int count = 0;
    for (core::SwitchQueueEntry& e : queue_.entries) {
        if (e.failed) {
            e.failed = false;
            e.failedPage = -1;
            e.error.clear();
            ++count;
        }
    }
    if (count == 0) {
        return false;
    }
    queueChanged();
    report(Action::RetryFailed, count, true);
    kick();
    return true;
}

bool SwitchDownloads::moveToTop(int chapterId) {
    auto it = std::find_if(queue_.entries.begin(), queue_.entries.end(),
                           [chapterId](const core::SwitchQueueEntry& e) { return e.chapter.id == chapterId; });
    if (it == queue_.entries.end()) {
        return false;
    }
    std::rotate(queue_.entries.begin(), it, it + 1);  // the downloading one goes on where it ends up
    queueChanged();
    report(Action::Top, 1, true);
    return true;
}

bool SwitchDownloads::moveToBottom(int chapterId) {
    auto it = std::find_if(queue_.entries.begin(), queue_.entries.end(),
                           [chapterId](const core::SwitchQueueEntry& e) { return e.chapter.id == chapterId; });
    if (it == queue_.entries.end()) {
        return false;
    }
    std::rotate(it, it + 1, queue_.entries.end());
    queueChanged();
    report(Action::Bottom, 1, true);
    return true;
}

bool SwitchDownloads::pause() {
    const int waiting = static_cast<int>(queue_.entries.size()) - failedCount();
    if (queue_.paused || waiting <= 0) {
        return false;
    }
    queue_.paused = true;
    stopRunning();  // its pages stay: it goes on from there
    queueChanged();
    report(Action::Pause, waiting, true);
    return true;
}

bool SwitchDownloads::resume() {
    if (!queue_.paused) {
        return false;
    }
    queue_.paused = false;
    queueChanged();
    report(Action::Resume, static_cast<int>(queue_.entries.size()) - failedCount(), true);
    kick();
    return true;
}

bool SwitchDownloads::clear() {
    std::vector<core::SwitchQueueEntry> kept;
    std::set<int> mangaIds;
    int removed = 0;
    for (core::SwitchQueueEntry& e : queue_.entries) {
        if (running_ && running_->chapterId == e.chapter.id && running_->committing) {
            kept.push_back(std::move(e));  // being saved as downloaded right now
            continue;
        }
        store_.removeTree(core::switchChapterDir(key_, e.mangaId, e.chapter.id));
        partial_.erase(e.chapter.id);
        progress_.erase(e.chapter.id);
        mangaIds.insert(e.mangaId);
        ++removed;
    }
    if (removed == 0) {
        return false;
    }
    if (!(running_ && running_->committing)) {
        stopRunning();
    }
    queue_.entries = std::move(kept);
    queue_.paused = false;
    for (int id : mangaIds) {
        dropMangaIfEmpty(id);
    }
    queueChanged();
    measureFree();
    report(Action::Clear, removed, true);
    return true;
}

bool SwitchDownloads::deleteChapters(const std::vector<int>& ids, Action action) {
    const std::set<int> wanted(ids.begin(), ids.end());
    int deleted = 0;
    std::vector<core::SwitchManga> changedManga;
    for (const core::SwitchManga& m : manga_) {
        core::SwitchManga copy = m;
        std::vector<int> gone;
        for (const core::SwitchChapter& c : m.chapters) {
            if (wanted.count(c.id) > 0) {
                gone.push_back(c.id);
            }
        }
        if (gone.empty()) {
            continue;
        }
        for (int id : gone) {
            core::removeChapter(copy, id);
        }
        deleted += static_cast<int>(gone.size());
        // The index first, then the pages: a power cut in between leaves folders the next start deletes.
        if (copy.chapters.empty() && !anyQueuedFor(copy.id)) {
            store_.removeTree(core::switchMangaDir(key_, copy.id));
        } else {
            store_.writeManga(key_, copy, [this, alive = std::weak_ptr<bool>(alive_)](bool ok) {
                if (!ok && !alive.expired() && *alive.lock()) {
                    pendingError_ = kWriteError;
                    changed();
                }
            });
            for (int id : gone) {
                store_.removeTree(core::switchChapterDir(key_, copy.id, id));
            }
        }
        changedManga.push_back(std::move(copy));
    }
    if (deleted == 0) {
        return false;
    }
    for (core::SwitchManga& copy : changedManga) {
        if (copy.chapters.empty() && !anyQueuedFor(copy.id)) {
            const int id = copy.id;
            manga_.erase(std::remove_if(manga_.begin(), manga_.end(), [id](const core::SwitchManga& m) { return m.id == id; }),
                         manga_.end());
            coverTried_.erase(id);
        } else if (copy.chapters.empty()) {
            pendingManga_[copy.id] = copy;  // chapters of it still queued: the commit writes it again
            const int id = copy.id;
            manga_.erase(std::remove_if(manga_.begin(), manga_.end(), [id](const core::SwitchManga& m) { return m.id == id; }),
                         manga_.end());
        } else {
            putManga(std::move(copy));
        }
    }
    measureFree();  // a full queue goes on by itself
    report(action, deleted, true);
    kick();
    return true;
}

bool SwitchDownloads::deleteManga(int mangaId) {
    const core::SwitchManga* known = mangaOf(mangaId);
    const bool damaged = std::find(damagedManga_.begin(), damagedManga_.end(), mangaId) != damagedManga_.end();
    if (!known && !damaged && !anyQueuedFor(mangaId)) {
        return false;
    }
    const int chapters = known ? static_cast<int>(known->chapters.size()) : 0;
    if (running_ && running_->mangaId == mangaId) {
        stopRunning();
    }
    for (const core::SwitchQueueEntry& e : queue_.entries) {
        if (e.mangaId == mangaId) {
            partial_.erase(e.chapter.id);
            progress_.erase(e.chapter.id);
        }
    }
    queue_.entries.erase(std::remove_if(queue_.entries.begin(), queue_.entries.end(),
                                        [mangaId](const core::SwitchQueueEntry& e) { return e.mangaId == mangaId; }),
                         queue_.entries.end());
    manga_.erase(std::remove_if(manga_.begin(), manga_.end(), [mangaId](const core::SwitchManga& m) { return m.id == mangaId; }),
                 manga_.end());
    damagedManga_.erase(std::remove_if(damagedManga_.begin(), damagedManga_.end(), [mangaId](int id) { return id == mangaId; }),
                        damagedManga_.end());
    pendingManga_.erase(mangaId);
    covers_.erase(mangaId);
    coverTried_.erase(mangaId);
    store_.removeTree(core::switchMangaDir(key_, mangaId));
    queueChanged();
    measureFree();
    report(Action::DeleteManga, chapters, true);
    kick();
    return true;
}

void SwitchDownloads::markDamaged(int chapterId, int page) {
    for (core::SwitchManga& m : manga_) {
        for (core::SwitchChapter& c : m.chapters) {
            if (c.id != chapterId) {
                continue;
            }
            if (page < 0 || page >= c.pages() || std::find(c.damaged.begin(), c.damaged.end(), page) != c.damaged.end()) {
                return;
            }
            c.damaged.insert(std::upper_bound(c.damaged.begin(), c.damaged.end(), page), page);
            say(true, "switch downloads: chapter " + std::to_string(chapterId) + " page " + std::to_string(page) +
                          " is damaged on the SD card");
            store_.writeManga(key_, m, {});
            changed();
            return;
        }
    }
}

// --- "Same server?" (WP-18c) ---

void SwitchDownloads::addressQuestion(const std::string& newUrl, std::function<void(AddressQuestion)> done) {
    AddressQuestion question;
    question.oldUrl = url_;
    question.newUrl = core::downloadServerUrl(newUrl);
    for (const core::SwitchManga& m : manga_) {
        question.chapters += static_cast<int>(m.chapters.size());
    }
    // Only a change away from a saved server that has downloaded chapters, to an address its server.json doesn't list.
    const bool maybe = loaded_ && !url_.empty() && !question.newUrl.empty() && question.newUrl != url_ &&
                       question.chapters > 0 &&
                       std::find(urls_.begin(), urls_.end(), question.newUrl) == urls_.end();
    if (!maybe) {
        done(question);
        return;
    }
    // Another folder may list it already (an earlier answer, or downloads of its own): then nothing to ask.
    store_.findListing(question.newUrl, [done, question](std::optional<DownloadStore::Listing> listing) mutable {
        question.ask = !listing.has_value();
        done(question);
    });
}

void SwitchDownloads::joinAddress(const std::string& newUrl, std::function<void(bool ok)> done) {
    if (!loaded_ || key_.empty()) {
        if (done) {
            done(false);
        }
        return;
    }
    urls_ = core::addServerUrl(urls_.empty() ? std::vector<std::string>{url_} : urls_, newUrl, url_);
    serverWritten_ = true;
    say(false, "switch downloads: " + core::downloadServerUrl(newUrl) + " is the same server as " + url_);
    // On the store's thread, before the scan the new address's setServer() asks for (FIFO): the scan finds this folder.
    store_.writeServer(key_, urls_, [this, alive = std::weak_ptr<bool>(alive_), done](bool ok) {
        if (!ok && !alive.expired() && *alive.lock()) {
            pendingError_ = kWriteError;
        }
        if (done) {
            done(ok);
        }
    });
}

void SwitchDownloads::keepApart(const std::string& newUrl, std::function<void(bool ok)> done) {
    const std::string url = core::downloadServerUrl(newUrl);
    say(false, "switch downloads: " + url + " is another server than " + url_);
    store_.startServerFolder(core::switchServerKey(url), {url}, [this, alive = std::weak_ptr<bool>(alive_), done](bool ok) {
        if (!ok && !alive.expired() && *alive.lock()) {
            pendingError_ = kWriteError;
        }
        if (done) {
            done(ok);
        }
    });
}

}  // namespace snx::ui
