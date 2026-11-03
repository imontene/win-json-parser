# Warning and sanitizer flags shared by every target in the project.
function(wjson_set_compiler_options target)
    if(MSVC)
        # Also covers clang-cl, which accepts MSVC-style flags.
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
        if(WJSON_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
            -Wnon-virtual-dtor -Wold-style-cast -Wcast-align)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            # GCC's -Wshadow wrongly flags scoped enumerators (Type::Array vs wjson::Array).
            target_compile_options(${target} PRIVATE -Wshadow)
        endif()
        if(WJSON_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
            if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
                # GCC 15 reports false positives inside std::variant at -O3; keep
                # them visible as warnings without failing the build.
                target_compile_options(${target} PRIVATE -Wno-error=maybe-uninitialized)
            endif()
        endif()
        if(WJSON_SANITIZE)
            target_compile_options(${target} PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
endfunction()
