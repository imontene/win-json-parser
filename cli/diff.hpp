// Structural comparison of two JSON documents for `wjson diff`. Same rules as
// the viewer's JsonDiff (gui/Wjson.Viewer.Core/JsonDiff.cs): objects member by
// member (order does not matter), arrays aligned with a longest common
// subsequence, numbers by value (1 equals 1.0).
// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>

#include "wjson/wjson.hpp"

namespace wjson_cli {

enum class DiffKind { Added, Removed, Changed };

struct DiffEntry {
    DiffKind kind;
    std::string left_pointer;   // where the value is in the left document (empty for Added)
    std::string right_pointer;  // where the value is in the right document (empty for Removed)
    std::optional<wjson::ValueRef> left;
    std::optional<wjson::ValueRef> right;

    // The pointer to show: the left one, or the right one for Added.
    const std::string& pointer() const noexcept { return kind == DiffKind::Added ? right_pointer : left_pointer; }
};

// Arrays whose differing middle parts multiply to more than this are compared by position.
constexpr std::size_t max_alignment_cells = 4'000'000;

// Calls `report` for each difference, in document order. Returns the number of differences.
std::size_t diff(wjson::ValueRef left, wjson::ValueRef right, const std::function<void(const DiffEntry&)>& report);

// Deep equality with JSON semantics (numbers by value, objects ignoring member order).
bool deep_equals(wjson::ValueRef a, wjson::ValueRef b);

// One-line JSON text of a value, cut to about max_length bytes (on a UTF-8 boundary).
std::string preview(wjson::ValueRef value, std::size_t max_length = 80, bool ascii_only = false);

// "+ /a: 1", "- /b: 2" or "~ /c: 1 -> 2" (the root pointer is shown as "(root)").
std::string to_string(const DiffEntry& entry, bool ascii_only = false);

}  // namespace wjson_cli
