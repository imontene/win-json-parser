// JSON Pointer (RFC 6901) helpers shared by Value and ValueRef. Internal.
// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <limits>
#include <string>
#include <string_view>

namespace wjson::detail {

// Decodes one JSON Pointer reference token ("~1" -> "/", "~0" -> "~").
// Returns false if the token contains an invalid escape.
inline bool unescape_token(std::string_view token, std::string& out) {
    out.clear();
    for (std::size_t i = 0; i < token.size(); ++i) {
        if (token[i] != '~') {
            out.push_back(token[i]);
            continue;
        }
        if (++i == token.size()) return false;
        if (token[i] == '0') out.push_back('~');
        else if (token[i] == '1') out.push_back('/');
        else return false;
    }
    return true;
}

// Parses an array index: "0" or digits without a leading zero.
inline bool parse_index(std::string_view token, std::size_t& index) noexcept {
    if (token.empty() || (token.size() > 1 && token[0] == '0')) return false;
    std::size_t value = 0;
    for (const char c : token) {
        if (c < '0' || c > '9') return false;
        const auto digit = static_cast<std::size_t>(c - '0');
        if (value > (std::numeric_limits<std::size_t>::max() - digit) / 10) return false;
        value = value * 10 + digit;
    }
    index = value;
    return true;
}


}  // namespace wjson::detail
