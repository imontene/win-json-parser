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
    }
    return "unknown error";
}

}  // namespace wjson
