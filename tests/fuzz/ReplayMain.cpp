// Without clang there is no libFuzzer. This main() runs a fuzz target once on every file in the folders it is given (the seed
// corpus and any crash files), under GCC's ASan/UBSan. That is not fuzzing, but it proves the targets build and survive the seeds.
// Usage: <target> <folder or file>...
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

namespace {

int runFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    const std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    LLVMFuzzerTestOneInput(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    int count = 0;
    for (int i = 1; i < argc; ++i) {
        const std::filesystem::path path(argv[i]);
        if (std::filesystem::is_directory(path)) {
            for (const auto& entry : std::filesystem::directory_iterator(path)) {
                if (entry.is_regular_file()) {
                    count += runFile(entry.path());
                }
            }
        } else if (std::filesystem::is_regular_file(path)) {
            count += runFile(path);
        }
    }
    std::printf("replayed %d input(s), no crash\n", count);
    return count > 0 ? 0 : 1;
}
