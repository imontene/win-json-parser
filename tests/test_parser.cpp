#include <doctest/doctest.h>

#include <string>

#include "wjson/wjson.hpp"

using namespace std::string_literals;
using wjson::ErrorCode;

namespace {

wjson::Value ok(std::string_view text) {
    auto r = wjson::parse(text);
    INFO("input: ", text, " error: ", r.error.message());
    REQUIRE(r);
    return *r.value;
}

wjson::ParseError err(std::string_view text) {
    auto r = wjson::parse(text);
    INFO("input: ", text);
    REQUIRE_FALSE(r);
    return r.error;
}

}  // namespace

TEST_CASE("literals") {
    CHECK(ok("null").is_null());
    CHECK(ok("true").as_bool() == true);
    CHECK(ok(" \t\r\n false \n").as_bool() == false);
    CHECK(err("nul").code == ErrorCode::InvalidLiteral);
    CHECK(err("True").code == ErrorCode::UnexpectedCharacter);
}

TEST_CASE("numbers") {
    CHECK(ok("0").as_integer() == 0);
    CHECK(ok("-42").as_integer() == -42);
    CHECK(ok("9223372036854775807").as_integer() == INT64_MAX);
    CHECK(ok("-9223372036854775808").as_integer() == INT64_MIN);
    CHECK(ok("9223372036854775808").is_double());  // overflows int64
    CHECK(ok("1.5").as_double() == 1.5);
    CHECK(ok("-1.25e2").as_double() == -125.0);
    CHECK(ok("1E-2").as_double() == doctest::Approx(0.01));
    CHECK(ok("1e-400").as_double() == 0.0);  // underflow rounds to zero
    CHECK(ok("3").as_double() == 3.0);

    CHECK(err("1e400").code == ErrorCode::NumberOutOfRange);
    CHECK(err("01").code == ErrorCode::InvalidNumber);
    CHECK(err("1.").code == ErrorCode::InvalidNumber);
    CHECK(err(".5").code == ErrorCode::UnexpectedCharacter);
    CHECK(err("-").code == ErrorCode::InvalidNumber);
    CHECK(err("1e").code == ErrorCode::InvalidNumber);
    CHECK(err("+1").code == ErrorCode::UnexpectedCharacter);
}

TEST_CASE("strings") {
    CHECK(ok(R"("hola")").as_string() == "hola");
    CHECK(ok(R"("a\"b\\c\/d\b\f\n\r\t")").as_string() == "a\"b\\c/d\b\f\n\r\t");
    CHECK(ok("\"\\u00e9\"").as_string() == "\xC3\xA9");                 // U+00E9
    CHECK(ok("\"\\uD834\\uDD1E\"").as_string() == "\xF0\x9D\x84\x9E");   // U+1D11E
    CHECK(ok("\"Iv\xC3\xA1n \xE2\x82\xAC\"").as_string() == "Iv\xC3\xA1n \xE2\x82\xAC");
    CHECK(ok("\"\\u0000\"").as_string() == "\0"s);

    CHECK(err(R"("abc)").code == ErrorCode::UnterminatedString);
    CHECK(err("\"a\tb\"").code == ErrorCode::ControlCharacterInString);
    CHECK(err(R"("\x")").code == ErrorCode::InvalidEscape);
    CHECK(err("\"\\u12\"").code == ErrorCode::InvalidUnicodeEscape);
    CHECK(err("\"\\uD800\"").code == ErrorCode::InvalidSurrogate);
    CHECK(err("\"\\uDC00\"").code == ErrorCode::InvalidSurrogate);
    CHECK(err("\"\\uD800\\u0041\"").code == ErrorCode::InvalidSurrogate);
    CHECK(err("\"\xFF\"").code == ErrorCode::InvalidUtf8);
    CHECK(err("\"\xC0\xAF\"").code == ErrorCode::InvalidUtf8);      // overlong
    CHECK(err("\"\xED\xA0\x80\"").code == ErrorCode::InvalidUtf8);  // encoded surrogate
}

TEST_CASE("arrays and objects") {
    CHECK(ok("[]").as_array().empty());
    CHECK(ok("{}").as_object().empty());

    const auto v = ok(R"({"a": [1, 2, {"b": null}], "c": "d"})");
    REQUIRE(v.is_object());
    REQUIRE(v.find("a") != nullptr);
    CHECK(v.find("a")->as_array().size() == 3);
    CHECK(v.find("a")->as_array()[2].find("b")->is_null());
    CHECK(v.find("c")->as_string() == "d");
    CHECK(v.find("missing") == nullptr);

    // Duplicate keys are kept in order; find() returns the last one.
    const auto dup = ok(R"({"k": 1, "k": 2})");
    CHECK(dup.as_object().size() == 2);
    CHECK(dup.find("k")->as_integer() == 2);

    CHECK(err("[1, 2,]").code == ErrorCode::UnexpectedCharacter);
    CHECK(err("[1 2]").code == ErrorCode::ExpectedCommaOrBracket);
    CHECK(err(R"({"a" 1})").code == ErrorCode::ExpectedColon);
    CHECK(err(R"({"a": 1,})").code == ErrorCode::ExpectedKey);
    CHECK(err(R"({a: 1})").code == ErrorCode::ExpectedKey);
    CHECK(err(R"({"a": 1 "b": 2})").code == ErrorCode::ExpectedCommaOrBrace);
    CHECK(err("[").code == ErrorCode::UnexpectedEnd);
}

TEST_CASE("top level") {
    CHECK(err("").code == ErrorCode::UnexpectedEnd);
    CHECK(err("   ").code == ErrorCode::UnexpectedEnd);
    CHECK(err("1 2").code == ErrorCode::TrailingCharacters);
    CHECK(err("{} x").code == ErrorCode::TrailingCharacters);
    CHECK(err("\xEF\xBB\xBF{}").code == ErrorCode::UnexpectedCharacter);  // parse() is strict about BOM
}

TEST_CASE("depth limit") {
    wjson::ParseOptions options;
    options.max_depth = 3;
    CHECK(wjson::parse("[[[1]]]", options));
    const auto r = wjson::parse("[[[[1]]]]", options);
    REQUIRE_FALSE(r);
    CHECK(r.error.code == ErrorCode::DepthLimitExceeded);

    // Hostile input must fail cleanly instead of overflowing the stack.
    const std::string deep(100000, '[');
    CHECK(wjson::parse(deep).error.code == ErrorCode::DepthLimitExceeded);
}

TEST_CASE("error positions") {
    const auto e = err("{\n  \"a\": 1,\n  \"b\": [1, 2,]\n}");
    CHECK(e.code == ErrorCode::UnexpectedCharacter);
    CHECK(e.line == 3);
    CHECK(e.column == 14);
    CHECK(e.offset == 25);

    // Columns count code points, not bytes.
    const auto u = err("[\"\xC3\xA1\xC3\xA9\", x]");
    CHECK(u.line == 1);
    CHECK(u.column == 8);

    // CRLF line endings count as one line break.
    CHECK(err("[\r\n1,\r\nx]").line == 3);
}
