// SPDX-License-Identifier: MIT
#include "diff.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace wjson_cli {
namespace {

using wjson::ValueRef;

// Exact: an integer equals a double only if the double is that integer.
bool numbers_equal(ValueRef a, ValueRef b) {
    if (a.is_integer() && b.is_integer()) return a.as_integer() == b.as_integer();
    if (a.is_double() && b.is_double()) return a.as_double() == b.as_double();
    const std::int64_t i = a.is_integer() ? a.as_integer() : b.as_integer();
    const double d = a.is_double() ? a.as_double() : b.as_double();
    return std::floor(d) == d && std::fabs(d) < 9.2e18 && static_cast<std::int64_t>(d) == i;
}

// Calls f(key, value) once per distinct key, in order of first appearance,
// with the value of the last duplicate (the one find() returns).
template <typename F>
void for_each_member(ValueRef object, F&& f) {
    const std::size_t n = object.size();
    std::unordered_set<std::string_view> seen;
    for (std::size_t i = 0; i < n; ++i) {
        const std::string_view key = object.key(i);
        if (!seen.insert(key).second) continue;
        f(key, *object.find(key));
    }
}

std::string escape_token(std::string_view key) {
    std::string out;
    out.reserve(key.size() + 1);
    out.push_back('/');
    for (const char c : key) {
        if (c == '~') {
            out += "~0";
        } else if (c == '/') {
            out += "~1";
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string index_token(std::size_t i) { return "/" + std::to_string(i); }

// A string that is equal for two values exactly when deep_equals is true:
// object members sorted by key, integral doubles written as integers.
void write_canonical(std::string& out, ValueRef value) {
    switch (value.type()) {
        case wjson::Type::Object: {
            std::vector<std::pair<std::string_view, ValueRef>> members;
            for_each_member(value, [&](std::string_view key, ValueRef v) { members.emplace_back(key, v); });
            std::sort(members.begin(), members.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });
            out.push_back('{');
            for (const auto& [key, v] : members) {
                out += wjson::serialize(wjson::Value(std::string(key)));
                out.push_back(':');
                write_canonical(out, v);
                out.push_back(',');
            }
            out.push_back('}');
            break;
        }
        case wjson::Type::Array: {
            out.push_back('[');
            for (std::size_t i = 0, n = value.size(); i < n; ++i) {
                write_canonical(out, value[i]);
                out.push_back(',');
            }
            out.push_back(']');
            break;
        }
        case wjson::Type::Double: {
            const double d = value.as_double();
            if (std::floor(d) == d && std::fabs(d) < 9.2e18) {
                out += std::to_string(static_cast<std::int64_t>(d));
                break;
            }
            out += wjson::serialize(value);
            break;
        }
        default:
            out += wjson::serialize(value);
            break;
    }
}

class Differ {
public:
    explicit Differ(const std::function<void(const DiffEntry&)>& report) : report_(report) {}

    std::size_t count() const noexcept { return count_; }

    void compare(ValueRef left, ValueRef right, const std::string& left_path, const std::string& right_path) {
        if (left.is_object() && right.is_object()) {
            compare_objects(left, right, left_path, right_path);
        } else if (left.is_array() && right.is_array()) {
            compare_arrays(left, right, left_path, right_path);
        } else if (!deep_equals(left, right)) {
            add({DiffKind::Changed, left_path, right_path, left, right});
        }
    }

private:
    void add(const DiffEntry& entry) {
        ++count_;
        report_(entry);
    }

    void compare_objects(ValueRef left, ValueRef right, const std::string& left_path, const std::string& right_path) {
        for_each_member(left, [&](std::string_view key, ValueRef value) {
            const std::string token = escape_token(key);
            if (const auto other = right.find(key)) {
                compare(value, *other, left_path + token, right_path + token);
            } else {
                add({DiffKind::Removed, left_path + token, {}, value, std::nullopt});
            }
        });
        for_each_member(right, [&](std::string_view key, ValueRef value) {
            if (!left.find(key)) add({DiffKind::Added, {}, right_path + escape_token(key), std::nullopt, value});
        });
    }

    void compare_arrays(ValueRef left, ValueRef right, const std::string& left_path, const std::string& right_path) {
        // Skip the common prefix and suffix: the usual case (a few edits) stays cheap.
        std::size_t start = 0;
        const std::size_t left_size = left.size();
        const std::size_t right_size = right.size();
        while (start < left_size && start < right_size && deep_equals(left[start], right[start])) ++start;
        std::size_t left_end = left_size;
        std::size_t right_end = right_size;
        while (left_end > start && right_end > start && deep_equals(left[left_end - 1], right[right_end - 1])) {
            --left_end;
            --right_end;
        }

        const std::size_t n = left_end - start;
        const std::size_t m = right_end - start;
        if (n != 0 && m > max_alignment_cells / n) {
            compare_positionally(left, right, start, left_end, right_end, left_path, right_path);
            return;
        }

        // Equal elements get the same id, so the alignment compares integers.
        std::unordered_map<std::string, std::uint32_t> ids;
        auto id = [&](ValueRef v) {
            std::string key;
            write_canonical(key, v);
            return ids.emplace(std::move(key), static_cast<std::uint32_t>(ids.size())).first->second;
        };
        std::vector<std::uint32_t> left_ids(n);
        std::vector<std::uint32_t> right_ids(m);
        for (std::size_t i = 0; i < n; ++i) left_ids[i] = id(left[start + i]);
        for (std::size_t j = 0; j < m; ++j) right_ids[j] = id(right[start + j]);

        // Longest common subsequence of the middle part; lcs[i][j] at i * (m + 1) + j.
        const std::size_t width = m + 1;
        std::vector<std::uint32_t> lcs((n + 1) * width, 0);
        for (std::size_t i = n; i-- > 0;) {
            for (std::size_t j = m; j-- > 0;) {
                lcs[i * width + j] = left_ids[i] == right_ids[j]
                                         ? lcs[(i + 1) * width + j + 1] + 1
                                         : std::max(lcs[(i + 1) * width + j], lcs[i * width + j + 1]);
            }
        }

        // Walk the alignment. Between two matches, removed and added elements
        // are paired up as changes (and compared in depth).
        std::vector<std::size_t> removed;
        std::vector<std::size_t> added;
        std::size_t a = 0;
        std::size_t b = 0;
        while (a < n || b < m) {
            if (a < n && b < m && left_ids[a] == right_ids[b]) {
                flush(left, right, removed, added, left_path, right_path);
                ++a;
                ++b;
            } else if (b < m && (a == n || lcs[a * width + b + 1] >= lcs[(a + 1) * width + b])) {
                added.push_back(start + b++);
            } else {
                removed.push_back(start + a++);
            }
        }
        flush(left, right, removed, added, left_path, right_path);
    }

    void flush(ValueRef left, ValueRef right, std::vector<std::size_t>& removed, std::vector<std::size_t>& added,
               const std::string& left_path, const std::string& right_path) {
        const std::size_t pairs = std::min(removed.size(), added.size());
        for (std::size_t k = 0; k < pairs; ++k) {
            compare(left[removed[k]], right[added[k]], left_path + index_token(removed[k]),
                    right_path + index_token(added[k]));
        }
        for (std::size_t k = pairs; k < removed.size(); ++k) {
            add({DiffKind::Removed, left_path + index_token(removed[k]), {}, left[removed[k]], std::nullopt});
        }
        for (std::size_t k = pairs; k < added.size(); ++k) {
            add({DiffKind::Added, {}, right_path + index_token(added[k]), std::nullopt, right[added[k]]});
        }
        removed.clear();
        added.clear();
    }

    void compare_positionally(ValueRef left, ValueRef right, std::size_t start, std::size_t left_end,
                              std::size_t right_end, const std::string& left_path, const std::string& right_path) {
        const std::size_t common = std::min(left_end, right_end);
        for (std::size_t i = start; i < common; ++i) {
            compare(left[i], right[i], left_path + index_token(i), right_path + index_token(i));
        }
        for (std::size_t i = common; i < left_end; ++i) {
            add({DiffKind::Removed, left_path + index_token(i), {}, left[i], std::nullopt});
        }
        for (std::size_t i = common; i < right_end; ++i) {
            add({DiffKind::Added, {}, right_path + index_token(i), std::nullopt, right[i]});
        }
    }

    const std::function<void(const DiffEntry&)>& report_;
    std::size_t count_ = 0;
};

}  // namespace

bool deep_equals(ValueRef a, ValueRef b) {
    if (a.is_number() && b.is_number()) return numbers_equal(a, b);
    if (a.type() != b.type()) return false;
    switch (a.type()) {
        case wjson::Type::Null:
            return true;
        case wjson::Type::Boolean:
            return a.as_bool() == b.as_bool();
        case wjson::Type::String:
            return a.as_string() == b.as_string();
        case wjson::Type::Array: {
            const std::size_t n = a.size();
            if (n != b.size()) return false;
            for (std::size_t i = 0; i < n; ++i) {
                if (!deep_equals(a[i], b[i])) return false;
            }
            return true;
        }
        case wjson::Type::Object: {
            bool equal = true;
            for_each_member(a, [&](std::string_view key, ValueRef value) {
                if (!equal) return;
                const auto other = b.find(key);
                equal = other && deep_equals(value, *other);
            });
            if (!equal) return false;
            for_each_member(b, [&](std::string_view key, ValueRef) {
                if (equal && !a.find(key)) equal = false;
            });
            return equal;
        }
        default:
            return false;
    }
}

std::size_t diff(ValueRef left, ValueRef right, const std::function<void(const DiffEntry&)>& report) {
    Differ differ(report);
    differ.compare(left, right, "", "");
    return differ.count();
}

std::string preview(ValueRef value, std::size_t max_length, bool ascii_only) {
    wjson::WriteOptions options;
    options.ascii_only = ascii_only;
    std::string text = wjson::serialize(value, options);
    if (text.size() <= max_length) return text;
    std::size_t cut = max_length - 3;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;  // UTF-8 boundary
    text.resize(cut);
    return text + "...";
}

std::string to_string(const DiffEntry& entry, bool ascii_only) {
    const std::string& pointer = entry.pointer();
    const std::string where = pointer.empty() ? "(root)" : pointer;
    switch (entry.kind) {
        case DiffKind::Added:
            return "+ " + where + ": " + preview(*entry.right, 80, ascii_only);
        case DiffKind::Removed:
            return "- " + where + ": " + preview(*entry.left, 80, ascii_only);
        default:
            return "~ " + where + ": " + preview(*entry.left, 60, ascii_only) + " -> " +
                   preview(*entry.right, 60, ascii_only);
    }
}

}  // namespace wjson_cli
