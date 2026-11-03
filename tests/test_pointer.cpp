#include <doctest/doctest.h>

#include "wjson/wjson.hpp"

namespace {

// Example document from RFC 6901, section 5.
const char* const rfc_example = R"({
  "foo": ["bar", "baz"],
  "": 0,
  "a/b": 1,
  "c%d": 2,
  "e^f": 3,
  "g|h": 4,
  "i\\j": 5,
  "k\"l": 6,
  " ": 7,
  "m~n": 8
})";

}  // namespace

TEST_CASE("JSON Pointer (RFC 6901 examples)") {
    const auto r = wjson::parse(rfc_example);
    REQUIRE(r);
    const wjson::Value& doc = *r.value;

    CHECK(doc.at_pointer("") == &doc);
    CHECK(doc.at_pointer("/foo")->as_array().size() == 2);
    CHECK(doc.at_pointer("/foo/0")->as_string() == "bar");
    CHECK(doc.at_pointer("/")->as_integer() == 0);
    CHECK(doc.at_pointer("/a~1b")->as_integer() == 1);
    CHECK(doc.at_pointer("/c%d")->as_integer() == 2);
    CHECK(doc.at_pointer("/e^f")->as_integer() == 3);
    CHECK(doc.at_pointer("/g|h")->as_integer() == 4);
    CHECK(doc.at_pointer("/i\\j")->as_integer() == 5);
    CHECK(doc.at_pointer("/k\"l")->as_integer() == 6);
    CHECK(doc.at_pointer("/ ")->as_integer() == 7);
    CHECK(doc.at_pointer("/m~0n")->as_integer() == 8);
}

TEST_CASE("JSON Pointer: missing paths and malformed pointers") {
    const auto r = wjson::parse(R"({"a": [10, {"b": null}], "x": 1})");
    REQUIRE(r);
    const wjson::Value& doc = *r.value;

    CHECK(doc.at_pointer("/a/1/b")->is_null());
    CHECK(doc.at_pointer("/missing") == nullptr);
    CHECK(doc.at_pointer("/a/2") == nullptr);   // out of range
    CHECK(doc.at_pointer("/a/-") == nullptr);   // one past the end
    CHECK(doc.at_pointer("/a/01") == nullptr);  // leading zero
    CHECK(doc.at_pointer("/a/x") == nullptr);
    CHECK(doc.at_pointer("/x/y") == nullptr);   // cannot descend into a number
    CHECK(doc.at_pointer("a") == nullptr);      // must start with '/'
    CHECK(doc.at_pointer("/a~2") == nullptr);   // invalid escape
    CHECK(doc.at_pointer("/a~") == nullptr);
    CHECK(doc.at_pointer("/a/99999999999999999999999") == nullptr);

    // The non-const overload allows modification.
    wjson::Value copy = doc;
    *copy.at_pointer("/x") = "changed";
    CHECK(copy.find("x")->as_string() == "changed");
}
