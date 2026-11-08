#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>

#include "wjson/wjson.hpp"

using wjson::ErrorCode;
using wjson::ParseOptions;
using wjson::Type;

namespace {

const char* const corpus[] = {
    "null",
    "true",
    "-0",
    "123456789012345678901234567890",
    "1e-400",
    "\"\"",
    "[]",
    "{}",
    "[[[]], {}, [{}]]",
    "{\"a\": [1, -2.5e3, true, false, null, \"x\"], \"b\": {\"c\": {\"d\": []}}}",
    "{\"k\": 1, \"k\": 2, \"\": 3}",
    "{\"s\": \"Iv\xC3\xA1n \xF0\x9D\x84\x9E\", \"e\": \"q\\\"b\\\\n\\n\\t\"}",
    "  [1 ,  2 ,\n 3 ]  ",
    // errors
    "",
    "[1, 2,]",
    "{\"a\" 1}",
    "[\"\xFF\"]",
    "[1e400]",
    "{\"a\":1}}",
};

void check_same_as_value(const std::string& text, const ParseOptions& options = {}) {
    INFO("input: ", text);
    const auto value = wjson::parse(text, options);
    const auto doc = wjson::parse_document(text, options);
    REQUIRE(static_cast<bool>(value) == static_cast<bool>(doc));
    CHECK(doc.error.code == value.error.code);
    CHECK(doc.error.offset == value.error.offset);
    CHECK(doc.error.line == value.error.line);
    CHECK(doc.error.column == value.error.column);
    if (!value) return;

    const wjson::ValueRef root = doc.document->root();
    for (int indent : {0, 2}) {
        wjson::WriteOptions write;
        write.indent = indent;
        CHECK(wjson::serialize(root, write) == wjson::serialize(*value.value, write));
    }
    CHECK(root.to_value() == *value.value);
}

}  // namespace

TEST_CASE("Document matches Value on the corpus") {
    for (const char* text : corpus) check_same_as_value(text);
    check_same_as_value("// c\n[1, /* x */ 2,]", ParseOptions::jsonc());

    std::string big = "[";
    for (int i = 0; i < 3000; ++i) big += (i ? "," : "") + std::string("{\"id\":") + std::to_string(i) + ",\"t\":[1,2]}";
    big += "]";
    check_same_as_value(big);
}

TEST_CASE("Document accessors") {
    const auto r = wjson::parse_document(
        "{\"name\": \"wjson\", \"n\": 42, \"pi\": 3.5, \"ok\": true, \"none\": null, \"list\": [10, [20], {\"x\": 1}]}");
    REQUIRE(r);
    const wjson::ValueRef root = r.document->root();

    CHECK(root.type() == Type::Object);
    CHECK(root.size() == 6);
    CHECK(root.key(0) == "name");
    CHECK(root.value(0).as_string() == "wjson");
    CHECK(root.find("n")->as_integer() == 42);
    CHECK(root.find("n")->as_double() == 42.0);
    CHECK(root.find("pi")->as_double() == 3.5);
    CHECK(root.find("ok")->as_bool());
    CHECK(root.find("none")->is_null());
    CHECK_FALSE(root.find("missing"));

    const wjson::ValueRef list = *root.find("list");
    CHECK(list.is_array());
    CHECK(list.size() == 3);
    CHECK(list[0].as_integer() == 10);
    CHECK(list[1][0].as_integer() == 20);
    CHECK(list[2].find("x")->as_integer() == 1);

    CHECK(root.at_pointer("")->is_object());
    CHECK(root.at_pointer("/list/2/x")->as_integer() == 1);
    CHECK_FALSE(root.at_pointer("/list/3"));
    CHECK_FALSE(root.at_pointer("/list/01"));
    CHECK_FALSE(root.at_pointer("list"));

    // Wrong types and indexes throw, like Value.
    CHECK_THROWS_AS(root.as_string(), std::bad_variant_access);
    CHECK_THROWS_AS(list.key(0), std::bad_variant_access);
    CHECK_THROWS_AS(list[3], std::out_of_range);
    CHECK_THROWS_AS(root.value(6), std::out_of_range);
    CHECK(root.find("name")->size() == 0);  // scalars have no children

    // Identity, not content.
    CHECK(root.find("n") == root.value(1));
    CHECK(root.value(0) != root.value(1));
}

TEST_CASE("Document: duplicate keys and JSON Pointer escapes") {
    const auto r = wjson::parse_document("{\"k\": 1, \"a/b\": {\"m~n\": 2}, \"k\": 3}");
    REQUIRE(r);
    const wjson::ValueRef root = r.document->root();
    CHECK(root.size() == 3);  // duplicates are kept
    CHECK(root.find("k")->as_integer() == 3);  // the last one wins
    CHECK(root.at_pointer("/a~1b/m~0n")->as_integer() == 2);
}

TEST_CASE("Document from streams and files") {
    std::string text = "{\"items\":[";
    for (int i = 0; i < 200; ++i) text += (i ? "," : "") + std::string("\"item ") + std::to_string(i) + "\"";
    text += "]}";

    for (std::size_t buffer : {std::size_t{4}, std::size_t{7}, std::size_t{64 * 1024}}) {
        ParseOptions options;
        options.stream_buffer_size = buffer;
        std::istringstream in(text);
        const auto r = wjson::parse_document_stream(in, options);
        REQUIRE(r);
        CHECK(wjson::serialize(r.document->root()) == text);
    }

    const auto path = std::filesystem::temp_directory_path() / "wjson_document_utf16.json";
    std::ofstream(path, std::ios::binary) << std::string("\xFF\xFE[\0" "1\0]\0", 8);
    const auto f = wjson::parse_document_file(path);
    REQUIRE(f);
    CHECK(f.document->root()[0].as_integer() == 1);
    std::filesystem::remove(path);

    CHECK(wjson::parse_document_file(std::filesystem::temp_directory_path() / "wjson_missing.json").error.code ==
          ErrorCode::IoError);
}

TEST_CASE("Document: limits and errors") {
    ParseOptions options;
    options.max_depth = 2;
    CHECK(wjson::parse_document("[[1]]", options));
    CHECK(wjson::parse_document("[[[1]]]", options).error.code == ErrorCode::DepthLimitExceeded);

    options = {};
    options.max_string_length = 3;
    CHECK(wjson::parse_document("[\"abcd\"]", options).error.code == ErrorCode::StringTooLong);

    options = {};
    options.max_document_size = 4;
    CHECK(wjson::parse_document("[1, 2]", options).error.code == ErrorCode::DocumentTooLarge);

    const auto failed = wjson::parse_document("[1,");
    CHECK_FALSE(failed);
    CHECK_FALSE(failed.document.has_value());
}

TEST_CASE("Document uses less memory than a Value tree") {
    std::string text = "[";
    for (int i = 0; i < 20000; ++i)
        text += (i ? "," : "") + std::string("{\"id\":") + std::to_string(i) +
                ",\"name\":\"usuario " + std::to_string(i) + "\",\"tags\":[\"a\",\"b\"],\"score\":1.5}";
    text += "]";
    const auto r = wjson::parse_document(text);
    REQUIRE(r);
    // Value tree: at least 40 bytes per value and 72 per member, before heap
    // overhead and strings. The document must stay below that, even counting
    // the spare capacity of its arrays.
    const std::size_t value_tree_lower_bound = 20000 * (40 * 8 + 72 * 4);
    MESSAGE("document: ", r.document->memory_usage(), " bytes; Value tree > ", value_tree_lower_bound);
    CHECK(r.document->memory_usage() < value_tree_lower_bound);
}
