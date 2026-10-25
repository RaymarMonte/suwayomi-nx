// The only file in src/ allowed to call the raw write functions (tools/safety/check-writes.sh). Everything below first turns
// the caller's path into a full path with core::jailPath(), which refuses anything outside the root (SAFETY.md S4).

#include "platform/SafeFs.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <utility>

#include "core/SafePath.hpp"

namespace snx::platform {

namespace {

constexpr int kMaxTreeDepth = 16;

void (*gRefusalLogger)(std::string_view, std::string_view) = nullptr;

bool isDirectory(const std::string& path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool fileExists(const std::string& path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0;
}

// mkdir() with the trailing slash taken off (newlib's sdmc: driver doesn't like it). An existing folder is fine.
bool makeOneDir(std::string path) {
    while (path.size() > 1 && path.back() == '/') {
        path.pop_back();
    }
    if (isDirectory(path)) {
        return true;
    }
    return ::mkdir(path.c_str(), 0755) == 0 || isDirectory(path);
}

// rename() that also replaces an existing target. POSIX rename does that by itself; Horizon's file system refuses, so the
// target is deleted first and the rename tried again.
bool renameReplacing(const std::string& from, const std::string& to) {
    if (std::rename(from.c_str(), to.c_str()) == 0) {
        return true;
    }
    if (!fileExists(to) || ::unlink(to.c_str()) != 0) {
        return false;
    }
    return std::rename(from.c_str(), to.c_str()) == 0;
}

bool copyPath(char (&out)[SafeFs::CrashFile::kMaxPath], const std::string& path) {
    if (path.size() + 1 > sizeof(out)) {
        return false;
    }
    std::memcpy(out, path.c_str(), path.size() + 1);
    return true;
}

}  // namespace

SafeFs::SafeFs(std::string root) : root_(std::move(root)) {
    if (root_.empty() || root_.back() != '/') {
        root_ += '/';
    }
}

void SafeFs::setRefusalLogger(void (*logger)(std::string_view, std::string_view)) {
    gRefusalLogger = logger;
}

std::optional<std::string> SafeFs::jail(std::string_view path, bool allowRoot) const {
    core::JailResult result = core::jailPath(root_, path, allowRoot);
    if (!result.path && gRefusalLogger != nullptr) {
        gRefusalLogger(path, result.why);
    }
    return std::move(result.path);
}

std::optional<std::string> SafeFs::resolve(std::string_view path) const {
    return jail(path);
}

bool SafeFs::makeDirs(std::string_view dir) const {
    const auto full = jail(dir, true);
    if (!full) {
        return false;
    }
    if (!makeOneDir(root_)) {
        return false;
    }
    // Each folder below the root in turn: root/a, root/a/b, ...
    for (std::size_t slash = full->find('/', root_.size()); slash != std::string::npos; slash = full->find('/', slash + 1)) {
        if (!makeOneDir(full->substr(0, slash))) {
            return false;
        }
    }
    return makeOneDir(*full);
}

bool SafeFs::writeAtomic(std::string_view path, std::string_view data, Backup backup) const {
    const auto full = jail(path);
    const auto tmp = jail(std::string(path) + ".tmp");
    if (!full || !tmp) {
        return false;
    }
    std::FILE* file = std::fopen(tmp->c_str(), "wb");
    if (file == nullptr) {
        return false;
    }
    bool ok = std::fwrite(data.data(), 1, data.size(), file) == data.size();
    ok = std::fflush(file) == 0 && ok;
    ok = ::fsync(::fileno(file)) == 0 && ok;
    ok = std::fclose(file) == 0 && ok;
    if (!ok) {
        ::unlink(tmp->c_str());
        return false;
    }
    if (backup == Backup::Keep && fileExists(*full)) {
        const auto bak = jail(std::string(path) + ".bak");
        if (!bak || !renameReplacing(*full, *bak)) {
            ::unlink(tmp->c_str());
            return false;
        }
    }
    return renameReplacing(*tmp, *full);
}

bool SafeFs::append(std::string_view path, std::string_view data) const {
    const auto full = jail(path);
    if (!full) {
        return false;
    }
    std::FILE* file = std::fopen(full->c_str(), "ab");
    if (file == nullptr) {
        return false;
    }
    const bool ok = std::fwrite(data.data(), 1, data.size(), file) == data.size();
    return std::fclose(file) == 0 && ok;
}

bool SafeFs::removeFile(std::string_view path) const {
    const auto full = jail(path);
    return full && ::unlink(full->c_str()) == 0;
}

bool SafeFs::renameFile(std::string_view from, std::string_view to) const {
    const auto fullFrom = jail(from);
    const auto fullTo = jail(to);
    return fullFrom && fullTo && renameReplacing(*fullFrom, *fullTo);
}

bool SafeFs::removeTree(std::string_view dir) const {
    const auto full = jail(dir);  // the root itself is refused here
    if (!full) {
        return false;
    }
    if (!fileExists(*full)) {
        return true;
    }
    return removeTreeAt(*full, 0);
}

bool SafeFs::removeTreeAt(const std::string& full, int depth) const {
    if (depth > kMaxTreeDepth) {
        return false;
    }
    struct stat st {};
#ifdef __SWITCH__
    const int statResult = ::stat(full.c_str(), &st);  // no symbolic links on the SD card
#else
    const int statResult = ::lstat(full.c_str(), &st);  // never follow a link out of the folder
#endif
    if (statResult != 0) {
        return false;
    }
    if (!S_ISDIR(st.st_mode)) {
        return ::unlink(full.c_str()) == 0;
    }
    DIR* folder = ::opendir(full.c_str());
    if (folder == nullptr) {
        return false;
    }
    std::vector<std::string> children;
    while (const dirent* entry = ::readdir(folder)) {
        const std::string_view name = entry->d_name;
        if (name != "." && name != "..") {
            children.emplace_back(name);
        }
    }
    ::closedir(folder);
    bool ok = true;
    for (const std::string& child : children) {
        ok = removeTreeAt(full + "/" + child, depth + 1) && ok;
    }
    return ::rmdir(full.c_str()) == 0 && ok;
}

int SafeFs::removeTempFiles(std::string_view dir) const {
    int removed = 0;
    const std::string prefix = dir.empty() ? std::string() : std::string(dir) + "/";
    for (const std::string& name : listDir(dir)) {
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".tmp") == 0 && removeFile(prefix + name)) {
            removed++;
        }
    }
    return removed;
}

std::vector<std::string> SafeFs::listDir(std::string_view dir) const {
    std::vector<std::string> names;
    const auto full = jail(dir, true);
    if (!full) {
        return names;
    }
    DIR* folder = ::opendir(full->c_str());
    if (folder == nullptr) {
        return names;
    }
    while (const dirent* entry = ::readdir(folder)) {
        const std::string_view name = entry->d_name;
        if (name != "." && name != "..") {
            names.emplace_back(name);
        }
    }
    ::closedir(folder);
    return names;
}

std::optional<std::string> SafeFs::readFile(std::string_view path, std::size_t maxBytes) const {
    const auto full = jail(path);
    if (!full) {
        return std::nullopt;
    }
    std::FILE* file = std::fopen(full->c_str(), "rb");
    if (file == nullptr) {
        return std::nullopt;
    }
    std::string data;
    char buffer[4096];
    std::size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (data.size() + n > maxBytes) {
            std::fclose(file);
            return std::nullopt;
        }
        data.append(buffer, n);
    }
    std::fclose(file);
    return data;
}

bool SafeFs::exists(std::string_view path) const {
    const auto full = jail(path);
    return full && fileExists(*full);
}

std::FILE* SafeFs::openLog(std::string_view path) const {
    const auto full = jail(path);
    return full ? std::fopen(full->c_str(), "w") : nullptr;
}

SafeFs::PieceWriter SafeFs::startPieces(std::string_view path) const {
    PieceWriter writer;
    const auto full = jail(path);
    const auto tmp = jail(std::string(path) + ".tmp");
    if (!full || !tmp) {
        return writer;
    }
    writer.file_ = std::fopen(tmp->c_str(), "wb");
    if (writer.file_ != nullptr) {
        writer.tmpPath_ = *tmp;
        writer.finalPath_ = *full;
    }
    return writer;
}

SafeFs::PieceWriter::PieceWriter(PieceWriter&& other) noexcept
    : file_(std::exchange(other.file_, nullptr)), tmpPath_(std::move(other.tmpPath_)), finalPath_(std::move(other.finalPath_)) {}

SafeFs::PieceWriter& SafeFs::PieceWriter::operator=(PieceWriter&& other) noexcept {
    if (this != &other) {
        abandon();
        file_ = std::exchange(other.file_, nullptr);
        tmpPath_ = std::move(other.tmpPath_);
        finalPath_ = std::move(other.finalPath_);
    }
    return *this;
}

SafeFs::PieceWriter::~PieceWriter() {
    abandon();
}

bool SafeFs::PieceWriter::write(std::string_view piece) {
    return file_ != nullptr && std::fwrite(piece.data(), 1, piece.size(), file_) == piece.size();
}

bool SafeFs::PieceWriter::commit() {
    if (file_ == nullptr) {
        return false;
    }
    bool ok = std::fflush(file_) == 0;
    ok = ::fsync(::fileno(file_)) == 0 && ok;
    ok = std::fclose(file_) == 0 && ok;
    file_ = nullptr;
    if (!ok || !renameReplacing(tmpPath_, finalPath_)) {
        ::unlink(tmpPath_.c_str());
        return false;
    }
    return true;
}

void SafeFs::PieceWriter::abandon() {
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
        ::unlink(tmpPath_.c_str());
    }
}

bool SafeFs::prepareCrashFile(CrashFile& file, std::string_view tmpPath, std::string_view finalPath) const {
    file.discard();
    const auto tmp = jail(tmpPath);
    const auto target = jail(finalPath);
    if (!tmp || !target || !copyPath(file.tmpPath_, *tmp) || !copyPath(file.finalPath_, *target)) {
        return false;
    }
    file.fd_ = ::open(file.tmpPath_, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    return file.fd_ >= 0;
}

bool SafeFs::CrashFile::write(const char* data, std::size_t size) noexcept {
    while (fd_ >= 0 && size > 0) {
        const ssize_t n = ::write(fd_, data, size);
        if (n <= 0) {
            return false;
        }
        data += n;
        size -= static_cast<std::size_t>(n);
    }
    return fd_ >= 0;
}

bool SafeFs::CrashFile::finish() noexcept {
    if (fd_ < 0) {
        return false;
    }
    ::fsync(fd_);
    ::close(fd_);
    fd_ = -1;
    return std::rename(tmpPath_, finalPath_) == 0;
}

void SafeFs::CrashFile::discard() noexcept {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
        ::unlink(tmpPath_);
    }
}

}  // namespace snx::platform
