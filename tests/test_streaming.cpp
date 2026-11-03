#include <doctest/doctest.h>

#include <sstream>
#include <string>
#include <vector>

#include "wjson/wjson.hpp"

using wjson::ErrorCode;
using wjson::ParseOptions;

namespace {

// Records every event as text so that two parses can be compared.
class Recorder : public wjson::Handler {
public:
    std::vector<std::string> events;
    std::size_t cancel_after = static_cast<std::size_t>(-1);

    bool on_null() override { return add("null"); }
    bool on_bool(bool b) override { return add(b ? "true" : "false"); }
    bool on_integer(std::int64_t i) override { return add("int:" + std::to_string(i)); }
    bool on_double(double d) override { return add("double:" + std::to_string(d)); }
    bool on_string(std::string_view s) override { return add("str:" + std::string(s)); }
    bool on_key(std::string_view k) override { return add("key:" + std::string(k)); }
    bool on_start_object() override { return add("{"); }
    bool on_end_object() override { return add("}"); }
    bool on_start_array() override { return add("["); }
    bool on_end_array() override { return add("]"); }

private:
    bool add(std::string e) {
        events.push_back(std::move(e));
        return events.size() < cancel_after;
    }
};

struct Outcome {
    std::vector<std::string> events;
    wjson::ParseError error;
};

Outcome from_memory(std::string_view text, const ParseOptions& options = {}) {
    Recorder r;
    auto e = wjson::parse(text, r, options);
    return {r.events, e};
}

Outcome from_stream(const std::string& bytes, ParseOptions options = {}, std::size_t buffer = 64 * 1024) {
    options.stream_buffer_size = buffer;
    std::istringstream in(bytes);
    Recorder r;
    auto e = wjson::parse_stream(in, r, options);
    return {r.events, e};
}

void check_same(const Outcome& a, const Outcome& b) {
    CHECK(a.events == b.events);
    CHECK(a.error.code == b.error.code);
    CHECK(a.error.offset == b.error.offset);
    CHECK(a.error.line == b.error.line);
    CHECK(a.error.column == b.error.column);
}

const char* const corpus[] = {
    "null",
    "  [1, -2.5e3, true, false, null, \"x\"]  ",
    "{\"a\": {\"b\": [[], {}]}, \"c\": \"Iv\xC3\xA1n \xF0\x9D\x84\x9E\"}",
    ("\"esc: \\\" \\\\ \\/ \\b \\f \\n \\r \\t \\" "u00e9 \\" "uD834\\" "uDD1E\""),
    "[123456789012345678901234567890, 1e-400, 0.1]",
    // errors
    "",
    "[1, 2,]",
    "{\n  \"a\": 1,\n  \"b\" 2\n}",
    "[\"abc",
    "[\"\xFF\"]",
    "[\"\xE2\x82\"]",
    "[tru]",
    "[01]",
    "[1e400]",
    "{\"a\":1}}",
    "[\r\n1,\r\n\"a\tb\"]",
    ("[\"\\" "uD800\"]"),
};

const std::size_t buffers[] = {4, 5, 7, 64 * 1024};

}  // namespace

TEST_CASE("memory and stream parsing agree, for any buffer size") {
    for (const char* text : corpus) {
        INFO("input: ", text);
        const Outcome expected = from_memory(text);
        for (std::size_t buffer : buffers) {
            INFO("buffer: ", buffer);
            check_same(expected, from_stream(text, {}, buffer));
        }
    }
}

TEST_CASE("handler events") {
    const auto out = from_memory("{\"k\": [1, \"v\", null]}");
    CHECK(out.error.ok());
    const std::vector<std::string> expected{"{", "key:k", "[", "int:1", "str:v", "null", "]", "}"};
    CHECK(out.events == expected);
}

TEST_CASE("the default Handler validates without building a tree") {
    wjson::Handler validator;
    CHECK(wjson::parse("[1, {\"a\": true}]", validator).ok());
    CHECK(wjson::parse("[1, {\"a\": tru}]", validator).code == ErrorCode::InvalidLiteral);
}

TEST_CASE("cancellation") {
    Recorder r;
    r.cancel_after = 2;
    const auto e = wjson::parse("[1, 2, 3]", r);
    CHECK(e.code == ErrorCode::Cancelled);
    CHECK(r.events.size() == 2);
}

TEST_CASE("stream encodings") {
    const std::string utf16le("\xFF\xFE[\0\"\0" "a\0\"\0]\0", 12);
    const std::string utf16be("\xFE\xFF\0[\0\"\0a\0\"\0]", 12);
    const std::vector<std::string> expected{"[", "str:a", "]"};
    for (std::size_t buffer : {std::size_t{4}, std::size_t{5}, std::size_t{64 * 1024}}) {
        CHECK(from_stream(utf16le, {}, buffer).events == expected);
        CHECK(from_stream(utf16be, {}, buffer).events == expected);
        CHECK(from_stream("\xEF\xBB\xBF[\"a\"]", {}, buffer).events == expected);
    }

    // Surrogate pair U+1D11E (D834 DD1E) split across 4-byte buffers.
    const std::string clef("\xFF\xFE\"\0\x34\xD8\x1E\xDD\"\0", 10);
    for (std::size_t buffer : {std::size_t{4}, std::size_t{5}, std::size_t{6}, std::size_t{7}}) {
        const auto out = from_stream(clef, {}, buffer);
        REQUIRE(out.error.ok());
        CHECK(out.events == std::vector<std::string>{"str:\xF0\x9D\x84\x9E"});
    }

    CHECK(from_stream(std::string("\xFF\xFE\"\0\x34\xD8\"\0", 8)).error.code == ErrorCode::InvalidEncoding);
    CHECK(from_stream(std::string("\xFF\xFE[\0]", 5)).error.code == ErrorCode::InvalidEncoding);  // odd size
}

TEST_CASE("DOM from a stream") {
    std::istringstream in("{\"a\": [1, 2]}");
    const auto r = wjson::parse_stream(in);
    REQUIRE(r);
    CHECK(r.value->find("a")->as_array().size() == 2);
}

TEST_CASE("document size limit") {
    ParseOptions options;
    options.max_document_size = 7;
    CHECK(wjson::parse("[1,2,3]", options));
    CHECK(from_stream("[1,2,3]", options, 4).error.ok());

    const auto r = wjson::parse("[1,2,3] ", options);
    CHECK(r.error.code == ErrorCode::DocumentTooLarge);
    CHECK(r.error.offset == 7);
    for (std::size_t buffer : {std::size_t{4}, std::size_t{5}, std::size_t{64 * 1024}}) {
        const auto s = from_stream("[1,2,3] ", options, buffer);
        CHECK(s.error.code == ErrorCode::DocumentTooLarge);
        CHECK(s.error.offset == 7);
    }
    CHECK(wjson::parse("[1,2,3,4,5]", options).error.code == ErrorCode::DocumentTooLarge);
}

TEST_CASE("string length limit") {
    ParseOptions options;
    options.max_string_length = 3;
    CHECK(wjson::parse("[\"abc\"]", options));
    CHECK(wjson::parse("{\"abc\": 1}", options));
    const auto e = wjson::parse("[\"abcd\"]", options).error;
    CHECK(e.code == ErrorCode::StringTooLong);
    CHECK(e.column == 2);  // reported at the opening quote
    CHECK(wjson::parse("{\"abcd\": 1}", options).error.code == ErrorCode::StringTooLong);
}

TEST_CASE("JSONC: comments and trailing commas") {
    const char* text =
        "// VS Code style settings\n"
        "{\n"
        "  /* block\n     comment */ \"a\": [1, 2,], // trailing\n"
        "  \"b\": {\"c\": true,},\n"
        "}\n";
    CHECK_FALSE(wjson::parse(text));  // strict by default

    const auto r = wjson::parse(text, ParseOptions::jsonc());
    REQUIRE(r);
    CHECK(r.value->find("a")->as_array().size() == 2);
    CHECK(r.value->find("b")->find("c")->as_bool());
    for (std::size_t buffer : {std::size_t{4}, std::size_t{5}, std::size_t{64 * 1024}}) check_same(from_memory(text, ParseOptions::jsonc()),
                                                            from_stream(text, ParseOptions::jsonc(), buffer));

    ParseOptions comments_only;
    comments_only.allow_comments = true;
    CHECK(wjson::parse("[1 /* x */, 2] // end", comments_only));
    CHECK(wjson::parse("[1, 2,]", comments_only).error.code == ErrorCode::UnexpectedCharacter);

    CHECK(wjson::parse("[1, /x]", comments_only).error.code == ErrorCode::InvalidComment);
    CHECK(wjson::parse("[1] /* open", comments_only).error.code == ErrorCode::UnterminatedComment);
    CHECK(wjson::parse("[/**/1/***/]", comments_only));

    // Trailing commas never allow empty elements.
    CHECK_FALSE(wjson::parse("[,]", ParseOptions::jsonc()));
    CHECK_FALSE(wjson::parse("[1,,]", ParseOptions::jsonc()));
    CHECK_FALSE(wjson::parse("{,}", ParseOptions::jsonc()));
}
