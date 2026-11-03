cmake_minimum_required(VERSION 3.21)

project(wjson
    VERSION 0.5.0
    DESCRIPTION "Strict, fast JSON parser for Windows"
    LANGUAGES CXX)

option(WJSON_BUILD_CLI "Build the wjson command-line tool" ${PROJECT_IS_TOP_LEVEL})
option(WJSON_BUILD_TESTS "Build the unit tests" ${PROJECT_IS_TOP_LEVEL})
option(WJSON_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(WJSON_SANITIZE "Build with AddressSanitizer and UBSan (GCC/Clang)" OFF)
option(WJSON_INSTALL "Generate install rules and the CMake package" ${PROJECT_IS_TOP_LEVEL})

include(cmake/CompilerOptions.cmake)

# ---------------------------------------------------------------------------
# Library: all JSON logic lives here so that the CLI, a future GUI and language
# bindings are thin front-ends over the same code.
# ---------------------------------------------------------------------------
set(WJSON_SOURCES
    src/c_api.cpp
    src/encoding.cpp
    src/parser.cpp
    src/value.cpp
    src/writer.cpp
)

# Static by default; -DBUILD_SHARED_LIBS=ON builds wjson.dll / libwjson.so.
# The generated export.h records the choice so installed headers match.
if(BUILD_SHARED_LIBS)
    set(WJSON_SHARED_LIBRARY 1)
else()
    set(WJSON_SHARED_LIBRARY 0)
endif()
set(WJSON_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/include")
configure_file(cmake/export.h.in "${WJSON_GENERATED_INCLUDE_DIR}/wjson/export.h" @ONLY)

# The library uses the default MSVC runtime (/MD), which is what consuming
# projects and package managers expect.
add_library(wjson ${WJSON_SOURCES})
add_library(wjson::wjson ALIAS wjson)
target_include_directories(wjson
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${WJSON_GENERATED_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(wjson PUBLIC cxx_std_17)
target_compile_definitions(wjson PRIVATE WJSON_BUILDING WJSON_VERSION="${PROJECT_VERSION}")
set_target_properties(wjson PROPERTIES
    CXX_EXTENSIONS OFF
    # Only symbols marked WJSON_API are exported from a shared library.
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})
wjson_set_compiler_options(wjson)

# ---------------------------------------------------------------------------
# Command-line tool
# ---------------------------------------------------------------------------
if(WJSON_BUILD_CLI)
    add_executable(wjson_cli cli/main.cpp)
    set_target_properties(wjson_cli PROPERTIES OUTPUT_NAME wjson CXX_EXTENSIONS OFF)
    if(MSVC AND NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
        # Self-contained wjson.exe: static MSVC runtime. The library sources are
        # compiled into the executable because a /MD library cannot be linked
        # into a /MT program. (A toolchain that picks the runtime, such as
        # vcpkg, takes precedence.)
        target_sources(wjson_cli PRIVATE ${WJSON_SOURCES})
        target_include_directories(wjson_cli PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include ${WJSON_GENERATED_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_compile_features(wjson_cli PRIVATE cxx_std_17)
        target_compile_definitions(wjson_cli PRIVATE WJSON_STATIC_DEFINE)
        set_target_properties(wjson_cli PROPERTIES
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        target_link_libraries(wjson_cli PRIVATE wjson::wjson)
    endif()
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
# Install and CMake package: find_package(wjson CONFIG) + wjson::wjson
# ---------------------------------------------------------------------------
if(WJSON_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)
    set(WJSON_CMAKE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/wjson")

    install(TARGETS wjson EXPORT wjsonTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(FILES "${WJSON_GENERATED_INCLUDE_DIR}/wjson/export.h" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/wjson)
    if(MSVC AND BUILD_SHARED_LIBS)
        install(FILES $<TARGET_PDB_FILE:wjson> DESTINATION ${CMAKE_INSTALL_BINDIR} OPTIONAL)
    endif()
    install(EXPORT wjsonTargets NAMESPACE wjson:: DESTINATION ${WJSON_CMAKE_DIR})

    configure_package_config_file(cmake/wjsonConfig.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/wjsonConfig.cmake"
        INSTALL_DESTINATION ${WJSON_CMAKE_DIR})
    # Before 1.0, a new minor version may break the API.
    write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/wjsonConfigVersion.cmake"
        COMPATIBILITY SameMinorVersion)
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/wjsonConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/wjsonConfigVersion.cmake"
        DESTINATION ${WJSON_CMAKE_DIR})

    if(WJSON_BUILD_CLI)
        install(TARGETS wjson_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
endif()

# ---------------------------------------------------------------------------
# Benchmarks (off by default; see scripts/fetch_benchmark_data.py)
# ---------------------------------------------------------------------------
option(WJSON_BUILD_BENCHMARKS "Build the wjson_bench benchmark" OFF)
if(WJSON_BUILD_BENCHMARKS)
    add_executable(wjson_bench bench/bench.cpp)
    target_link_libraries(wjson_bench PRIVATE wjson::wjson)
    set_target_properties(wjson_bench PROPERTIES CXX_EXTENSIONS OFF)
    wjson_set_compiler_options(wjson_bench)
endif()
