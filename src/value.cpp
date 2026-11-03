// SPDX-License-Identifier: MIT
#include "wjson/wjson.hpp"

namespace wjson {

double Value::as_double() const {
    if (const auto* i = std::get_if<std::int64_t>(&data_)) return static_cast<double>(*i);
    return std::get<double>(data_);
}

const Value* Value::find(std::string_view key) const noexcept {
    const auto* object = std::get_if<Object>(&data_);
    if (!object) return nullptr;
    // Reverse search: the last duplicate wins.
    for (auto it = object->rbegin(); it != object->rend(); ++it) {
        if (it->first == key) return &it->second;
    }
    return nullptr;
}

Value* Value::find(std::string_view key) noexcept {
    return const_cast<Value*>(static_cast<const Value&>(*this).find(key));
}

namespace {

// Decodes one JSON Pointer reference token ("~1" -> "/", "~0" -> "~").
// Returns false if the token contains an invalid escape.
bool unescape_token(std::string_view token, std::string& out) {
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
bool parse_index(std::string_view token, std::size_t& index) noexcept {
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

}  // namespace

const Value* Value::at_pointer(std::string_view pointer) const noexcept {
    if (pointer.empty()) return this;
    if (pointer[0] != '/') return nullptr;
    const Value* current = this;
    std::string token;
    try {
        while (!pointer.empty() && current) {
            pointer.remove_prefix(1);  // '/'
            const std::size_t end = pointer.find('/');
            const std::string_view raw = pointer.substr(0, end);
            pointer = end == std::string_view::npos ? std::string_view{} : pointer.substr(end);

            if (current->is_object()) {
                if (!unescape_token(raw, token)) return nullptr;
                current = current->find(token);
            } else if (current->is_array()) {
                std::size_t index = 0;
                // "-" (one past the end) never refers to an existing element.
                if (!parse_index(raw, index) || index >= current->as_array().size()) return nullptr;
                current = &current->as_array()[index];
            } else {
                return nullptr;
            }
        }
    } catch (...) {
        return nullptr;  // std::bad_alloc while unescaping
    }
    return current;
}

Value* Value::at_pointer(std::string_view pointer) noexcept {
    return const_cast<Value*>(static_cast<const Value&>(*this).at_pointer(pointer));
}

const char* message(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::None: return "no error";
        case ErrorCode::UnexpectedEnd: return "unexpected end of input";
        case ErrorCode::UnexpectedCharacter: return "unexpected character";
        case ErrorCode::TrailingCharacters: return "unexpected content after the JSON value";
        case ErrorCode::ExpectedKey: return "expected a string key";
        case ErrorCode::ExpectedColon: return "expected ':' after object key";
        case ErrorCode::ExpectedCommaOrBrace: return "expected ',' or '}'";
        case ErrorCode::ExpectedCommaOrBracket: return "expected ',' or ']'";
        case ErrorCode::InvalidLiteral: return "invalid literal (expected true, false or null)";
        case ErrorCode::InvalidNumber: return "invalid number";
        case ErrorCode::NumberOutOfRange: return "number is too large";
        case ErrorCode::UnterminatedString: return "unterminated string";
        case ErrorCode::ControlCharacterInString: return "unescaped control character in string";
        case ErrorCode::InvalidEscape: return "invalid escape sequence";
        case ErrorCode::InvalidUnicodeEscape: return "invalid \\u escape (expected 4 hex digits)";
        case ErrorCode::InvalidSurrogate: return "invalid or unpaired UTF-16 surrogate in \\u escape";
        case ErrorCode::InvalidUtf8: return "invalid UTF-8 byte sequence";
        case ErrorCode::InvalidEncoding: return "invalid text encoding";
        case ErrorCode::DepthLimitExceeded: return "maximum nesting depth exceeded";
        case ErrorCode::IoError: return "could not read file";
        case ErrorCode::InvalidComment: return "invalid comment (expected // or /*)";
        case ErrorCode::UnterminatedComment: return "unterminated /* comment";
        case ErrorCode::StringTooLong: return "string exceeds the maximum allowed length";
        case ErrorCode::DocumentTooLarge: return "document exceeds the maximum allowed size";
        case ErrorCode::Cancelled: return "parsing was cancelled by the handler";
    }
    return "unknown error";
}

}  // namespace wjson
