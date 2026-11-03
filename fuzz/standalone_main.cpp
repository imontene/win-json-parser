// Runs the fuzz target over files when libFuzzer is not available (MSVC,
// GCC). Useful to replay a crash or a corpus on Windows:
//   wjson_fuzz crash-1234 corpus_dir/ more_inputs/
// SPDX-License-Identifier: MIT
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

void run_file(const std::filesystem::path& path, std::size_t& count) {
    std::ifstream in(path, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>{});
    LLVMFuzzerTestOneInput(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
    ++count;
}

}  // namespace

int main(int argc, char** argv) {
    std::size_t count = 0;
    for (int i = 1; i < argc; ++i) {
        const std::filesystem::path arg(argv[i]);
        if (std::filesystem::is_directory(arg)) {
            std::vector<std::filesystem::path> files;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(arg))
                if (entry.is_regular_file()) files.push_back(entry.path());
            for (const auto& f : files) run_file(f, count);
        } else {
            run_file(arg, count);
        }
    }
    std::printf("wjson_fuzz: ran %zu inputs\n", count);
    return 0;
}
