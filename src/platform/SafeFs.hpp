#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The write jail (docs/SAFETY.md S4/S5, D14). Every file the app creates, writes, renames or deletes goes through SafeFs, and
// every path is checked by core::jailPath() first: anything outside the app's own folder is refused. tools/safety/check-writes.sh
// fails the build if code anywhere else in src/ calls the raw functions (fopen for writing, rename, remove, mkdir, ...).
// Paths are relative to the root ("logs/app.log"), or absolute inside it. One implementation for both platforms: newlib on the
// Switch and glibc on the PC have the same POSIX calls. Thread-safe: a SafeFs never changes after it is made.
namespace snx::platform {

class SafeFs {
  public:
    // root: the app's folder with a trailing slash, normally platform::dataDir().
    explicit SafeFs(std::string root);

    const std::string& root() const { return root_; }

    // Called with the path and the reason whenever a path is refused, so the app can log it. Set once at start-up.
    static void setRefusalLogger(void (*logger)(std::string_view path, std::string_view why));

    // The full path inside the root, or nothing if it's refused.
    std::optional<std::string> resolve(std::string_view path) const;

    // Like "mkdir -p" below the root. "" means the root itself (on the Switch it already exists: the .nro lives in it).
    bool makeDirs(std::string_view dir) const;

    enum class Backup { None, Keep };
    // A whole file, the safe way (S5): <path>.tmp → write → fflush → fsync → close → rename to <path>. With Backup::Keep the old
    // file becomes <path>.bak first. On the Switch a rename can't replace a file, so the old one is moved or deleted just before;
    // if the power goes in that moment, <path>.bak (or the complete <path>.tmp) is still there.
    bool writeAtomic(std::string_view path, std::string_view data, Backup backup = Backup::None) const;

    // open → append → close, for small frequent lines (test events). Never left open. Not atomic: a crash can cut a line short.
    bool append(std::string_view path, std::string_view data) const;

    bool removeFile(std::string_view path) const;
    // Replaces `to` if it exists. Both paths must be inside the root.
    bool renameFile(std::string_view from, std::string_view to) const;
    // A folder and everything in it (at most 16 levels deep). Never the root itself. True if it's gone (or wasn't there).
    bool removeTree(std::string_view dir) const;
    // Deletes the "*.tmp" files directly in a folder: what interrupted writes left behind (S5). Returns how many.
    int removeTempFiles(std::string_view dir) const;

    // Reading isn't jailed, but keeping it here keeps all file access in one place.
    std::vector<std::string> listDir(std::string_view dir) const;  // names (no "." or ".."); empty if the folder is missing
    std::optional<std::string> readFile(std::string_view path, std::size_t maxBytes) const;  // nothing if missing or bigger
    bool exists(std::string_view path) const;

    // listDir() with what stat() says about each entry (the disk cache's start-up scan, WP-07).
    struct Entry {
        std::string name;
        bool isDir = false;
        std::uint64_t size = 0;
        std::int64_t modified = 0;  // seconds since 1970 as the file system has it (the Switch's clock may be off; 0 if unknown)
    };
    std::vector<Entry> listDirEntries(std::string_view dir) const;

    // Free bytes on the file system that holds the root (the SD card on the Switch), for S5's free-space guard. Nothing if
    // the file system can't say.
    std::optional<std::uint64_t> freeBytes() const;

    // A log file that stays open for the whole run (the logger). Starts empty. nullptr if refused or it can't be opened.
    std::FILE* openLog(std::string_view path) const;

    // A file written in pieces (the disk cache, later): it exists only as <path>.tmp until commit() gives it its real name.
    // Destroying it without commit() deletes the .tmp, so a reader never sees half a file.
    class PieceWriter {
      public:
        PieceWriter() = default;
        PieceWriter(PieceWriter&& other) noexcept;
        PieceWriter& operator=(PieceWriter&& other) noexcept;
        PieceWriter(const PieceWriter&) = delete;
        PieceWriter& operator=(const PieceWriter&) = delete;
        ~PieceWriter();

        bool isOpen() const { return file_ != nullptr; }
        bool write(std::string_view piece);
        bool commit();   // flush, fsync, close, rename <path>.tmp → <path>
        void abandon();  // close and delete <path>.tmp

      private:
        friend class SafeFs;
        std::FILE* file_ = nullptr;
        std::string tmpPath_;
        std::string finalPath_;
    };
    PieceWriter startPieces(std::string_view path) const;

    // For the crash handler (S1): the file is opened at start-up and written only after a crash, when allocating memory may
    // hang. write(), finish() and discard() never allocate. It exists as `tmpPath` until finish() renames it to `finalPath`.
    class CrashFile {
      public:
        static constexpr std::size_t kMaxPath = 512;
        CrashFile() = default;
        CrashFile(const CrashFile&) = delete;
        CrashFile& operator=(const CrashFile&) = delete;

        bool isOpen() const noexcept { return fd_ >= 0; }
        bool write(const char* data, std::size_t size) noexcept;
        bool finish() noexcept;   // fsync, close, rename to the final name
        void discard() noexcept;  // close and delete the (empty) .tmp: the run ended without a crash
        const char* finalPath() const noexcept { return finalPath_; }

      private:
        friend class SafeFs;
        int fd_ = -1;
        char tmpPath_[kMaxPath] = {};
        char finalPath_[kMaxPath] = {};
    };
    // Opens `tmpPath` (emptied) for `file`. False if either path is refused or the file can't be opened.
    bool prepareCrashFile(CrashFile& file, std::string_view tmpPath, std::string_view finalPath) const;

  private:
    std::optional<std::string> jail(std::string_view path, bool allowRoot = false) const;
    bool removeTreeAt(const std::string& full, int depth) const;

    std::string root_;
};

}  // namespace snx::platform
