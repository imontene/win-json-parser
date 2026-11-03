#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "wjson/wjson.hpp"

namespace fs = std::filesystem;

namespace {

fs::path write_temp(const std::string& name, const std::string& bytes) {
    const fs::path path = fs::temp_directory_path() / name;
    std::ofstream(path, std::ios::binary) << bytes;
    return path;
}

}  // namespace

TEST_CASE("decode_text") {
    CHECK(*wjson::decode_text("{}") == "{}");
    CHECK(*wjson::decode_text("\xEF\xBB\xBF{}") == "{}");
    CHECK(*wjson::decode_text(std::string("\xFF\xFE{\0}\0", 6)) == "{}");  // UTF-16 LE
    CHECK(*wjson::decode_text(std::string("\xFE\xFF\0{\0}", 6)) == "{}");  // UTF-16 BE
    CHECK_FALSE(wjson::decode_text(std::string("\xFF\xFE{", 3)));          // odd length
}

TEST_CASE("utf16_to_utf8") {
    CHECK(*wjson::utf16_to_utf8(u"Iv\u00e1n") == "Iv\xC3\xA1n");
    CHECK(*wjson::utf16_to_utf8(u"\U0001D11E") == "\xF0\x9D\x84\x9E");
    CHECK_FALSE(wjson::utf16_to_utf8(std::u16string(1, char16_t(0xD800))));
}

TEST_CASE("line_and_column") {
    CHECK(wjson::line_and_column("abc", 0) == std::pair<std::size_t, std::size_t>{1, 1});
    CHECK(wjson::line_and_column("ab\ncd", 4) == std::pair<std::size_t, std::size_t>{2, 2});
    CHECK(wjson::line_and_column("ab\r\ncd", 5) == std::pair<std::size_t, std::size_t>{2, 2});
}

TEST_CASE("parse_file") {
    // UTF-16 LE with BOM, as written by Notepad / PowerShell 5 "Unicode".
    const std::string utf16("\xFF\xFE{\0\"\0a\0\"\0:\0" "1\0}\0", 16);
    const auto path = write_temp("wjson_test_utf16.json", utf16);
    const auto r = wjson::parse_file(path);
    REQUIRE(r);
    CHECK(r.value->find("a")->as_integer() == 1);
    fs::remove(path);

    const auto missing = wjson::parse_file(fs::temp_directory_path() / "wjson_does_not_exist.json");
    CHECK(missing.error.code == wjson::ErrorCode::IoError);
}

TEST_CASE("parse_file with a Unicode file name") {
    const fs::path path = fs::temp_directory_path() / fs::path(u"wjson_prueba_\u00f1_\u20ac.json");
    std::ofstream(path, std::ios::binary) << R"({"ok": true})";
    const auto r = wjson::parse_file(path);
    REQUIRE(r);
    CHECK(r.value->find("ok")->as_bool());
    fs::remove(path);
}
