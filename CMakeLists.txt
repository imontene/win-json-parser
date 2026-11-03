cmake_minimum_required(VERSION 3.21)

project(wjson
    VERSION 0.1.0
    DESCRIPTION "Strict, fast JSON parser for Windows"
    LANGUAGES CXX)

option(WJSON_BUILD_CLI "Build the wjson command-line tool" ${PROJECT_IS_TOP_LEVEL})
option(WJSON_BUILD_TESTS "Build the unit tests" ${PROJECT_IS_TOP_LEVEL})
option(WJSON_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(WJSON_SANITIZE "Build with AddressSanitizer and UBSan (GCC/Clang)" OFF)

# Ship a self-contained wjson.exe: link the MSVC runtime statically, unless the
# parent project (when consumed via add_subdirectory) has decided otherwise.
if(PROJECT_IS_TOP_LEVEL AND NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(cmake/CompilerOptions.cmake)

# ---------------------------------------------------------------------------
# Library: all JSON logic lives here so that the CLI, a future GUI and language
# bindings are thin front-ends over the same code.
# ---------------------------------------------------------------------------
add_library(wjson
    src/encoding.cpp
    src/parser.cpp
    src/value.cpp
    src/writer.cpp
)
add_library(wjson::wjson ALIAS wjson)
target_include_directories(wjson
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(wjson PUBLIC cxx_std_17)
set_target_properties(wjson PROPERTIES CXX_EXTENSIONS OFF)
wjson_set_compiler_options(wjson)

# ---------------------------------------------------------------------------
# Command-line tool
# ---------------------------------------------------------------------------
if(WJSON_BUILD_CLI)
    add_executable(wjson_cli cli/main.cpp)
    set_target_properties(wjson_cli PROPERTIES OUTPUT_NAME wjson CXX_EXTENSIONS OFF)
    target_link_libraries(wjson_cli PRIVATE wjson::wjson)
    target_compile_definitions(wjson_cli PRIVATE WJSON_VERSION="${PROJECT_VERSION}")
    wjson_set_compiler_options(wjson_cli)
    if(MINGW)
        # wmain() entry point and no dependency on MinGW runtime DLLs.
        target_link_options(wjson_cli PRIVATE -municode -static)
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(WJSON_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS wjson ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
if(WJSON_BUILD_CLI)
    install(TARGETS wjson_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
