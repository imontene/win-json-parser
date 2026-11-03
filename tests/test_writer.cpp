#include <doctest/doctest.h>

#include <cmath>
#include <limits>

#include "wjson/wjson.hpp"

using wjson::Array;
using wjson::Object;
using wjson::Value;

TEST_CASE("compact output") {
    const Value v = Object{{"a", Array{1, 2.5, true, nullptr}}, {"b", "x"}};
    CHECK(wjson::serialize(v) == R"({"a":[1,2.5,true,null],"b":"x"})");
    CHECK(wjson::serialize(Array{}) == "[]");
    CHECK(wjson::serialize(Object{}) == "{}");
}

TEST_CASE("pretty output") {
    const Value v = Object{{"a", Array{1, 2}}, {"b", Object{}}};
    wjson::WriteOptions options;
    options.indent = 2;
    CHECK(wjson::serialize(v, options) == "{\n  \"a\": [\n    1,\n    2\n  ],\n  \"b\": {}\n}");
}

TEST_CASE("numbers") {
    CHECK(wjson::serialize(Value(3.0)) == "3.0");  // stays a double on round-trip
    CHECK(wjson::serialize(Value(0.1)) == "0.1");
    CHECK(wjson::serialize(Value(1e300)) == "1e+300");
    CHECK(wjson::serialize(Value(-7)) == "-7");
    CHECK(wjson::serialize(Value(std::numeric_limits<double>::quiet_NaN())) == "null");
    CHECK(wjson::serialize(Value(std::numeric_limits<double>::infinity())) == "null");
}

TEST_CASE("string escaping") {
    CHECK(wjson::serialize(Value("a\"b\\c\n\x01")) == "\"a\\\"b\\\\c\\n\\u0001\"");
    CHECK(wjson::serialize(Value("Iv\xC3\xA1n")) == "\"Iv\xC3\xA1n\"");

    wjson::WriteOptions ascii;
    ascii.ascii_only = true;
    CHECK(wjson::serialize(Value("Iv\xC3\xA1n"), ascii) == "\"Iv\\u00e1n\"");
    CHECK(wjson::serialize(Value("\xF0\x9D\x84\x9E"), ascii) == "\"\\ud834\\udd1e\"");

    // Invalid UTF-8 in user-built strings is replaced, so output is always valid.
    CHECK(wjson::serialize(Value("\xFF")) == "\"\xEF\xBF\xBD\"");
}

TEST_CASE("round trip") {
    const char* inputs[] = {
        "{\"a\":[1,-2,3.5,1e-07,true,false,null],\"b\":{\"c\":\"\\u00e9\\ud834\\udd1e\"},\"d\":\"\"}",
        R"([[[]],{},"\"\\\/"])",
    };
    for (const char* input : inputs) {
        const auto first = wjson::parse(input);
        REQUIRE(first);
        const std::string out = wjson::serialize(*first.value);
        const auto second = wjson::parse(out);
        REQUIRE(second);
        CHECK(*first.value == *second.value);
    }
}
