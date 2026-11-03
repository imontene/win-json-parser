// SPDX-License-Identifier: TBD
#include "wjson/wjson.hpp"

#include <charconv>
#include <climits>
#include <cstdint>
#include <limits>
#include <system_error>

#include "utf8.hpp"

namespace wjson {

namespace {

bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

int hex_value(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

class Parser {
public:
    Parser(std::string_view text, const ParseOptions& options) noexcept
        : text_(text), options_(options) {}

    ParseResult run() {
        ParseResult result;
        Value root;
        skip_whitespace();
        if (parse_value(root, 0)) {
            skip_whitespace();
            if (pos_ == text_.size()) {
                result.value = std::move(root);
                return result;
            }
            fail(ErrorCode::TrailingCharacters);
        }
        result.error.code = error_;
        result.error.offset = error_pos_;
        auto [line, column] = line_and_column(text_, error_pos_);
        result.error.line = line;
        result.error.column = column;
        return result;
    }

private:
    bool at_end() const noexcept { return pos_ >= text_.size(); }
    char peek() const noexcept { return text_[pos_]; }

    bool fail(ErrorCode code) noexcept { return fail_at(pos_, code); }
    bool fail_at(std::size_t pos, ErrorCode code) noexcept {
        error_ = code;
        error_pos_ = pos;
        return false;
    }

    void skip_whitespace() noexcept {
        while (!at_end()) {
            const char c = peek();
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
            ++pos_;
        }
    }

    bool parse_value(Value& out, std::size_t depth) {
        if (at_end()) return fail(ErrorCode::UnexpectedEnd);
        switch (peek()) {
            case '{': return parse_object(out, depth + 1);
            case '[': return parse_array(out, depth + 1);
            case '"': {
                std::string s;
                if (!parse_string(s)) return false;
                out = Value(std::move(s));
                return true;
            }
            case 't': return parse_literal("true", Value(true), out);
            case 'f': return parse_literal("false", Value(false), out);
            case 'n': return parse_literal("null", Value(nullptr), out);
            default:
                if (peek() == '-' || is_digit(peek())) return parse_number(out);
                return fail(ErrorCode::UnexpectedCharacter);
        }
    }

    bool parse_literal(std::string_view word, Value value, Value& out) {
        if (text_.substr(pos_, word.size()) != word) return fail(ErrorCode::InvalidLiteral);
        pos_ += word.size();
        out = std::move(value);
        return true;
    }

    bool parse_object(Value& out, std::size_t depth) {
        if (depth > options_.max_depth) return fail(ErrorCode::DepthLimitExceeded);
        ++pos_;  // '{'
        Object object;
        skip_whitespace();
        if (!at_end() && peek() == '}') {
            ++pos_;
            out = Value(std::move(object));
            return true;
        }
        for (;;) {
            skip_whitespace();
            if (at_end()) return fail(ErrorCode::UnexpectedEnd);
            if (peek() != '"') return fail(ErrorCode::ExpectedKey);
            std::string key;
            if (!parse_string(key)) return false;

            skip_whitespace();
            if (at_end()) return fail(ErrorCode::UnexpectedEnd);
            if (peek() != ':') return fail(ErrorCode::ExpectedColon);
            ++pos_;
            skip_whitespace();

            Value value;
            if (!parse_value(value, depth)) return false;
            object.emplace_back(std::move(key), std::move(value));

            skip_whitespace();
            if (at_end()) return fail(ErrorCode::UnexpectedEnd);
            const char c = peek();
            if (c == ',') {
                ++pos_;
                continue;
            }
            if (c == '}') {
                ++pos_;
                break;
            }
            return fail(ErrorCode::ExpectedCommaOrBrace);
        }
        out = Value(std::move(object));
        return true;
    }

    bool parse_array(Value& out, std::size_t depth) {
        if (depth > options_.max_depth) return fail(ErrorCode::DepthLimitExceeded);
        ++pos_;  // '['
        Array array;
        skip_whitespace();
        if (!at_end() && peek() == ']') {
            ++pos_;
            out = Value(std::move(array));
            return true;
        }
        for (;;) {
            skip_whitespace();
            Value value;
            if (!parse_value(value, depth)) return false;
            array.push_back(std::move(value));

            skip_whitespace();
            if (at_end()) return fail(ErrorCode::UnexpectedEnd);
            const char c = peek();
            if (c == ',') {
                ++pos_;
                continue;
            }
            if (c == ']') {
                ++pos_;
                break;
            }
            return fail(ErrorCode::ExpectedCommaOrBracket);
        }
        out = Value(std::move(array));
        return true;
    }

    bool read_hex4(std::uint32_t& out) noexcept {
        if (text_.size() - pos_ < 4) return fail(ErrorCode::InvalidUnicodeEscape);
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            const int h = hex_value(text_[pos_ + static_cast<std::size_t>(i)]);
            if (h < 0) return fail(ErrorCode::InvalidUnicodeEscape);
            v = (v << 4) | static_cast<std::uint32_t>(h);
        }
        pos_ += 4;
        out = v;
        return true;
    }

    bool parse_escape(std::string& out) {
        const std::size_t escape_start = pos_;
        ++pos_;  // '\\'
        if (at_end()) return fail(ErrorCode::UnterminatedString);
        const char e = text_[pos_++];
        switch (e) {
            case '"': out.push_back('"'); return true;
            case '\\': out.push_back('\\'); return true;
            case '/': out.push_back('/'); return true;
            case 'b': out.push_back('\b'); return true;
            case 'f': out.push_back('\f'); return true;
            case 'n': out.push_back('\n'); return true;
            case 'r': out.push_back('\r'); return true;
            case 't': out.push_back('\t'); return true;
            case 'u': break;
            default: return fail_at(escape_start, ErrorCode::InvalidEscape);
        }

        std::uint32_t cp = 0;
        if (!read_hex4(cp)) return false;
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            // High surrogate: must be followed by an escaped low surrogate.
            if (text_.size() - pos_ < 2 || text_[pos_] != '\\' || text_[pos_ + 1] != 'u')
                return fail_at(escape_start, ErrorCode::InvalidSurrogate);
            pos_ += 2;
            std::uint32_t low = 0;
            if (!read_hex4(low)) return false;
            if (low < 0xDC00 || low > 0xDFFF)
                return fail_at(escape_start, ErrorCode::InvalidSurrogate);
            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            return fail_at(escape_start, ErrorCode::InvalidSurrogate);
        }
        detail::append_utf8(out, cp);
        return true;
    }

    bool parse_string(std::string& out) {
        ++pos_;  // opening quote
        for (;;) {
            // Fast path: copy runs of plain ASCII in one go.
            const std::size_t run_start = pos_;
            while (!at_end()) {
                const auto c = static_cast<unsigned char>(peek());
                if (c == '"' || c == '\\' || c < 0x20 || c >= 0x80) break;
                ++pos_;
            }
            out.append(text_.data() + run_start, pos_ - run_start);

            if (at_end()) return fail(ErrorCode::UnterminatedString);
            const auto c = static_cast<unsigned char>(peek());
            if (c == '"') {
                ++pos_;
                return true;
            }
            if (c == '\\') {
                if (!parse_escape(out)) return false;
            } else if (c < 0x20) {
                return fail(ErrorCode::ControlCharacterInString);
            } else {
                const std::size_t len = detail::utf8_sequence_length(text_, pos_);
                if (len == 0) return fail(ErrorCode::InvalidUtf8);
                out.append(text_.data() + pos_, len);
                pos_ += len;
            }
        }
    }

    bool parse_number(Value& out) {
        const std::size_t start = pos_;
        bool is_integer = true;

        if (peek() == '-') ++pos_;
        if (at_end() || !is_digit(peek())) return fail_at(start, ErrorCode::InvalidNumber);
        if (peek() == '0') {
            ++pos_;
            // Leading zeros ("012") are not allowed.
            if (!at_end() && is_digit(peek())) return fail_at(start, ErrorCode::InvalidNumber);
        } else {
            while (!at_end() && is_digit(peek())) ++pos_;
        }
        if (!at_end() && peek() == '.') {
            is_integer = false;
            ++pos_;
            if (at_end() || !is_digit(peek())) return fail_at(start, ErrorCode::InvalidNumber);
            while (!at_end() && is_digit(peek())) ++pos_;
        }
        if (!at_end() && (peek() == 'e' || peek() == 'E')) {
            is_integer = false;
            ++pos_;
            if (!at_end() && (peek() == '+' || peek() == '-')) ++pos_;
            if (at_end() || !is_digit(peek())) return fail_at(start, ErrorCode::InvalidNumber);
            while (!at_end() && is_digit(peek())) ++pos_;
        }

        const char* first = text_.data() + start;
        const char* last = text_.data() + pos_;

        if (is_integer) {
            std::int64_t i = 0;
            const auto r = std::from_chars(first, last, i);
            if (r.ec == std::errc{} && r.ptr == last) {
                out = Value(i);
                return true;
            }
            // Too big for 64 bits: fall back to double.
        }

        double d = 0.0;
        const auto r = std::from_chars(first, last, d);
        if (r.ec == std::errc::result_out_of_range) {
            if (is_overflow(std::string_view(first, static_cast<std::size_t>(last - first))))
                return fail_at(start, ErrorCode::NumberOutOfRange);
            d = (*first == '-') ? -0.0 : 0.0;  // underflow rounds to zero
        } else if (r.ec != std::errc{} || r.ptr != last) {
            return fail_at(start, ErrorCode::InvalidNumber);
        }
        out = Value(d);
        return true;
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

    std::string_view text_;
    const ParseOptions& options_;
    std::size_t pos_ = 0;
    ErrorCode error_ = ErrorCode::None;
    std::size_t error_pos_ = 0;
};

}  // namespace

ParseResult parse(std::string_view utf8_text, const ParseOptions& options) {
    return Parser(utf8_text, options).run();
}

}  // namespace wjson
