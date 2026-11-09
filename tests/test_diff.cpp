// Tests for the structural comparison behind `wjson diff` (cli/diff.cpp).
#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "diff.hpp"
#include "wjson/wjson.hpp"

#ifndef WJSON_SOURCE_DIR
#define WJSON_SOURCE_DIR "."
#endif

using wjson_cli::DiffKind;

namespace {

wjson::Document parse(const std::string& text) {
    auto result = wjson::parse_document(text);
    REQUIRE(result);
    return std::move(*result.document);
}

std::vector<std::string> diff_lines(const std::string& left, const std::string& right) {
    const auto a = parse(left);
    const auto b = parse(right);
    std::vector<std::string> lines;
    const auto count = wjson_cli::diff(a.root(), b.root(), [&](const wjson_cli::DiffEntry& e) {
        lines.push_back(wjson_cli::to_string(e));
    });
    CHECK(count == lines.size());
    return lines;
}

bool equal(const std::string& left, const std::string& right) {
    const auto a = parse(left);
    const auto b = parse(right);
    return wjson_cli::deep_equals(a.root(), b.root());
}

}  // namespace

TEST_CASE("diff: equal documents") {
    CHECK(diff_lines(R"({"a": [1, 2], "b": null})", R"({"b": null, "a": [1, 2.0]})").empty());
    CHECK(diff_lines("[]", "[]").empty());
    CHECK(diff_lines(R"({"k": 1, "k": 2})", R"({"k": 2})").empty());  // the last duplicate wins
}

TEST_CASE("diff: object members") {
    const auto lines = diff_lines(R"({"keep": 1, "old": true, "nested": {"x": "a"}})",
                                  R"({"keep": 1, "nested": {"x": "b"}, "new": [1]})");
    CHECK(lines == std::vector<std::string>{
                       "- /old: true",
                       "~ /nested/x: \"a\" -> \"b\"",
                       "+ /new: [1]",
                   });
}

TEST_CASE("diff: arrays are aligned") {
    CHECK(diff_lines("[1, 2, 3, 4, 5]", "[1, 2, 99, 3, 4, 5]") == std::vector<std::string>{"+ /2: 99"});
    CHECK(diff_lines(R"([{"id": 1}, {"id": 2}, {"id": 3}])", R"([{"id": 1}, {"id": 3}])") ==
          std::vector<std::string>{"- /1: {\"id\":2}"});
    // A changed element is compared in depth.
    CHECK(diff_lines(R"([{"id": 1, "name": "a"}, {"id": 2, "name": "b"}])",
                     R"([{"id": 1, "name": "a"}, {"id": 2, "name": "c"}])") ==
          std::vector<std::string>{"~ /1/name: \"b\" -> \"c\""});
    CHECK(diff_lines("[1, 2]", "[]") == std::vector<std::string>{"- /0: 1", "- /1: 2"});
}

TEST_CASE("diff: type changes, root and escaping") {
    CHECK(diff_lines(R"({"a": [1]})", R"({"a": {"0": 1}})") == std::vector<std::string>{"~ /a: [1] -> {\"0\":1}"});
    CHECK(diff_lines("1", "2") == std::vector<std::string>{"~ (root): 1 -> 2"});
    CHECK(diff_lines(R"({"a/b": {"m~n": 1}})", R"({"a/b": {"m~n": 2}})") ==
          std::vector<std::string>{"~ /a~1b/m~0n: 1 -> 2"});
}

TEST_CASE("diff: numbers by exact value") {
    CHECK(equal("1", "1.0"));
    CHECK(equal("0", "-0.0"));
    CHECK_FALSE(equal("9007199254740993", "9007199254740992.0"));  // no rounding through double
    CHECK_FALSE(equal("1", "1.5"));
    CHECK_FALSE(equal("1", "\"1\""));
    CHECK(equal(R"([{"a": 1, "b": [2.0]}])", R"([{"b": [2], "a": 1.0}])"));
    CHECK_FALSE(equal(R"({"a": 1})", R"({"a": 1, "b": 2})"));
    CHECK_FALSE(equal(R"({"a": 1, "b": 2})", R"({"a": 1})"));
}

TEST_CASE("diff: long arrays fall back to positional comparison") {
    std::string left = "[";
    std::string right = "[";
    for (int i = 0; i < 3000; ++i) {
        left += (i ? "," : "") + std::to_string(i);
        right += (i ? "," : "") + std::to_string(i + 1);
    }
    left += "]";
    right += "]";
    // 3000 x 3000 cells is above the alignment limit: element by element.
    CHECK(diff_lines(left, right).size() == 3000);
}

TEST_CASE("diff: preview is cut on a UTF-8 boundary") {
    const auto doc = parse("\"" + std::string(100, 'x') + "\xC3\xA1\"");
    const auto text = wjson_cli::preview(doc.root(), 80);
    CHECK(text.size() <= 80);
    CHECK(text.substr(text.size() - 3) == "...");
    const auto accent = parse("\"" + std::string(76, 'x') + "\xC3\xA1\xC3\xA1\"");
    const auto cut = wjson_cli::preview(accent.root(), 80);
    CHECK(cut == "\"" + std::string(76, 'x') + "...");
}

TEST_CASE("diff: sample files of the viewer") {
    auto load = [](const char* name) {
        auto result = wjson::parse_document_file(std::string(WJSON_SOURCE_DIR) + "/gui/samples/" + name);
        REQUIRE(result);
        return std::move(*result.document);
    };
    const auto a = load("sample.json");
    const auto b = load("sample-v2.json");
    std::vector<std::string> found;
    wjson_cli::diff(a.root(), b.root(), [&](const wjson_cli::DiffEntry& e) {
        const char* kind = e.kind == DiffKind::Added ? "Added" : e.kind == DiffKind::Removed ? "Removed" : "Changed";
        found.push_back(std::string(kind) + " " + e.pointer());
    });
    std::sort(found.begin(), found.end());
    // Same result as the viewer's JsonDiff (gui/Wjson.Viewer.Core.Tests).
    CHECK(found == std::vector<std::string>{
                       "Added /repositorio",
                       "Added /tiendas/5",
                       "Changed /configuracion/indentacion",
                       "Changed /tiendas/2/ciudad",
                       "Changed /version",
                       "Removed /lecturas/2499",
                       "Removed /licencia",
                   });
}
