#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "core/RuntimeProfile.hpp"
#include "core/SwitchDownloads.hpp"
#include "image/WorkerPool.hpp"
#include "platform/SafeFs.hpp"

// The Switch downloads' files (WP-18, D59; the layout and formats: core/SwitchDownloads.hpp). Files only, no network: the
// queue model (SwitchDownloads) decides, this writes and reads. Every file task runs on the store's own worker thread
// (workerThreadStackBytes, one task after the other, in the order asked) and answers on the main thread through `deliver`.
// All writes go through SafeFs (S4): a page is one writeAtomic (.tmp -> fsync -> rename, S5), written once, never edited;
// manga.json and queue.json keep a .bak. No borealis: unit-tested in temp folders (tests/unit/test_ui_switch_downloads.cpp).
namespace snx::ui {

class DownloadStore {
  public:
    struct Options {
        std::size_t mangaFileBytes = 1024 * 1024;  // RuntimeProfile::switchMangaFileBytes
        std::size_t queueFileBytes = 512 * 1024;   // RuntimeProfile::switchQueueFileBytes
        int maxDepth = 64;                         // RuntimeProfile::json.maxDepth
        std::size_t queueMax = 1000;               // RuntimeProfile::switchQueueMax
        std::uint64_t workerStackBytes = 256 * 1024;  // RuntimeProfile::workerThreadStackBytes
        // The SD card's free bytes (any thread). Empty: SafeFs::freeBytes(). A test build may fake it (sdFreeBytes).
        std::function<std::optional<std::uint64_t>()> freeBytes;
    };
    static Options optionsFrom(const core::RuntimeProfile& profile);
    using Deliver = std::function<void(std::function<void()>)>;  // to the main thread (brls::sync; tests: MainQueue)
    using Log = std::function<void(bool warning, const std::string& line)>;

    DownloadStore(const platform::SafeFs& fs, Options options, Deliver deliver, Log log = {});
    ~DownloadStore();  // stop()
    DownloadStore(const DownloadStore&) = delete;
    DownloadStore& operator=(const DownloadStore&) = delete;

    // What is on the SD card for one server (start-up, a changed server).
    struct Scan {
        std::string key;     // the server's folder: the one whose server.json lists the address, else the address's own key
        bool folderThere = false;              // that folder exists (else it is made by the first write)
        std::vector<std::string> urls;         // its server.json (the address itself when there is none yet)
        std::vector<core::SwitchManga> manga;  // every readable manga.json (or its .bak)
        std::vector<int> damagedManga;         // manga folders whose manga.json and .bak are both unreadable: kept as they are
        core::SwitchQueue queue;               // queue.json (or its .bak), else empty
        std::map<int, std::uint64_t> partial;  // queued chapters: the bytes of their pages written so far
        std::map<int, std::uint64_t> covers;   // queued manga with no manga.json yet: their cover.img's bytes
        std::uint64_t freeBytes = 0;
        bool freeKnown = false;
        int freeMs = 0;                        // how long freeBytes() took (D59: measured on the console)
        int orphansRemoved = 0;                // chapter / manga folders neither indexed nor queued (a stopped delete)
        std::vector<std::string> problems;     // for the log
    };
    // Finds and reads the server's folder: deletes .tmp files in queued chapters' folders and the folders that are neither
    // in a readable manga.json nor in the queue; a damaged manga's folder stays.
    void scan(std::string url, std::function<void(Scan)> done);

    // A page already on the SD card.
    struct PageFile {
        char format = core::kPageJpeg;
        std::uint64_t bytes = 0;
    };
    // The pages written so far in a chapter folder (its .tmp files deleted first). Empty if it isn't there.
    void listPages(std::string chapterDir, std::function<void(std::map<int, PageFile>)> done);
    // One whole file (a page, a cover), made the safe way. Its folder is made if needed. false: writes are stopped
    // (setWritesAllowed(false)) or the SD card refused.
    void writeFile(std::string path, std::shared_ptr<const std::string> bytes, std::function<void(bool ok)> done);
    // The commit: manga.json written (Backup::Keep). Also server.json when `urls` is given (the folder's first write).
    void writeManga(std::string key, core::SwitchManga manga, std::function<void(bool ok)> done);
    void writeServer(std::string key, std::vector<std::string> urls, std::function<void(bool ok)> done);
    // WP-18c: the folder whose server.json lists the address (downloadServerUrl form), if any.
    struct Listing {
        std::string key;
        std::vector<std::string> urls;
    };
    void findListing(std::string url, std::function<void(std::optional<Listing>)> done);
    // A new server's folder with only its server.json ("Another server"): nothing when a folder of that name is there
    // already (ok stays true).
    void startServerFolder(std::string key, std::vector<std::string> urls, std::function<void(bool ok)> done);
    // A folder and everything in it (a chapter, a manga), or one file.
    void removeTree(std::string dir, std::function<void(bool ok)> done = {});
    void removeFile(std::string path, std::function<void(bool ok)> done = {});
    // The SD card's free bytes now, and how long asking took.
    void freeBytes(std::function<void(std::optional<std::uint64_t> bytes, int ms)> done);

    // queue.json: the newest text is written by the next writeQueueSoon() (on the worker thread) or flush() (on the calling
    // thread, at once: HOME, sleep, exit). One mutex keeps the two from writing at the same time; an older text never
    // replaces a newer one.
    void setQueue(const std::string& key, const core::SwitchQueue& queue);
    void writeQueueSoon();
    bool flush();         // true if nothing needed writing or it worked
    int queueWrites() const { return queueWrites_.load(); }

    // HOME / sleep: no page, cover or manga.json writes until allowed again (those asked meanwhile answer false).
    void setWritesAllowed(bool allowed) { writesAllowed_.store(allowed); }
    bool writesAllowed() const { return writesAllowed_.load(); }

    // Waits for the task that runs, drops the waiting ones (their `done` never runs), flushes the queue file. Exit (S5).
    void stop();

    // For tests: the folder of a key, and whether a file task is waiting or running.
    std::size_t waiting() const { return pool_.waiting(); }

  private:
    void post(std::function<void()> task);
    void say(bool warning, const std::string& line) const;
    Scan scanNow(const std::string& url) const;
    std::optional<Listing> listingNow(const std::string& url) const;  // url in downloadServerUrl form
    bool writeQueueNow();

    const platform::SafeFs& fs_;
    Options options_;
    Deliver deliver_;
    Log log_;
    std::atomic<bool> writesAllowed_{true};
    std::atomic<int> queueWrites_{0};
    // queue.json's newest text and what was written last (under queueMutex_).
    std::mutex queueMutex_;
    std::string queuePath_;
    std::string queueText_;
    std::uint64_t queueVersion_ = 0;
    std::uint64_t queueWritten_ = 0;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    image::WorkerPool pool_;  // last: it stops first
};

}  // namespace snx::ui
