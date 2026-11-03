// wjson - strict, fast JSON parser for Windows.
// SPDX-License-Identifier: TBD
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace wjson {

class Value;

using Array = std::vector<Value>;
using Member = std::pair<std::string, Value>;
// Objects keep members in document order. Duplicate keys are preserved;
// Value::find() returns the last occurrence (same behaviour as JavaScript).
using Object = std::vector<Member>;

enum class Type { Null, Boolean, Integer, Double, String, Array, Object };

// A JSON value. Strings are always stored as UTF-8.
class Value {
public:
    Value() noexcept = default;
    Value(std::nullptr_t) noexcept {}
    Value(bool b) noexcept : data_(b) {}
    template <typename T,
              std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
    Value(T i) noexcept : data_(static_cast<std::int64_t>(i)) {}
    Value(double d) noexcept : data_(d) {}
    Value(std::string s) : data_(std::move(s)) {}
    Value(std::string_view s) : data_(std::string(s)) {}
    Value(const char* s) : data_(std::string(s)) {}
    Value(Array a) : data_(std::move(a)) {}
    Value(Object o) : data_(std::move(o)) {}

    Type type() const noexcept { return static_cast<Type>(data_.index()); }

    bool is_null() const noexcept { return type() == Type::Null; }
    bool is_bool() const noexcept { return type() == Type::Boolean; }
    bool is_integer() const noexcept { return type() == Type::Integer; }
    bool is_double() const noexcept { return type() == Type::Double; }
    bool is_number() const noexcept { return is_integer() || is_double(); }
    bool is_string() const noexcept { return type() == Type::String; }
    bool is_array() const noexcept { return type() == Type::Array; }
    bool is_object() const noexcept { return type() == Type::Object; }

    // Typed accessors. They throw std::bad_variant_access on type mismatch.
    bool as_bool() const { return std::get<bool>(data_); }
    std::int64_t as_integer() const { return std::get<std::int64_t>(data_); }
    // Accepts both integers and doubles.
    double as_double() const;
    const std::string& as_string() const { return std::get<std::string>(data_); }
    std::string& as_string() { return std::get<std::string>(data_); }
    const Array& as_array() const { return std::get<Array>(data_); }
    Array& as_array() { return std::get<Array>(data_); }
    const Object& as_object() const { return std::get<Object>(data_); }
    Object& as_object() { return std::get<Object>(data_); }

    // Object lookup; returns nullptr if this is not an object or the key is missing.
    const Value* find(std::string_view key) const noexcept;
    Value* find(std::string_view key) noexcept;

    friend bool operator==(const Value& a, const Value& b) { return a.data_ == b.data_; }
    friend bool operator!=(const Value& a, const Value& b) { return !(a == b); }

private:
    // Alternative order must match enum class Type.
    std::variant<std::nullptr_t, bool, std::int64_t, double, std::string, Array, Object> data_;
};

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

// Error codes are stable so that front-ends (CLI, GUI, bindings) can localize
// their own messages instead of relying on the English text from message().
enum class ErrorCode {
    None = 0,
    UnexpectedEnd,
    UnexpectedCharacter,
    TrailingCharacters,
    ExpectedKey,
    ExpectedColon,
    ExpectedCommaOrBrace,
    ExpectedCommaOrBracket,
    InvalidLiteral,
    InvalidNumber,
    NumberOutOfRange,
    UnterminatedString,
    ControlCharacterInString,
    InvalidEscape,
    InvalidUnicodeEscape,
    InvalidSurrogate,
    InvalidUtf8,
    InvalidEncoding,
    DepthLimitExceeded,
    IoError,
};

const char* message(ErrorCode code) noexcept;

struct ParseError {
    ErrorCode code = ErrorCode::None;
    std::size_t offset = 0;  // byte offset into the UTF-8 text
    std::size_t line = 0;    // 1-based (0 when not applicable, e.g. IoError)
    std::size_t column = 0;  // 1-based, counted in Unicode code points

    const char* message() const noexcept { return wjson::message(code); }
};

struct ParseOptions {
    // Maximum nesting of arrays/objects. Protects against stack overflow on
    // hostile input such as 100000 opening brackets.
    std::size_t max_depth = 512;
};

struct ParseResult {
    std::optional<Value> value;
    ParseError error;

    explicit operator bool() const noexcept { return value.has_value(); }
};

// Parses UTF-8 text strictly according to RFC 8259.
ParseResult parse(std::string_view utf8_text, const ParseOptions& options = {});

// Reads a file, detects its encoding (UTF-8 with or without BOM, UTF-16 LE/BE
// with BOM) and parses it. I/O failures are reported as ErrorCode::IoError.
ParseResult parse_file(const std::filesystem::path& path, const ParseOptions& options = {});

// ---------------------------------------------------------------------------
// Encoding helpers
// ---------------------------------------------------------------------------

// Converts raw bytes to UTF-8 based on the BOM: strips a UTF-8 BOM and
// converts UTF-16 LE/BE. Bytes without a BOM are returned unchanged.
// Returns std::nullopt if the UTF-16 data is malformed.
std::optional<std::string> decode_text(std::string_view raw_bytes);

// Converts UTF-16 (e.g. Windows wchar_t strings) to UTF-8.
// Returns std::nullopt on unpaired surrogates.
std::optional<std::string> utf16_to_utf8(std::u16string_view text);

// Computes the 1-based line and column (in code points) of a byte offset.
std::pair<std::size_t, std::size_t> line_and_column(std::string_view text, std::size_t offset) noexcept;

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

struct WriteOptions {
    // 0 produces compact output; > 0 pretty-prints with that many spaces.
    int indent = 0;
    // Escape every non-ASCII character as \uXXXX.
    bool ascii_only = false;
};

std::string serialize(const Value& value, const WriteOptions& options = {});

}  // namespace wjson
