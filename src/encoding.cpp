// SPDX-License-Identifier: MIT
#include <fstream>
#include <iterator>

#include "utf8.hpp"
#include "wjson/wjson.hpp"

namespace wjson {

namespace {

std::optional<std::string> utf16_bytes_to_utf8(std::string_view bytes, bool big_endian) {
    if (bytes.size() % 2 != 0) return std::nullopt;
    std::u16string units;
    units.reserve(bytes.size() / 2);
    for (std::size_t i = 0; i < bytes.size(); i += 2) {
        const auto b0 = static_cast<unsigned char>(bytes[i]);
        const auto b1 = static_cast<unsigned char>(bytes[i + 1]);
        units.push_back(static_cast<char16_t>(big_endian ? (b0 << 8) | b1 : (b1 << 8) | b0));
    }
    return utf16_to_utf8(units);
}

bool starts_with(std::string_view s, std::string_view prefix) noexcept {
    return s.substr(0, prefix.size()) == prefix;
}

}  // namespace

std::optional<std::string> utf16_to_utf8(std::u16string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        std::uint32_t cp = text[i];
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            if (i + 1 >= text.size()) return std::nullopt;
            const std::uint32_t low = text[i + 1];
            if (low < 0xDC00 || low > 0xDFFF) return std::nullopt;
            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
            ++i;
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            return std::nullopt;
        }
        detail::append_utf8(out, cp);
    }
    return out;
}

std::optional<std::string> decode_text(std::string_view raw) {
    if (starts_with(raw, "\xEF\xBB\xBF")) return std::string(raw.substr(3));
    if (starts_with(raw, "\xFF\xFE")) return utf16_bytes_to_utf8(raw.substr(2), false);
    if (starts_with(raw, "\xFE\xFF")) return utf16_bytes_to_utf8(raw.substr(2), true);
    return std::string(raw);
}

std::pair<std::size_t, std::size_t> line_and_column(std::string_view text, std::size_t offset) noexcept {
    if (offset > text.size()) offset = text.size();
    std::size_t line = 1;
    std::size_t column = 1;
    for (std::size_t i = 0; i < offset; ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c == '\n') {
            ++line;
            column = 1;
        } else if (c == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') continue;  // CRLF counts once
            ++line;
            column = 1;
        } else if ((c & 0xC0) != 0x80) {
            ++column;  // count code points, not continuation bytes
        }
    }
    return {line, column};
}

ParseResult parse_file(const std::filesystem::path& path, const ParseOptions& options) {
    // std::ifstream takes a wide path on Windows, so Unicode file names work.
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        ParseResult result;
        result.error.code = ErrorCode::IoError;
        return result;
    }
    const std::string raw(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>{});
    auto text = decode_text(raw);
    if (!text) {
        ParseResult result;
        result.error.code = ErrorCode::InvalidEncoding;
        return result;
    }
    return parse(*text, options);
}

}  // namespace wjson
