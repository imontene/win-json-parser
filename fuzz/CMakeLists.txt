# Fuzz target. With Clang + libFuzzer this is a real fuzzer:
#   cmake -B build/fuzz -DCMAKE_CXX_COMPILER=clang++ -DWJSON_BUILD_FUZZERS=ON
#   build/fuzz/fuzz/wjson_fuzz -dict=fuzz/json.dict corpus/
# Elsewhere (MSVC, GCC) it builds a driver that replays files and folders.
include(CheckCXXSourceCompiles)

set(CMAKE_REQUIRED_FLAGS "-fsanitize=fuzzer")
set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=fuzzer")
check_cxx_source_compiles([[
#include <cstddef>
#include <cstdint>
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t*, std::size_t) { return 0; }
]] WJSON_HAVE_LIBFUZZER)
unset(CMAKE_REQUIRED_FLAGS)
unset(CMAKE_REQUIRED_LINK_OPTIONS)

# The library sources are compiled into the target so that they get the
# fuzzer's coverage instrumentation.
add_executable(wjson_fuzz fuzz_parse.cpp)
list(TRANSFORM WJSON_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/" OUTPUT_VARIABLE fuzz_library_sources)
set_property(TARGET wjson_fuzz PROPERTY SOURCES fuzz_parse.cpp ${fuzz_library_sources})
target_include_directories(wjson_fuzz PRIVATE
    ${PROJECT_SOURCE_DIR}/include ${WJSON_GENERATED_INCLUDE_DIR} ${PROJECT_SOURCE_DIR}/src)
target_compile_features(wjson_fuzz PRIVATE cxx_std_17)
target_compile_definitions(wjson_fuzz PRIVATE WJSON_STATIC_DEFINE WJSON_VERSION="${PROJECT_VERSION}")
set_target_properties(wjson_fuzz PROPERTIES CXX_EXTENSIONS OFF)

if(WJSON_HAVE_LIBFUZZER)
    message(STATUS "wjson_fuzz: libFuzzer + ASan + UBSan")
    set(fuzz_flags -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
    target_compile_options(wjson_fuzz PRIVATE ${fuzz_flags})
    target_link_options(wjson_fuzz PRIVATE ${fuzz_flags})
else()
    message(STATUS "wjson_fuzz: libFuzzer not available, building the standalone replay driver")
    target_sources(wjson_fuzz PRIVATE standalone_main.cpp)
    if(NOT MSVC AND WJSON_SANITIZE)
        target_compile_options(wjson_fuzz PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(wjson_fuzz PRIVATE -fsanitize=address,undefined)
    endif()
endif()
