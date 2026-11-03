# Consumer project that checks the installed CMake package (or the vcpkg
# port) can be found and linked:
#   cmake -S tests/package -B build/package -DCMAKE_PREFIX_PATH=<install prefix>
cmake_minimum_required(VERSION 3.21)
project(wjson_package_test LANGUAGES CXX)

find_package(wjson 0.5 CONFIG REQUIRED)

add_executable(package_test main.cpp)
target_link_libraries(package_test PRIVATE wjson::wjson)

enable_testing()
add_test(NAME package_test COMMAND package_test)
