// Internal UTF-8 helpers. Not part of the public API.
// SPDX-License-Identifier: TBD
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace wjson::detail {

constexpr std::uint32_t replacement_character = 0xFFFD;

inline void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// Returns the length (1-4) of the well-formed UTF-8 sequence starting at
// text[i], or 0 if it is ill-formed (Unicode Standard, table 3-7).
inline std::size_t utf8_sequence_length(std::string_view text, std::size_t i) noexcept {
    auto byte = [&](std::size_t k) -> unsigned {
        return i + k < text.size() ? static_cast<unsigned char>(text[i + k]) : 0x100u;
    };
    auto cont = [](unsigned b) { return (b & 0xC0u) == 0x80u; };
    auto in = [](unsigned b, unsigned lo, unsigned hi) { return b >= lo && b <= hi; };

    const unsigned b0 = byte(0);
    if (b0 < 0x80) return 1;
    if (in(b0, 0xC2, 0xDF)) return cont(byte(1)) ? 2 : 0;
    if (b0 == 0xE0) return in(byte(1), 0xA0, 0xBF) && cont(byte(2)) ? 3 : 0;
    if (in(b0, 0xE1, 0xEC) || b0 == 0xEE || b0 == 0xEF)
        return cont(byte(1)) && cont(byte(2)) ? 3 : 0;
    if (b0 == 0xED) return in(byte(1), 0x80, 0x9F) && cont(byte(2)) ? 3 : 0;
    if (b0 == 0xF0) return in(byte(1), 0x90, 0xBF) && cont(byte(2)) && cont(byte(3)) ? 4 : 0;
    if (in(b0, 0xF1, 0xF3)) return cont(byte(1)) && cont(byte(2)) && cont(byte(3)) ? 4 : 0;
    if (b0 == 0xF4) return in(byte(1), 0x80, 0x8F) && cont(byte(2)) && cont(byte(3)) ? 4 : 0;
    return 0;
}

// Decodes the code point at text[i] and advances i. Ill-formed input yields
// U+FFFD and advances by one byte.
inline std::uint32_t decode_utf8(std::string_view text, std::size_t& i) noexcept {
    const std::size_t len = utf8_sequence_length(text, i);
    auto b = [&](std::size_t k) { return static_cast<std::uint32_t>(static_cast<unsigned char>(text[i + k])); };
    std::uint32_t cp = replacement_character;
    switch (len) {
        case 1: cp = b(0); break;
        case 2: cp = ((b(0) & 0x1F) << 6) | (b(1) & 0x3F); break;
        case 3: cp = ((b(0) & 0x0F) << 12) | ((b(1) & 0x3F) << 6) | (b(2) & 0x3F); break;
        case 4:
            cp = ((b(0) & 0x07) << 18) | ((b(1) & 0x3F) << 12) | ((b(2) & 0x3F) << 6) | (b(3) & 0x3F);
            break;
        default: break;
    }
    i += len == 0 ? 1 : len;
    return cp;
}

}  // namespace wjson::detail
