#include "ui/downloads/DownloadStore.hpp"

#include <algorithm>
#include <chrono>
#include <set>
#include <utility>

namespace snx::ui {

namespace {

constexpr int kStorePriority = 0;  // one priority: the tasks run in the order they were asked

std::int64_t nowMs() {
    return image::steadyMs();
}

std::string join(const std::string& dir, const std::string& name) {
    return dir + "/" + name;
}

}  // namespace

DownloadStore::Options DownloadStore::optionsFrom(const core::RuntimeProfile& profile) {
    Options options;
    options.mangaFileBytes = static_cast<std::size_t>(profile.switchMangaFileBytes);
    options.queueFileBytes = static_cast<std::size_t>(profile.switchQueueFileBytes);
    options.maxDepth = profile.json.maxDepth;
    options.queueMax = static_cast<std::size_t>(std::max(profile.switchQueueMax, 1));
    options.workerStackBytes = profile.workerThreadStackBytes;
    return options;
}

DownloadStore::DownloadStore(const platform::SafeFs& fs, Options options, Deliver deliver, Log log)
    : fs_(fs), options_(std::move(options)), deliver_(std::move(deliver)), log_(std::move(log)),
      pool_(1, options_.workerStackBytes) {
    pool_.setOnException([this](const char* what) { say(true, std::string("switch downloads: a file task threw: ") + what); });
    if (pool_.threads() == 0) {
        say(true, "switch downloads: the file thread couldn't start, so nothing will be written");
    }
}

DownloadStore::~DownloadStore() {
    stop();
}

void DownloadStore::say(bool warning, const std::string& line) const {
    if (log_) {
        log_(warning, line);
    }
}

void DownloadStore::post(std::function<void()> task) {
    pool_.post(kStorePriority, std::move(task));
}

void DownloadStore::stop() {
    writesAllowed_.store(false);
    pool_.stop();
    flush();
}

// --- The scan ---

std::optional<DownloadStore::Listing> DownloadStore::listingNow(const std::string& url) const {
    core::JsonLimits small;
    small.maxBytes = options_.queueFileBytes;
    small.maxDepth = options_.maxDepth;
    for (const platform::SafeFs::Entry& entry : fs_.listDirEntries(core::kSwitchDownloadsDir)) {
        if (!entry.isDir) {
            continue;
        }
        const std::string dir = core::switchServerDir(entry.name);
        const auto text = fs_.readFile(join(dir, core::kSwitchServerFile), options_.queueFileBytes);
        const auto urls = text ? core::readServerFile(*text, small) : std::nullopt;
        if (urls && std::find(urls->begin(), urls->end(), url) != urls->end()) {
            return Listing{entry.name, *urls};
        }
    }
    return std::nullopt;
}

DownloadStore::Scan DownloadStore::scanNow(const std::string& rawUrl) const {
    Scan scan;
    const std::string url = core::downloadServerUrl(rawUrl);
    core::JsonLimits small;
    small.maxBytes = options_.queueFileBytes;
    small.maxDepth = options_.maxDepth;
    // 1. The folder: the one whose server.json lists the address; else the one named after it (its server.json is gone).
    if (auto listing = listingNow(url)) {
        scan.key = std::move(listing->key);
        scan.urls = std::move(listing->urls);
        scan.folderThere = true;
    }
    if (scan.key.empty()) {
        scan.key = core::switchServerKey(url);
        scan.urls = {url};
        scan.folderThere = fs_.exists(core::switchServerDir(scan.key));
    }
    const std::string serverDir = core::switchServerDir(scan.key);

    // 2. The queue (or its .bak).
    core::JsonLimits queueLimits = small;
    const std::string queuePath = join(serverDir, core::kSwitchQueueFile);
    core::QueueFileLoad queue;
    if (const auto text = fs_.readFile(queuePath, options_.queueFileBytes)) {
        queue = core::readQueueFile(*text, queueLimits, options_.queueMax);
    }
    if (!queue.usable) {
        if (const auto backup = fs_.readFile(queuePath + core::kSwitchBackupSuffix, options_.queueFileBytes)) {
            core::QueueFileLoad again = core::readQueueFile(*backup, queueLimits, options_.queueMax);
            if (again.usable) {
                scan.problems.push_back("switch downloads: queue.json is unreadable or missing, using its .bak");
                queue = std::move(again);
            }
        }
    }
    scan.problems.insert(scan.problems.end(), queue.problems.begin(), queue.problems.end());
    scan.queue = std::move(queue.queue);
    std::set<int> queued;
    for (const core::SwitchQueueEntry& entry : scan.queue.entries) {
        queued.insert(entry.chapter.id);
    }

    // 3. Each manga folder: its index, then its chapter folders.
    core::JsonLimits mangaLimits;
    mangaLimits.maxBytes = options_.mangaFileBytes;
    mangaLimits.maxDepth = options_.maxDepth;
    for (const platform::SafeFs::Entry& entry : fs_.listDirEntries(serverDir)) {
        const auto mangaId = entry.isDir ? core::idOfFolder(entry.name, 'm') : std::nullopt;
        if (!mangaId) {
            continue;
        }
        const std::string mangaDir = core::switchMangaDir(scan.key, *mangaId);
        const std::string indexPath = join(mangaDir, core::kSwitchMangaFile);
        const std::string backupPath = indexPath + core::kSwitchBackupSuffix;
        core::MangaFileLoad index;
        const bool indexThere = fs_.exists(indexPath) || fs_.exists(backupPath);
        if (const auto text = fs_.readFile(indexPath, options_.mangaFileBytes)) {
            index = core::readMangaFile(*text, mangaLimits);
        }
        if (!index.usable && fs_.exists(backupPath)) {
            if (const auto backup = fs_.readFile(backupPath, options_.mangaFileBytes)) {
                core::MangaFileLoad again = core::readMangaFile(*backup, mangaLimits);
                if (again.usable) {
                    scan.problems.push_back("switch downloads: " + indexPath + " is unreadable, using its .bak");
                    index = std::move(again);
                }
            }
        }
        if (index.usable && index.manga.id != *mangaId) {
            index.usable = false;  // an index of another manga in this folder: don't trust it
        }
        scan.problems.insert(scan.problems.end(), index.problems.begin(), index.problems.end());
        const bool anyQueued = std::any_of(scan.queue.entries.begin(), scan.queue.entries.end(),
                                           [&](const core::SwitchQueueEntry& e) { return e.mangaId == *mangaId; });
        if (indexThere && !index.usable) {
            scan.damagedManga.push_back(*mangaId);  // never deleted by itself, only by "Delete all downloads..."
            continue;
        }
        if (!indexThere && !anyQueued) {
            // A manga whose last chapter was removed or deleted while the power went: nothing of it is wanted.
            fs_.removeTree(mangaDir);
            scan.orphansRemoved += 1;
            continue;
        }
        std::set<int> indexed;
        for (const core::SwitchChapter& chapter : index.manga.chapters) {
            indexed.insert(chapter.id);
        }
        for (const platform::SafeFs::Entry& sub : fs_.listDirEntries(mangaDir)) {
            if (!sub.isDir && sub.name == core::kSwitchCoverFile && !index.usable && sub.size > 0) {
                scan.covers[*mangaId] = sub.size;  // fetched before the first commit: counted, not fetched again
            }
            const auto chapterId = sub.isDir ? core::idOfFolder(sub.name, 'c') : std::nullopt;
            if (!chapterId) {
                continue;
            }
            const std::string chapterDir = join(mangaDir, sub.name);
            if (queued.count(*chapterId) > 0) {
                fs_.removeTempFiles(chapterDir);  // a page cut off by a power cut; it comes again
                std::uint64_t bytes = 0;
                for (const platform::SafeFs::Entry& page : fs_.listDirEntries(chapterDir)) {
                    if (!page.isDir && core::parsePageName(page.name)) {
                        bytes += page.size;
                    }
                }
                scan.partial[*chapterId] = bytes;
            } else if (indexed.count(*chapterId) == 0) {
                fs_.removeTree(chapterDir);  // a remove or delete that a power cut stopped halfway
                scan.orphansRemoved += 1;
            }
        }
        if (index.usable) {
            scan.manga.push_back(std::move(index.manga));
        }
    }
    const std::int64_t started = nowMs();
    const auto free = options_.freeBytes ? options_.freeBytes() : fs_.freeBytes();
    scan.freeMs = static_cast<int>(nowMs() - started);
    scan.freeKnown = free.has_value();
    scan.freeBytes = free.value_or(0);
    return scan;
}

void DownloadStore::scan(std::string url, std::function<void(Scan)> done) {
    post([this, url = std::move(url), done = std::move(done)] {
        auto result = std::make_shared<Scan>(scanNow(url));
        deliver_([done, result] { done(std::move(*result)); });
    });
}

// --- Pages and the index ---

void DownloadStore::listPages(std::string chapterDir, std::function<void(std::map<int, PageFile>)> done) {
    post([this, chapterDir = std::move(chapterDir), done = std::move(done)] {
        std::map<int, PageFile> pages;
        if (fs_.exists(chapterDir)) {
            fs_.removeTempFiles(chapterDir);
            for (const platform::SafeFs::Entry& entry : fs_.listDirEntries(chapterDir)) {
                if (entry.isDir) {
                    continue;
                }
                if (const auto name = core::parsePageName(entry.name); name && entry.size > 0) {
                    pages[name->index] = PageFile{name->format, entry.size};
                }
            }
        }
        deliver_([done, pages = std::move(pages)] { done(pages); });
    });
}

void DownloadStore::writeFile(std::string path, std::shared_ptr<const std::string> bytes, std::function<void(bool)> done) {
    post([this, path = std::move(path), bytes = std::move(bytes), done = std::move(done)] {
        bool ok = false;
        if (writesAllowed_.load()) {
            const std::size_t slash = path.rfind('/');
            ok = (slash == std::string::npos || fs_.makeDirs(path.substr(0, slash))) && fs_.writeAtomic(path, *bytes);
            if (!ok) {
                say(true, "switch downloads: can't write " + path);
            }
        }
        if (done) {
            deliver_([done, ok] { done(ok); });
        }
    });
}

void DownloadStore::writeManga(std::string key, core::SwitchManga manga, std::function<void(bool)> done) {
    post([this, key = std::move(key), manga = std::move(manga), done = std::move(done)] {
        bool ok = false;
        if (writesAllowed_.load()) {
            const std::string dir = core::switchMangaDir(key, manga.id);
            ok = fs_.makeDirs(dir) &&
                 fs_.writeAtomic(join(dir, core::kSwitchMangaFile), core::writeMangaFile(manga), platform::SafeFs::Backup::Keep);
            if (!ok) {
                say(true, "switch downloads: can't write " + join(dir, core::kSwitchMangaFile));
            }
        }
        if (done) {
            deliver_([done, ok] { done(ok); });
        }
    });
}

void DownloadStore::writeServer(std::string key, std::vector<std::string> urls, std::function<void(bool)> done) {
    post([this, key = std::move(key), urls = std::move(urls), done = std::move(done)] {
        const std::string dir = core::switchServerDir(key);
        const bool ok = fs_.makeDirs(dir) && fs_.writeAtomic(join(dir, core::kSwitchServerFile), core::writeServerFile(urls));
        if (!ok) {
            say(true, "switch downloads: can't write " + join(dir, core::kSwitchServerFile));
        }
        if (done) {
            deliver_([done, ok] { done(ok); });
        }
    });
}

void DownloadStore::findListing(std::string url, std::function<void(std::optional<Listing>)> done) {
    post([this, url = core::downloadServerUrl(url), done = std::move(done)] {
        auto listing = std::make_shared<std::optional<Listing>>(listingNow(url));
        deliver_([done, listing] { done(std::move(*listing)); });
    });
}

void DownloadStore::startServerFolder(std::string key, std::vector<std::string> urls, std::function<void(bool)> done) {
    post([this, key = std::move(key), urls = std::move(urls), done = std::move(done)] {
        const std::string dir = core::switchServerDir(key);
        bool ok = true;
        if (!fs_.exists(dir)) {
            ok = fs_.makeDirs(dir) && fs_.writeAtomic(join(dir, core::kSwitchServerFile), core::writeServerFile(urls));
            if (!ok) {
                say(true, "switch downloads: can't write " + join(dir, core::kSwitchServerFile));
            }
        }
        if (done) {
            deliver_([done, ok] { done(ok); });
        }
    });
}

void DownloadStore::removeTree(std::string dir, std::function<void(bool)> done) {
    post([this, dir = std::move(dir), done = std::move(done)] {
        const bool ok = fs_.removeTree(dir);
        if (!ok) {
            say(true, "switch downloads: can't delete " + dir);
        }
        if (done) {
            deliver_([done, ok] { done(ok); });
        }
    });
}

void DownloadStore::removeFile(std::string path, std::function<void(bool)> done) {
    post([this, path = std::move(path), done = std::move(done)] {
        const bool ok = !fs_.exists(path) || fs_.removeFile(path);
        if (done) {
            deliver_([done, ok] { done(ok); });
        }
    });
}

void DownloadStore::freeBytes(std::function<void(std::optional<std::uint64_t>, int)> done) {
    post([this, done = std::move(done)] {
        const std::int64_t started = nowMs();
        const auto free = options_.freeBytes ? options_.freeBytes() : fs_.freeBytes();
        const int ms = static_cast<int>(nowMs() - started);
        deliver_([done, free, ms] { done(free, ms); });
    });
}

// --- queue.json ---

void DownloadStore::setQueue(const std::string& key, const core::SwitchQueue& queue) {
    std::string text = core::writeQueueFile(queue);
    // Written to fit what is read back (S6): the last entries go first (the user's newest additions).
    core::SwitchQueue fitted;
    if (text.size() > options_.queueFileBytes) {
        fitted = queue;
        while (!fitted.entries.empty() && text.size() > options_.queueFileBytes) {
            fitted.entries.pop_back();
            text = core::writeQueueFile(fitted);
        }
        say(true, "switch downloads: queue.json would be too big, only the first " +
                      std::to_string(fitted.entries.size()) + " entries are written");
    }
    std::lock_guard<std::mutex> lock(queueMutex_);
    queuePath_ = join(core::switchServerDir(key), core::kSwitchQueueFile);
    queueText_ = std::move(text);
    ++queueVersion_;
}

bool DownloadStore::writeQueueNow() {
    std::lock_guard<std::mutex> lock(queueMutex_);
    if (queueWritten_ == queueVersion_ || queuePath_.empty()) {
        return true;
    }
    const std::size_t slash = queuePath_.rfind('/');
    const bool ok = fs_.makeDirs(queuePath_.substr(0, slash)) &&
                    fs_.writeAtomic(queuePath_, queueText_, platform::SafeFs::Backup::Keep);
    if (ok) {
        queueWritten_ = queueVersion_;
        queueWrites_.fetch_add(1);
    } else {
        say(true, "switch downloads: can't write " + queuePath_);
    }
    return ok;
}

void DownloadStore::writeQueueSoon() {
    post([this] { writeQueueNow(); });
}

bool DownloadStore::flush() {
    return writeQueueNow();
}

}  // namespace snx::ui
