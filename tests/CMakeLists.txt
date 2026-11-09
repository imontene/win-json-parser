# doctest is a single header; download a pinned, hash-checked copy at configure
# time. Set WJSON_DOCTEST_INCLUDE_DIR to use a local copy (offline builds).
set(WJSON_DOCTEST_VERSION 2.4.12)
set(WJSON_DOCTEST_SHA256 94029a7d32da24a56249658147dbd2b33ff0b9ed665295cbbaf19aafff5b0ced)
set(WJSON_DOCTEST_INCLUDE_DIR "" CACHE PATH "Directory containing doctest/doctest.h")

if(NOT WJSON_DOCTEST_INCLUDE_DIR)
    set(doctest_dir "${CMAKE_BINARY_DIR}/_deps/doctest-${WJSON_DOCTEST_VERSION}")
    set(doctest_header "${doctest_dir}/doctest/doctest.h")
    if(NOT EXISTS "${doctest_header}")
        message(STATUS "Downloading doctest ${WJSON_DOCTEST_VERSION}")
        file(DOWNLOAD
            "https://raw.githubusercontent.com/doctest/doctest/v${WJSON_DOCTEST_VERSION}/doctest/doctest.h"
            "${doctest_header}"
            EXPECTED_HASH SHA256=${WJSON_DOCTEST_SHA256}
            TLS_VERIFY ON
            STATUS status)
        list(GET status 0 status_code)
        if(NOT status_code EQUAL 0)
            file(REMOVE "${doctest_header}")
            message(FATAL_ERROR "Could not download doctest: ${status}. "
                                "Set WJSON_DOCTEST_INCLUDE_DIR to build offline.")
        endif()
    endif()
    set(WJSON_DOCTEST_INCLUDE_DIR "${doctest_dir}")
endif()

add_executable(wjson_tests
    main.cpp
    test_document.cpp
    test_encoding.cpp
    test_parser.cpp
    test_pointer.cpp
    test_streaming.cpp
    test_writer.cpp
    # `wjson diff` logic; it only uses the public API.
    test_diff.cpp
    ${PROJECT_SOURCE_DIR}/cli/diff.cpp
)
target_link_libraries(wjson_tests PRIVATE wjson::wjson)
target_include_directories(wjson_tests PRIVATE "${PROJECT_SOURCE_DIR}/cli")
target_compile_definitions(wjson_tests PRIVATE WJSON_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_include_directories(wjson_tests SYSTEM PRIVATE "${WJSON_DOCTEST_INCLUDE_DIR}")
set_target_properties(wjson_tests PROPERTIES CXX_EXTENSIONS OFF)
wjson_set_compiler_options(wjson_tests)

add_test(NAME unit COMMAND wjson_tests)

# C API tests, compiled as C to check that wjson.h is valid C.
enable_language(C)
add_executable(wjson_c_tests test_c_api.c)
target_link_libraries(wjson_c_tests PRIVATE wjson::wjson)
set_target_properties(wjson_c_tests PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(wjson_c_tests PRIVATE /W4 $<$<BOOL:${WJSON_WARNINGS_AS_ERRORS}>:/WX>)
else()
    target_compile_options(wjson_c_tests PRIVATE -Wall -Wextra -Wpedantic $<$<BOOL:${WJSON_WARNINGS_AS_ERRORS}>:-Werror>)
    if(WJSON_SANITIZE)
        target_compile_options(wjson_c_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(wjson_c_tests PRIVATE -fsanitize=address,undefined)
    endif()
endif()
add_test(NAME c_api COMMAND wjson_c_tests
         ${CMAKE_CURRENT_SOURCE_DIR}/data/valid.json ${CMAKE_CURRENT_SOURCE_DIR}/data/invalid.json)

# With a DLL build on Windows, copy wjson.dll next to the test executables.
if(WIN32 AND BUILD_SHARED_LIBS)
    foreach(test_target wjson_tests wjson_c_tests)
        add_custom_command(TARGET ${test_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:wjson> $<TARGET_FILE_DIR:${test_target}>)
    endforeach()
    if(TARGET wjson_cli)
        get_target_property(cli_libs wjson_cli LINK_LIBRARIES)
        if(cli_libs MATCHES "wjson")
            add_custom_command(TARGET wjson_cli POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:wjson> $<TARGET_FILE_DIR:wjson_cli>)
        endif()
    endif()
endif()

# CLI smoke tests: exit code 0 for valid JSON, 1 for invalid JSON.
if(WJSON_BUILD_CLI)
    add_test(NAME cli.validate_ok
             COMMAND wjson_cli validate ${CMAKE_CURRENT_SOURCE_DIR}/data/valid.json)
    add_test(NAME cli.validate_invalid
             COMMAND wjson_cli validate ${CMAKE_CURRENT_SOURCE_DIR}/data/invalid.json)
    set_tests_properties(cli.validate_invalid PROPERTIES WILL_FAIL TRUE)
    add_test(NAME cli.format COMMAND wjson_cli format ${CMAKE_CURRENT_SOURCE_DIR}/data/valid.json)
    add_test(NAME cli.version COMMAND wjson_cli --version)

    add_test(NAME cli.query
             COMMAND wjson_cli query --raw /nombre ${CMAKE_CURRENT_SOURCE_DIR}/data/valid.json)
    set_tests_properties(cli.query PROPERTIES PASS_REGULAR_EXPRESSION "^Iv")
    add_test(NAME cli.query_missing
             COMMAND wjson_cli query /nope ${CMAKE_CURRENT_SOURCE_DIR}/data/valid.json)
    set_tests_properties(cli.query_missing PROPERTIES WILL_FAIL TRUE)

    add_test(NAME cli.jsonc_strict
             COMMAND wjson_cli validate ${CMAKE_CURRENT_SOURCE_DIR}/data/settings.jsonc)
    set_tests_properties(cli.jsonc_strict PROPERTIES WILL_FAIL TRUE)
    add_test(NAME cli.jsonc
             COMMAND wjson_cli validate --jsonc ${CMAKE_CURRENT_SOURCE_DIR}/data/settings.jsonc)

    # diff: exit code 0 when equal, 4 when the documents differ.
    set(samples ${PROJECT_SOURCE_DIR}/gui/samples)
    add_test(NAME cli.diff_equal COMMAND wjson_cli diff ${samples}/sample.json ${samples}/sample.json)
    add_test(NAME cli.diff COMMAND wjson_cli diff ${samples}/sample.json ${samples}/sample-v2.json)
    set_tests_properties(cli.diff PROPERTIES
        PASS_REGULAR_EXPRESSION "~ /version: \"1.1.0\" -> \"1.3.0\"")
    add_test(NAME cli.diff_exit_code
             COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:wjson_cli> -DEXPECTED=4
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake
                     diff --quiet ${samples}/sample.json ${samples}/sample-v2.json)
    add_test(NAME cli.diff_invalid
             COMMAND wjson_cli diff ${samples}/sample.json ${samples}/invalid.json)
    set_tests_properties(cli.diff_invalid PROPERTIES WILL_FAIL TRUE)
endif()
