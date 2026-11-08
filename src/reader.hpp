// The JSON grammar, written once and shared by every parse function.
// Not part of the public API.
// SPDX-License-Identifier: MIT
#pragma once

#include <charconv>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <system_error>

#include "scan.hpp"
#include "sources.hpp"
#include "utf8.hpp"
#include "wjson/wjson.hpp"

// Third-party code: compiled without this project's strict warnings.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <fast_float/fast_float.h>  // src/third_party, a SYSTEM include directory
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace wjson::detail {

// Sink interface (events, each returns false to cancel):
//   null_value() boolean(bool) integer(int64_t) number(double)
//   string(std::string&) key(std::string&)   -- may move the buffer out
//   start_object() end_object() start_array() end_array()
template <typename Source, typename Sink>
class Reader {
public:
    Reader(Source& source, Sink& sink, const ParseOptions& options)
        : src_(source), sink_(sink), options_(options) {}

    ParseError run() {
        bool ok = skip_whitespace() && parse_value(0) && skip_whitespace();
        if (ok && src_.peek() >= 0) ok = fail(ErrorCode::TrailingCharacters);
        if (ok && src_.error() == ErrorCode::None) return {};
        // Errors raised by the source (I/O, encoding, size limit) explain the
        // underlying cause better than the grammar error they triggered.
        if (src_.error() != ErrorCode::None) return src_.make_error(src_.error(), src_.mark());
        return src_.make_error(error_, error_pos_);
    }

private:
    static bool is_digit(int c) noexcept { return c >= '0' && c <= '9'; }

    static int hex_value(int c) noexcept {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    bool fail(ErrorCode code) { return fail_at(src_.mark(), code); }
    bool fail_at(Position pos, ErrorCode code) noexcept {
        error_ = code;
        error_pos_ = pos;
        return false;
    }
    bool emit(bool keep_going) { return keep_going || fail(ErrorCode::Cancelled); }

    // Whitespace and, if enabled, comments.
    bool skip_whitespace() {
        for (;;) {
            // Skip the whitespace that is already buffered in one go.
            const std::string_view view = src_.available();
            const std::size_t n = whitespace_run(view.data(), view.size());
            if (n > 0) src_.skip_bytes(n);

            const int c = src_.peek();  // refills a stream when the buffer was all whitespace
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
            if (c == '/' && options_.allow_comments) {
                if (!skip_comment()) return false;
                continue;
            }
            return true;
        }
    }

    bool skip_comment() {
        const Position start = src_.mark();
        src_.advance();  // '/'
        int c = src_.peek();
        if (c == '/') {
            while ((c = src_.peek()) >= 0 && c != '\n' && c != '\r') src_.advance();
            return true;
        }
        if (c == '*') {
            src_.advance();
            for (;;) {
                c = src_.peek();
                if (c < 0) return fail_at(start, ErrorCode::UnterminatedComment);
                src_.advance();
                if (c == '*' && src_.peek() == '/') {
                    src_.advance();
                    return true;
                }
            }
        }
        return fail_at(start, ErrorCode::InvalidComment);
    }

    bool parse_value(std::size_t depth) {
        const int c = src_.peek();
        switch (c) {
            case '{': return parse_object(depth + 1);
            case '[': return parse_array(depth + 1);
            case '"':
                if (!parse_string(buf_)) return false;
                return emit(sink_.string(buf_));
            case 't': return parse_literal("true") && emit(sink_.boolean(true));
            case 'f': return parse_literal("false") && emit(sink_.boolean(false));
            case 'n': return parse_literal("null") && emit(sink_.null_value());
            default:
                if (c < 0) return fail(ErrorCode::UnexpectedEnd);
                if (c == '-' || is_digit(c)) return parse_number();
                return fail(ErrorCode::UnexpectedCharacter);
        }
    }

    bool parse_literal(std::string_view word) {
        const Position start = src_.mark();
        for (const char ch : word) {
            if (src_.peek() != static_cast<unsigned char>(ch)) return fail_at(start, ErrorCode::InvalidLiteral);
            src_.advance();
        }
        return true;
    }

    bool parse_object(std::size_t depth) {
        if (depth > options_.max_depth) return fail(ErrorCode::DepthLimitExceeded);
        src_.advance();  // '{'
        if (!emit(sink_.start_object()) || !skip_whitespace()) return false;
        if (src_.peek() == '}') {
            src_.advance();
            return emit(sink_.end_object());
        }
        for (;;) {
            if (!skip_whitespace()) return false;
            const int c = src_.peek();
            if (c < 0) return fail(ErrorCode::UnexpectedEnd);
            if (c == '}' && options_.allow_trailing_commas) {
                src_.advance();
                return emit(sink_.end_object());
            }
            if (c != '"') return fail(ErrorCode::ExpectedKey);
            if (!parse_string(buf_) || !emit(sink_.key(buf_))) return false;

            if (!skip_whitespace()) return false;
            if (src_.peek() < 0) return fail(ErrorCode::UnexpectedEnd);
            if (src_.peek() != ':') return fail(ErrorCode::ExpectedColon);
            src_.advance();
            if (!skip_whitespace() || !parse_value(depth) || !skip_whitespace()) return false;

            const int next = src_.peek();
            if (next == ',') {
                src_.advance();
                continue;
            }
            if (next == '}') {
                src_.advance();
                return emit(sink_.end_object());
            }
            if (next < 0) return fail(ErrorCode::UnexpectedEnd);
            return fail(ErrorCode::ExpectedCommaOrBrace);
        }
    }

    bool parse_array(std::size_t depth) {
        if (depth > options_.max_depth) return fail(ErrorCode::DepthLimitExceeded);
        src_.advance();  // '['
        if (!emit(sink_.start_array()) || !skip_whitespace()) return false;
        if (src_.peek() == ']') {
            src_.advance();
            return emit(sink_.end_array());
        }
        for (bool first = true;; first = false) {
            if (!skip_whitespace()) return false;
            if (!first && src_.peek() == ']' && options_.allow_trailing_commas) {
                src_.advance();
                return emit(sink_.end_array());
            }
            if (!parse_value(depth) || !skip_whitespace()) return false;

            const int next = src_.peek();
            if (next == ',') {
                src_.advance();
                continue;
            }
            if (next == ']') {
                src_.advance();
                return emit(sink_.end_array());
            }
            if (next < 0) return fail(ErrorCode::UnexpectedEnd);
            return fail(ErrorCode::ExpectedCommaOrBracket);
        }
    }

    bool read_hex4(std::uint32_t& out) {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            const int h = hex_value(src_.peek());
            if (h < 0) return fail(ErrorCode::InvalidUnicodeEscape);
            src_.advance();
            v = (v << 4) | static_cast<std::uint32_t>(h);
        }
        out = v;
        return true;
    }

    bool parse_escape(std::string& out) {
        const Position escape_start = src_.mark();
        src_.advance();  // '\\'
        const int e = src_.peek();
        if (e < 0) return fail(ErrorCode::UnterminatedString);
        char decoded = 0;
        switch (e) {
            case '"': decoded = '"'; break;
            case '\\': decoded = '\\'; break;
            case '/': decoded = '/'; break;
            case 'b': decoded = '\b'; break;
            case 'f': decoded = '\f'; break;
            case 'n': decoded = '\n'; break;
            case 'r': decoded = '\r'; break;
            case 't': decoded = '\t'; break;
            case 'u': break;
            default: return fail_at(escape_start, ErrorCode::InvalidEscape);
        }
        src_.advance();
        if (e != 'u') {
            out.push_back(decoded);
            return true;
        }

        std::uint32_t cp = 0;
        if (!read_hex4(cp)) return false;
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            // A high surrogate must be followed by an escaped low surrogate.
            if (src_.peek() != '\\') return fail_at(escape_start, ErrorCode::InvalidSurrogate);
            src_.advance();
            if (src_.peek() != 'u') return fail_at(escape_start, ErrorCode::InvalidSurrogate);
            src_.advance();
            std::uint32_t low = 0;
            if (!read_hex4(low)) return false;
            if (low < 0xDC00 || low > 0xDFFF) return fail_at(escape_start, ErrorCode::InvalidSurrogate);
            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            return fail_at(escape_start, ErrorCode::InvalidSurrogate);
        }
        append_utf8(out, cp);
        return true;
    }

    // Copies one well-formed UTF-8 sequence (Unicode table 3-7) into out.
    bool parse_utf8_sequence(std::string& out) {
        const Position start = src_.mark();
        const int b0 = src_.peek();
        int length = 0;
        int lo = 0x80;
        int hi = 0xBF;
        if (b0 >= 0xC2 && b0 <= 0xDF) {
            length = 2;
        } else if (b0 == 0xE0) {
            length = 3;
            lo = 0xA0;
        } else if ((b0 >= 0xE1 && b0 <= 0xEC) || b0 == 0xEE || b0 == 0xEF) {
            length = 3;
        } else if (b0 == 0xED) {
            length = 3;
            hi = 0x9F;  // excludes UTF-16 surrogates
        } else if (b0 == 0xF0) {
            length = 4;
            lo = 0x90;
        } else if (b0 >= 0xF1 && b0 <= 0xF3) {
            length = 4;
        } else if (b0 == 0xF4) {
            length = 4;
            hi = 0x8F;  // caps at U+10FFFF
        } else {
            return fail_at(start, ErrorCode::InvalidUtf8);
        }
        out.push_back(static_cast<char>(b0));
        src_.advance();
        for (int k = 1; k < length; ++k) {
            const int b = src_.peek();
            if (b < (k == 1 ? lo : 0x80) || b > (k == 1 ? hi : 0xBF)) return fail_at(start, ErrorCode::InvalidUtf8);
            out.push_back(static_cast<char>(b));
            src_.advance();
        }
        return true;
    }

    bool parse_string(std::string& out) {
        const Position start = src_.mark();
        out.clear();
        src_.advance();  // opening quote
        for (;;) {
            // Fast path: copy the run of ordinary characters at once.
            const std::string_view view = src_.available();
            if (const std::size_t n = plain_run(view.data(), view.size()); n > 0) {
                if (out.size() + n > options_.max_string_length) return fail_at(start, ErrorCode::StringTooLong);
                out.append(view.data(), n);
                src_.skip_plain(n);
            }

            const int c = src_.peek();
            if (c == '"') {
                src_.advance();
                return true;
            }
            if (c < 0) return fail(ErrorCode::UnterminatedString);
            if (c == '\\') {
                if (!parse_escape(out)) return false;
            } else if (c < 0x20) {
                return fail(ErrorCode::ControlCharacterInString);
            } else if (c < 0x80) {
                out.push_back(static_cast<char>(c));
                src_.advance();
            } else if (!parse_utf8_sequence(out)) {
                return false;
            }
            if (out.size() > options_.max_string_length) return fail_at(start, ErrorCode::StringTooLong);
        }
    }

    // Length of a well-formed number at the start of `s`, or 0 if the text
    // there is not one (the slow path then reports the exact error).
    static std::size_t scan_number(std::string_view s, bool& is_integer) noexcept {
        std::size_t i = 0;
        auto digit = [&](std::size_t k) { return k < s.size() && is_digit(s[k]); };
        is_integer = true;
        if (i < s.size() && s[i] == '-') ++i;
        if (!digit(i)) return 0;
        if (s[i] == '0') {
            ++i;
            if (digit(i)) return 0;  // leading zero
        } else {
            while (digit(i)) ++i;
        }
        if (i < s.size() && s[i] == '.') {
            is_integer = false;
            ++i;
            if (!digit(i)) return 0;
            while (digit(i)) ++i;
        }
        if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
            is_integer = false;
            ++i;
            if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
            if (!digit(i)) return 0;
            while (digit(i)) ++i;
        }
        return i;
    }

    struct Number {
        bool is_integer = false;
        std::int64_t integer = 0;
        double real = 0.0;
    };

    // Converts the text of a well-formed number. Errors are reported at `start`.
    bool convert_number(std::string_view num, bool is_integer, Position start, Number& out) {
        const char* first = num.data();
        const char* last = num.data() + num.size();
        if (is_integer) {
            const auto r = std::from_chars(first, last, out.integer);
            if (r.ec == std::errc{} && r.ptr == last) {
                out.is_integer = true;
                return true;
            }
            // Too big for 64 bits: fall back to double.
        }
        out.is_integer = false;
        // fast_float (Eisel-Lemire): correctly rounded like std::from_chars,
        // and several times faster than some standard libraries (e.g. MSVC's).
        const auto r = fast_float::from_chars(first, last, out.real);
        if (r.ec == std::errc::result_out_of_range || (r.ec == std::errc{} && !std::isfinite(out.real))) {
            if (is_overflow(num)) return fail_at(start, ErrorCode::NumberOutOfRange);
            out.real = num[0] == '-' ? -0.0 : 0.0;  // underflow rounds to zero
        } else if (r.ec != std::errc{} || r.ptr != last) {
            return fail_at(start, ErrorCode::InvalidNumber);
        }
        return true;
    }

    bool emit_number(const Number& n) { return emit(n.is_integer ? sink_.integer(n.integer) : sink_.number(n.real)); }

    bool parse_number() {
        // Fast path: the number is in the buffer, converted where it lies.
        const std::string_view view = src_.available();
        bool is_integer = true;
        const std::size_t length = scan_number(view, is_integer);
        if (length > 0 && (length < view.size() || Source::whole_document)) {
            Number n;
            if (!convert_number(view.substr(0, length), is_integer, src_.mark(), n)) return false;
            src_.skip_plain(length);
            return emit_number(n);
        }
        return parse_number_slow();
    }

    // Byte-by-byte path: numbers split across stream chunks, and every error.
    bool parse_number_slow() {
        const Position start = src_.mark();
        std::string& num = num_;
        num.clear();
        bool is_integer = true;
        auto take = [&] {
            num.push_back(static_cast<char>(src_.peek()));
            src_.advance();
        };
        auto take_digits = [&] {
            while (is_digit(src_.peek())) take();
        };

        if (src_.peek() == '-') take();
        if (!is_digit(src_.peek())) return fail_at(start, ErrorCode::InvalidNumber);
        if (src_.peek() == '0') {
            take();
            // Leading zeros ("012") are not allowed.
            if (is_digit(src_.peek())) return fail_at(start, ErrorCode::InvalidNumber);
        } else {
            take_digits();
        }
        if (src_.peek() == '.') {
            is_integer = false;
            take();
            if (!is_digit(src_.peek())) return fail_at(start, ErrorCode::InvalidNumber);
            take_digits();
        }
        if (src_.peek() == 'e' || src_.peek() == 'E') {
            is_integer = false;
            take();
            if (src_.peek() == '+' || src_.peek() == '-') take();
            if (!is_digit(src_.peek())) return fail_at(start, ErrorCode::InvalidNumber);
            take_digits();
        }

        Number n;
        if (!convert_number(num, is_integer, start, n)) return false;
        return emit_number(n);
    }

    // Distinguishes overflow from underflow for an out-of-range number by
    // estimating its decimal exponent.
    static bool is_overflow(std::string_view num) noexcept {
        std::size_t i = 0;
        if (num[i] == '-') ++i;
        long long magnitude = 0;  // position of the most significant digit
        bool found_nonzero = false;
        long long int_digits = 0;
        while (i < num.size() && is_digit(num[i])) {
            if (num[i] != '0' || found_nonzero) {
                found_nonzero = true;
                ++int_digits;
            }
            ++i;
        }
        if (found_nonzero) {
            magnitude = int_digits;
        } else if (i < num.size() && num[i] == '.') {
            ++i;
            long long leading_zeros = 0;
            while (i < num.size() && num[i] == '0') {
                ++leading_zeros;
                ++i;
            }
            magnitude = -leading_zeros;
        }
        while (i < num.size() && num[i] != 'e' && num[i] != 'E') ++i;
        long long exponent = 0;
        if (i < num.size()) {
            ++i;
            bool negative = false;
            if (num[i] == '+' || num[i] == '-') negative = num[i++] == '-';
            constexpr long long cap = LLONG_MAX / 20;
            while (i < num.size()) {
                exponent = exponent * 10 + (num[i++] - '0');
                if (exponent > cap) exponent = cap;
            }
            if (negative) exponent = -exponent;
        }
        return magnitude + exponent > 0;
    }

    Source& src_;
    Sink& sink_;
    const ParseOptions& options_;
    std::string buf_;  // reused string buffer
    std::string num_;  // reused number buffer
    ErrorCode error_ = ErrorCode::None;
    Position error_pos_;
};

template <typename Source, typename Sink>
ParseError read(Source& source, Sink& sink, const ParseOptions& options) {
    return Reader<Source, Sink>(source, sink, options).run();
}

}  // namespace wjson::detail
