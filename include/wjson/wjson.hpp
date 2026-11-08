// wjson - strict, fast JSON parser for Windows.
// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "wjson/export.h"

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
    WJSON_API double as_double() const;
    const std::string& as_string() const { return std::get<std::string>(data_); }
    std::string& as_string() { return std::get<std::string>(data_); }
    const Array& as_array() const { return std::get<Array>(data_); }
    Array& as_array() { return std::get<Array>(data_); }
    const Object& as_object() const { return std::get<Object>(data_); }
    Object& as_object() { return std::get<Object>(data_); }

    // Object lookup; returns nullptr if this is not an object or the key is missing.
    WJSON_API const Value* find(std::string_view key) const noexcept;
    WJSON_API Value* find(std::string_view key) noexcept;

    // JSON Pointer lookup (RFC 6901), e.g. "/users/0/name". "" is the value
    // itself; "~0" and "~1" escape '~' and '/'. Returns nullptr if the path
    // does not exist or the pointer is malformed.
    WJSON_API const Value* at_pointer(std::string_view pointer) const noexcept;
    WJSON_API Value* at_pointer(std::string_view pointer) noexcept;

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
    InvalidComment,
    UnterminatedComment,
    StringTooLong,
    DocumentTooLarge,
    Cancelled,
};

WJSON_API const char* message(ErrorCode code) noexcept;

struct ParseError {
    ErrorCode code = ErrorCode::None;
    std::size_t offset = 0;  // byte offset into the UTF-8 text
    std::size_t line = 0;    // 1-based (0 when not applicable, e.g. IoError)
    std::size_t column = 0;  // 1-based, counted in Unicode code points

    bool ok() const noexcept { return code == ErrorCode::None; }
    const char* message() const noexcept { return wjson::message(code); }
};

inline constexpr std::size_t unlimited = std::numeric_limits<std::size_t>::max();

struct ParseOptions {
    // --- Security limits (protect against hostile or corrupt input) ---
    // Maximum nesting of arrays/objects; prevents stack overflow on input such
    // as 100000 opening brackets.
    std::size_t max_depth = 512;
    // Maximum length in bytes (UTF-8) of a single string or object key.
    std::size_t max_string_length = unlimited;
    // Maximum size in bytes of the (UTF-8 decoded) document.
    std::size_t max_document_size = unlimited;

    // --- Extensions (all off by default: strict RFC 8259) ---
    // Allow // line and /* block */ comments.
    bool allow_comments = false;
    // Allow a comma after the last element of an array or object.
    bool allow_trailing_commas = false;

    // Buffer size used by the streaming functions (parse_stream, the
    // Handler overload of parse_file).
    std::size_t stream_buffer_size = 64 * 1024;

    // JSON with Comments, as used by VS Code and Windows Terminal settings.
    static ParseOptions jsonc() {
        ParseOptions options;
        options.allow_comments = true;
        options.allow_trailing_commas = true;
        return options;
    }
};

struct ParseResult {
    std::optional<Value> value;
    ParseError error;

    explicit operator bool() const noexcept { return value.has_value(); }
};

// Parses UTF-8 text into a tree (DOM). Strict RFC 8259 unless extensions
// are enabled in the options. A BOM is not accepted here; see decode_text().
WJSON_API ParseResult parse(std::string_view utf8_text, const ParseOptions& options = {});

// Reads a whole file, detects its encoding (UTF-8 with or without BOM, UTF-16
// LE/BE with BOM) and parses it into a tree. I/O failures are reported as
// ErrorCode::IoError.
WJSON_API ParseResult parse_file(const std::filesystem::path& path, const ParseOptions& options = {});

// Like parse_file() but reads from a stream (for example std::cin).
WJSON_API ParseResult parse_stream(std::istream& in, const ParseOptions& options = {});

// ---------------------------------------------------------------------------
// Streaming (SAX) parsing
// ---------------------------------------------------------------------------

// Receives parse events in document order. Every callback returns true to
// continue or false to stop parsing, which reports ErrorCode::Cancelled.
// The default implementation accepts everything, so a plain Handler can be
// used to validate a document without building a tree.
//
// String views passed to on_string()/on_key() are only valid during the call.
class Handler {
public:
    virtual ~Handler() = default;
    virtual bool on_null() { return true; }
    virtual bool on_bool(bool) { return true; }
    virtual bool on_integer(std::int64_t) { return true; }
    virtual bool on_double(double) { return true; }
    virtual bool on_string(std::string_view) { return true; }
    virtual bool on_key(std::string_view) { return true; }
    virtual bool on_start_object() { return true; }
    virtual bool on_end_object() { return true; }
    virtual bool on_start_array() { return true; }
    virtual bool on_end_array() { return true; }
};

// Parses UTF-8 text and sends events to the handler.
WJSON_API ParseError parse(std::string_view utf8_text, Handler& handler, const ParseOptions& options = {});

// Parses a stream in chunks with constant memory use, regardless of the
// document size. Detects UTF-8 (with or without BOM) and UTF-16 LE/BE (with
// BOM). Error offsets refer to the UTF-8 decoded text.
WJSON_API ParseError parse_stream(std::istream& in, Handler& handler, const ParseOptions& options = {});

// Streams a file through the handler (constant memory, see parse_stream).
WJSON_API ParseError parse_file(const std::filesystem::path& path, Handler& handler,
                                const ParseOptions& options = {});

// ---------------------------------------------------------------------------
// Compact read-only documents
// ---------------------------------------------------------------------------

class Document;

namespace detail {
// One value of a Document (16 bytes). Internal; use ValueRef.
struct DocumentNode {
    std::uint8_t type;      // wjson::Type
    std::uint8_t reserved[3];
    std::uint32_t count;    // string length, or number of elements / members
    std::uint64_t payload;  // bool, int64 or double bits, string offset, or first link
};
// One object member: key in the string buffer and the value's node.
struct DocumentMember {
    std::uint32_t key_offset;
    std::uint32_t key_length;
    std::uint32_t value;
};
struct DocumentAccess;  // used by the parser to fill a Document
}  // namespace detail

// A read-only view of one value inside a Document: two words, cheap to copy.
// It stays valid as long as the Document it came from exists and is not
// moved from. Accessors throw std::bad_variant_access on a type mismatch and
// std::out_of_range for an index past the end, like Value.
class ValueRef {
public:
    Type type() const noexcept { return static_cast<Type>(node().type); }
    bool is_null() const noexcept { return type() == Type::Null; }
    bool is_bool() const noexcept { return type() == Type::Boolean; }
    bool is_integer() const noexcept { return type() == Type::Integer; }
    bool is_double() const noexcept { return type() == Type::Double; }
    bool is_number() const noexcept { return is_integer() || is_double(); }
    bool is_string() const noexcept { return type() == Type::String; }
    bool is_array() const noexcept { return type() == Type::Array; }
    bool is_object() const noexcept { return type() == Type::Object; }

    WJSON_API bool as_bool() const;
    WJSON_API std::int64_t as_integer() const;
    // Accepts both integers and doubles.
    WJSON_API double as_double() const;
    WJSON_API std::string_view as_string() const;

    // Number of elements of an array or members of an object; 0 otherwise.
    WJSON_API std::size_t size() const noexcept;
    // Array element.
    WJSON_API ValueRef operator[](std::size_t index) const;
    // Object member by position, in document order.
    WJSON_API std::string_view key(std::size_t index) const;
    WJSON_API ValueRef value(std::size_t index) const;
    // Object lookup; the last duplicate wins. std::nullopt if missing or not an object.
    WJSON_API std::optional<ValueRef> find(std::string_view key) const noexcept;
    // JSON Pointer (RFC 6901); std::nullopt if there is no value at the path.
    WJSON_API std::optional<ValueRef> at_pointer(std::string_view pointer) const noexcept;

    // Copies this value and everything below it into a mutable Value tree.
    WJSON_API Value to_value() const;

    // True if both refer to the same value of the same Document (identity, not content).
    friend bool operator==(ValueRef a, ValueRef b) noexcept { return a.doc_ == b.doc_ && a.index_ == b.index_; }
    friend bool operator!=(ValueRef a, ValueRef b) noexcept { return !(a == b); }

private:
    friend class Document;
    ValueRef(const Document* doc, std::uint32_t index) noexcept : doc_(doc), index_(index) {}
    const detail::DocumentNode& node() const noexcept;
    const Document* doc_;
    std::uint32_t index_;
};

// A parsed JSON document stored compactly: every value is a 16-byte node in
// one array and all strings and keys share one buffer, so it uses about half
// the memory of a Value tree and is faster to build. Read-only; use
// ValueRef::to_value() to get a mutable copy. Limits: 4 Gi values and 4 GiB of
// strings (larger input reports ErrorCode::DocumentTooLarge).
class Document {
public:
    Document() = default;
    // The top-level value. Only call on a successfully parsed document.
    ValueRef root() const noexcept { return ValueRef(this, 0); }
    // Bytes reserved for the document's storage, including spare capacity.
    WJSON_API std::size_t memory_usage() const noexcept;

private:
    friend class ValueRef;
    friend struct detail::DocumentAccess;
    std::vector<detail::DocumentNode> nodes_;
    std::vector<std::uint32_t> elements_;           // array elements (node indices)
    std::vector<detail::DocumentMember> members_;   // object members
    std::string strings_;                           // all strings and keys
};

inline const detail::DocumentNode& ValueRef::node() const noexcept { return doc_->nodes_[index_]; }

struct DocumentResult {
    std::optional<Document> document;
    ParseError error;

    explicit operator bool() const noexcept { return document.has_value(); }
};

// Same input, options and errors as parse() / parse_file() / parse_stream(),
// but the result is a compact read-only Document instead of a Value tree.
WJSON_API DocumentResult parse_document(std::string_view utf8_text, const ParseOptions& options = {});
WJSON_API DocumentResult parse_document_file(const std::filesystem::path& path, const ParseOptions& options = {});
WJSON_API DocumentResult parse_document_stream(std::istream& in, const ParseOptions& options = {});

// ---------------------------------------------------------------------------
// Encoding helpers
// ---------------------------------------------------------------------------

// Converts raw bytes to UTF-8 based on the BOM: strips a UTF-8 BOM and
// converts UTF-16 LE/BE. Bytes without a BOM are returned unchanged.
// Returns std::nullopt if the UTF-16 data is malformed.
WJSON_API std::optional<std::string> decode_text(std::string_view raw_bytes);

// Converts UTF-16 (e.g. Windows wchar_t strings) to UTF-8.
// Returns std::nullopt on unpaired surrogates.
WJSON_API std::optional<std::string> utf16_to_utf8(std::u16string_view text);

// Computes the 1-based line and column (in code points) of a byte offset.
WJSON_API std::pair<std::size_t, std::size_t> line_and_column(std::string_view text, std::size_t offset) noexcept;

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

struct WriteOptions {
    // 0 produces compact output; > 0 pretty-prints with that many spaces.
    int indent = 0;
    // Escape every non-ASCII character as \uXXXX.
    bool ascii_only = false;
};

WJSON_API std::string serialize(const Value& value, const WriteOptions& options = {});
WJSON_API std::string serialize(ValueRef value, const WriteOptions& options = {});

}  // namespace wjson
