#include "image/DiskCache.hpp"

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

namespace snx::image {

namespace {

// The file: magic (8) + URL length (4) + data length (8) + data checksum (8), little-endian, then the URL, then the data.
constexpr std::string_view kMagic = "SNXIMG1\n";
constexpr std::size_t kHeaderBytes = 8 + 4 + 8 + 8;
constexpr std::size_t kMaxUrlBytes = 4096;
constexpr std::string_view kSuffix = ".img";

std::uint64_t fnv1a(std::string_view text) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char c : text) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

void putLe(std::string& out, std::uint64_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) {
        out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
    }
}

std::uint64_t getLe(std::string_view in, std::size_t at, int bytes) {
    std::uint64_t value = 0;
    for (int i = 0; i < bytes; ++i) {
        value |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(in[at + static_cast<std::size_t>(i)])) << (8 * i);
    }
    return value;
}

bool isHex(std::string_view text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

// "<16 hex>.img" in the folder named after its first two digits.
bool isOurFile(std::string_view name, std::string_view folder) {
    return name.size() == 16 + kSuffix.size() && isHex(name.substr(0, 16)) && name.substr(16) == kSuffix &&
           name.substr(0, 2) == folder;
}

struct Parsed {
    std::string_view url;
    std::string_view data;
};

std::optional<Parsed> parse(std::string_view file) {
    if (file.size() < kHeaderBytes || file.substr(0, kMagic.size()) != kMagic) {
        return std::nullopt;
    }
    const std::uint64_t urlBytes = getLe(file, 8, 4);
    const std::uint64_t dataBytes = getLe(file, 12, 8);
    const std::uint64_t checksum = getLe(file, 20, 8);
    if (urlBytes > kMaxUrlBytes || file.size() < kHeaderBytes + urlBytes || file.size() - kHeaderBytes - urlBytes != dataBytes) {
        return std::nullopt;
    }
    Parsed parsed{file.substr(kHeaderBytes, urlBytes), file.substr(kHeaderBytes + urlBytes)};
    if (fnv1a(parsed.data) != checksum) {
        return std::nullopt;
    }
    return parsed;
}

}  // namespace

DiskCacheOptions diskCacheOptionsFrom(const core::RuntimeProfile& profile) {
    DiskCacheOptions options;
    options.budgetBytes = profile.diskCacheBytes;
    options.minFreeBytes = profile.minSdFreeBytes;
    options.maxEntryBytes = profile.maxImageBytes;
    return options;
}

DiskCache::DiskCache(const platform::SafeFs& fs, DiskCacheOptions options)
    : fs_(fs), options_(std::move(options)), index_(options_.budgetBytes) {
    if (!options_.freeBytes) {
        options_.freeBytes = [this] { return fs_.freeBytes(); };
    }
}

std::string DiskCache::keyOf(std::string_view url) {
    static const char kDigits[] = "0123456789abcdef";
    const std::uint64_t hash = fnv1a(url);
    std::string key(16, '0');
    for (int i = 0; i < 16; ++i) {
        key[static_cast<std::size_t>(15 - i)] = kDigits[(hash >> (4 * i)) & 0xF];
    }
    return key;
}

std::string DiskCache::pathOf(const std::string& key) const {
    return options_.dir + "/" + key.substr(0, 2) + "/" + key + std::string(kSuffix);
}

std::optional<std::string> DiskCache::read(std::string_view url) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!scanned_) {
        scanLocked();
    }
    const std::string key = keyOf(url);
    if (!index_.contains(key)) {
        return std::nullopt;  // a miss costs no SD card access
    }
    const std::string path = pathOf(key);
    const auto file = fs_.readFile(path, kHeaderBytes + kMaxUrlBytes + options_.maxEntryBytes);
    if (!file) {
        index_.erase(key);  // gone, or bigger than any file we write
        return std::nullopt;
    }
    const auto parsed = parse(*file);
    if (!parsed) {
        fs_.removeFile(path);  // broken (S5: the cache is disposable): it gets downloaded again
        index_.erase(key);
        return std::nullopt;
    }
    if (parsed->url != url) {
        return std::nullopt;  // another URL with the same hash; leave its file alone
    }
    index_.get(key);  // most recently used now
    return std::string(parsed->data);
}

DiskCache::Write DiskCache::write(std::string_view url, std::string_view bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopped_) {
        return Write::Stopped;
    }
    const std::uint64_t fileBytes = kHeaderBytes + url.size() + bytes.size();
    if (bytes.size() > options_.maxEntryBytes || url.size() > kMaxUrlBytes || fileBytes > options_.budgetBytes) {
        return Write::TooLarge;
    }
    if (!scanned_) {
        scanLocked();
    }
    const std::string key = keyOf(url);
    if (index_.contains(key)) {
        return Write::AlreadyThere;  // written once, never edited (S5)
    }
    const auto free = options_.freeBytes();
    if (!free || *free < fileBytes + options_.minFreeBytes) {
        return Write::NoSpace;
    }
    std::string file;
    file.reserve(fileBytes);
    file.append(kMagic);
    putLe(file, url.size(), 4);
    putLe(file, bytes.size(), 8);
    putLe(file, fnv1a(bytes), 8);
    file.append(url);
    file.append(bytes);
    if (!fs_.makeDirs(options_.dir + "/" + key.substr(0, 2)) || !fs_.writeAtomic(pathOf(key), file)) {
        return Write::Failed;
    }
    std::vector<std::string> evicted;
    index_.put(key, 0, fileBytes, &evicted);
    for (const std::string& old : evicted) {
        fs_.removeFile(pathOf(old));
    }
    return Write::Written;
}

void DiskCache::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    stopped_ = true;
}

void DiskCache::scan() {
    std::lock_guard<std::mutex> lock(mutex_);
    scanLocked();
}

std::uint64_t DiskCache::bytes() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!scanned_) {
        scanLocked();
    }
    return index_.bytes();
}

std::size_t DiskCache::count() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!scanned_) {
        scanLocked();
    }
    return index_.size();
}

void DiskCache::scanLocked() {
    scanned_ = true;
    index_.clear();
    struct Found {
        std::string key;
        std::uint64_t bytes;
        std::int64_t modified;
    };
    std::vector<Found> found;
    for (const platform::SafeFs::Entry& folder : fs_.listDirEntries(options_.dir)) {
        const std::string folderPath = options_.dir + "/" + folder.name;
        if (!folder.isDir) {
            fs_.removeFile(folderPath);  // only our sub-folders belong here (and .tmp leftovers go too)
            continue;
        }
        if (folder.name.size() != 2 || !isHex(folder.name)) {
            fs_.removeTree(folderPath);
            continue;
        }
        for (const platform::SafeFs::Entry& entry : fs_.listDirEntries(folderPath)) {
            const std::string path = folderPath + "/" + entry.name;
            if (entry.isDir) {
                fs_.removeTree(path);
            } else if (!isOurFile(entry.name, folder.name)) {
                fs_.removeFile(path);  // an interrupted write's .tmp, or a stranger
            } else {
                found.push_back(Found{entry.name.substr(0, 16), entry.size, entry.modified});
            }
        }
    }
    // Oldest first, so they are the first to go when the budget is full.
    std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) {
        return a.modified != b.modified ? a.modified < b.modified : a.key < b.key;
    });
    std::vector<std::string> evicted;
    for (const Found& file : found) {
        if (!index_.put(file.key, 0, file.bytes, &evicted)) {
            evicted.push_back(file.key);  // bigger than the whole budget (it was made smaller)
        }
    }
    for (const std::string& key : evicted) {
        fs_.removeFile(pathOf(key));
    }
}

}  // namespace snx::image
