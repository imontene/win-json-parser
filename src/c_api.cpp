// C API (include/wjson/wjson.h) implemented over the C++ API. No exception
// may cross this boundary.
// SPDX-License-Identifier: MIT
#include "wjson/wjson.h"

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <cstring>
#include <new>
#include <string>
#include <string_view>

#include "wjson/wjson.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifndef WJSON_VERSION
#define WJSON_VERSION "0.0.0"
#endif

struct wjson_document {
    wjson::Value root;
};

namespace {

using wjson::ErrorCode;

// The C enum mirrors wjson::ErrorCode; keep them in sync.
#define WJSON_SAME(c, cpp) static_assert(static_cast<int>(c) == static_cast<int>(ErrorCode::cpp), #cpp)
WJSON_SAME(WJSON_OK, None);
WJSON_SAME(WJSON_ERROR_UNEXPECTED_END, UnexpectedEnd);
WJSON_SAME(WJSON_ERROR_UNEXPECTED_CHARACTER, UnexpectedCharacter);
WJSON_SAME(WJSON_ERROR_TRAILING_CHARACTERS, TrailingCharacters);
WJSON_SAME(WJSON_ERROR_EXPECTED_KEY, ExpectedKey);
WJSON_SAME(WJSON_ERROR_EXPECTED_COLON, ExpectedColon);
WJSON_SAME(WJSON_ERROR_EXPECTED_COMMA_OR_BRACE, ExpectedCommaOrBrace);
WJSON_SAME(WJSON_ERROR_EXPECTED_COMMA_OR_BRACKET, ExpectedCommaOrBracket);
WJSON_SAME(WJSON_ERROR_INVALID_LITERAL, InvalidLiteral);
WJSON_SAME(WJSON_ERROR_INVALID_NUMBER, InvalidNumber);
WJSON_SAME(WJSON_ERROR_NUMBER_OUT_OF_RANGE, NumberOutOfRange);
WJSON_SAME(WJSON_ERROR_UNTERMINATED_STRING, UnterminatedString);
WJSON_SAME(WJSON_ERROR_CONTROL_CHARACTER_IN_STRING, ControlCharacterInString);
WJSON_SAME(WJSON_ERROR_INVALID_ESCAPE, InvalidEscape);
WJSON_SAME(WJSON_ERROR_INVALID_UNICODE_ESCAPE, InvalidUnicodeEscape);
WJSON_SAME(WJSON_ERROR_INVALID_SURROGATE, InvalidSurrogate);
WJSON_SAME(WJSON_ERROR_INVALID_UTF8, InvalidUtf8);
WJSON_SAME(WJSON_ERROR_INVALID_ENCODING, InvalidEncoding);
WJSON_SAME(WJSON_ERROR_DEPTH_LIMIT_EXCEEDED, DepthLimitExceeded);
WJSON_SAME(WJSON_ERROR_IO, IoError);
WJSON_SAME(WJSON_ERROR_INVALID_COMMENT, InvalidComment);
WJSON_SAME(WJSON_ERROR_UNTERMINATED_COMMENT, UnterminatedComment);
WJSON_SAME(WJSON_ERROR_STRING_TOO_LONG, StringTooLong);
WJSON_SAME(WJSON_ERROR_DOCUMENT_TOO_LARGE, DocumentTooLarge);
WJSON_SAME(WJSON_ERROR_CANCELLED, Cancelled);
#undef WJSON_SAME

static_assert(WJSON_TYPE_OBJECT == static_cast<int>(wjson::Type::Object), "wjson_type out of sync");

const wjson::Value* as_cpp(const wjson_value* v) noexcept { return reinterpret_cast<const wjson::Value*>(v); }
const wjson_value* as_c(const wjson::Value* v) noexcept { return reinterpret_cast<const wjson_value*>(v); }

void set_error(wjson_error* out, wjson_error_code code) noexcept {
    if (!out) return;
    out->code = code;
    out->offset = out->line = out->column = 0;
}

void set_error(wjson_error* out, const wjson::ParseError& e) noexcept {
    if (!out) return;
    out->code = static_cast<wjson_error_code>(e.code);
    out->offset = e.offset;
    out->line = e.line;
    out->column = e.column;
}

// Resolves WJSON_NUL_TERMINATED and rejects (NULL, n > 0).
bool get_text(const char* text, size_t length, std::string_view& out) noexcept {
    if (length == WJSON_NUL_TERMINATED) {
        if (!text) return false;
        out = std::string_view(text);
        return true;
    }
    if (!text && length > 0) return false;
    out = text ? std::string_view(text, length) : std::string_view{};
    return true;
}

// Copies the fields the caller's (possibly older, smaller) struct contains.
wjson::ParseOptions to_cpp(const wjson_parse_options* o) noexcept {
    wjson::ParseOptions options;
    if (!o) return options;
    const size_t size = o->struct_size;
    auto has = [size](size_t offset, size_t field_size) { return offset + field_size <= size; };
    if (has(offsetof(wjson_parse_options, max_depth), sizeof o->max_depth)) options.max_depth = o->max_depth;
    if (has(offsetof(wjson_parse_options, max_string_length), sizeof o->max_string_length))
        options.max_string_length = o->max_string_length;
    if (has(offsetof(wjson_parse_options, max_document_size), sizeof o->max_document_size))
        options.max_document_size = o->max_document_size;
    if (has(offsetof(wjson_parse_options, allow_comments), sizeof o->allow_comments))
        options.allow_comments = o->allow_comments != 0;
    if (has(offsetof(wjson_parse_options, allow_trailing_commas), sizeof o->allow_trailing_commas))
        options.allow_trailing_commas = o->allow_trailing_commas != 0;
    return options;
}

wjson::WriteOptions to_cpp(const wjson_write_options* o) noexcept {
    wjson::WriteOptions options;
    if (!o) return options;
    const size_t size = o->struct_size;
    if (offsetof(wjson_write_options, indent) + sizeof o->indent <= size)
        options.indent = o->indent < 0 ? 0 : (o->indent > 32 ? 32 : o->indent);
    if (offsetof(wjson_write_options, ascii_only) + sizeof o->ascii_only <= size)
        options.ascii_only = o->ascii_only != 0;
    return options;
}

bool to_path(const char* utf8, std::filesystem::path& out) {
    if (!utf8) return false;
#ifdef _WIN32
    const int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, nullptr, 0);
    if (len <= 0) return false;
    std::wstring wide(static_cast<std::size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, wide.data(), len);
    out = std::filesystem::path(wide);
#else
    out = std::filesystem::path(utf8);
#endif
    return true;
}

// Returns a malloc'ed NUL-terminated copy, so wjson_free() can release it.
char* to_c_string(const std::string& s, size_t* length) noexcept {
    char* out = static_cast<char*>(std::malloc(s.size() + 1));
    if (!out) return nullptr;
    std::memcpy(out, s.data(), s.size());
    out[s.size()] = '\0';
    if (length) *length = s.size();
    return out;
}

wjson_document* make_document(wjson::ParseResult&& result, wjson_error* error) {
    set_error(error, result.error);
    if (!result) return nullptr;
    return new wjson_document{std::move(*result.value)};
}

}  // namespace

extern "C" {

const char* wjson_version(void) { return WJSON_VERSION; }

const char* wjson_error_message(wjson_error_code code) {
    switch (code) {
        case WJSON_ERROR_OUT_OF_MEMORY: return "out of memory";
        case WJSON_ERROR_INVALID_ARGUMENT: return "invalid argument";
        default: return wjson::message(static_cast<ErrorCode>(code));
    }
}

void wjson_parse_options_init(wjson_parse_options* options) {
    if (!options) return;
    const wjson::ParseOptions defaults;
    options->struct_size = sizeof(wjson_parse_options);
    options->max_depth = defaults.max_depth;
    options->max_string_length = defaults.max_string_length;
    options->max_document_size = defaults.max_document_size;
    options->allow_comments = defaults.allow_comments ? 1 : 0;
    options->allow_trailing_commas = defaults.allow_trailing_commas ? 1 : 0;
}

void wjson_write_options_init(wjson_write_options* options) {
    if (!options) return;
    options->struct_size = sizeof(wjson_write_options);
    options->indent = 0;
    options->ascii_only = 0;
}

wjson_document* wjson_parse(const char* text, size_t length, const wjson_parse_options* options, wjson_error* error) {
    std::string_view view;
    if (!get_text(text, length, view)) {
        set_error(error, WJSON_ERROR_INVALID_ARGUMENT);
        return nullptr;
    }
    try {
        return make_document(wjson::parse(view, to_cpp(options)), error);
    } catch (...) {
        set_error(error, WJSON_ERROR_OUT_OF_MEMORY);
        return nullptr;
    }
}

wjson_document* wjson_parse_file(const char* path, const wjson_parse_options* options, wjson_error* error) {
    try {
        std::filesystem::path p;
        if (!to_path(path, p)) {
            set_error(error, WJSON_ERROR_INVALID_ARGUMENT);
            return nullptr;
        }
        return make_document(wjson::parse_file(p, to_cpp(options)), error);
    } catch (...) {
        set_error(error, WJSON_ERROR_OUT_OF_MEMORY);
        return nullptr;
    }
}

void wjson_document_free(wjson_document* document) { delete document; }

const wjson_value* wjson_document_root(const wjson_document* document) {
    return document ? as_c(&document->root) : nullptr;
}

int wjson_validate(const char* text, size_t length, const wjson_parse_options* options, wjson_error* error) {
    std::string_view view;
    if (!get_text(text, length, view)) {
        set_error(error, WJSON_ERROR_INVALID_ARGUMENT);
        return 0;
    }
    try {
        wjson::Handler validator;
        const auto e = wjson::parse(view, validator, to_cpp(options));
        set_error(error, e);
        return e.ok() ? 1 : 0;
    } catch (...) {
        set_error(error, WJSON_ERROR_OUT_OF_MEMORY);
        return 0;
    }
}

int wjson_validate_file(const char* path, const wjson_parse_options* options, wjson_error* error) {
    try {
        std::filesystem::path p;
        if (!to_path(path, p)) {
            set_error(error, WJSON_ERROR_INVALID_ARGUMENT);
            return 0;
        }
        wjson::Handler validator;
        const auto e = wjson::parse_file(p, validator, to_cpp(options));
        set_error(error, e);
        return e.ok() ? 1 : 0;
    } catch (...) {
        set_error(error, WJSON_ERROR_OUT_OF_MEMORY);
        return 0;
    }
}

wjson_type wjson_value_type(const wjson_value* value) {
    return value ? static_cast<wjson_type>(as_cpp(value)->type()) : WJSON_TYPE_NULL;
}

int wjson_value_get_bool(const wjson_value* value) {
    const auto* v = as_cpp(value);
    return v && v->is_bool() && v->as_bool() ? 1 : 0;
}

int64_t wjson_value_get_integer(const wjson_value* value) {
    const auto* v = as_cpp(value);
    return v && v->is_integer() ? v->as_integer() : 0;
}

double wjson_value_get_double(const wjson_value* value) {
    const auto* v = as_cpp(value);
    return v && v->is_number() ? v->as_double() : 0.0;
}

const char* wjson_value_get_string(const wjson_value* value, size_t* length) {
    const auto* v = as_cpp(value);
    if (!v || !v->is_string()) {
        if (length) *length = 0;
        return nullptr;
    }
    if (length) *length = v->as_string().size();
    return v->as_string().c_str();
}

size_t wjson_value_size(const wjson_value* value) {
    const auto* v = as_cpp(value);
    if (!v) return 0;
    if (v->is_array()) return v->as_array().size();
    if (v->is_object()) return v->as_object().size();
    return 0;
}

const wjson_value* wjson_array_at(const wjson_value* array, size_t index) {
    const auto* v = as_cpp(array);
    if (!v || !v->is_array() || index >= v->as_array().size()) return nullptr;
    return as_c(&v->as_array()[index]);
}

const char* wjson_object_key_at(const wjson_value* object, size_t index, size_t* length) {
    const auto* v = as_cpp(object);
    if (!v || !v->is_object() || index >= v->as_object().size()) {
        if (length) *length = 0;
        return nullptr;
    }
    const std::string& key = v->as_object()[index].first;
    if (length) *length = key.size();
    return key.c_str();
}

const wjson_value* wjson_object_value_at(const wjson_value* object, size_t index) {
    const auto* v = as_cpp(object);
    if (!v || !v->is_object() || index >= v->as_object().size()) return nullptr;
    return as_c(&v->as_object()[index].second);
}

const wjson_value* wjson_object_find(const wjson_value* object, const char* key, size_t key_length) {
    std::string_view k;
    if (!object || !get_text(key, key_length, k)) return nullptr;
    return as_c(as_cpp(object)->find(k));
}

const wjson_value* wjson_value_at_pointer(const wjson_value* value, const char* pointer, size_t length) {
    std::string_view p;
    if (!value || !get_text(pointer, length, p)) return nullptr;
    return as_c(as_cpp(value)->at_pointer(p));
}

char* wjson_serialize(const wjson_value* value, const wjson_write_options* options, size_t* length) {
    if (length) *length = 0;
    if (!value) return nullptr;
    try {
        return to_c_string(wjson::serialize(*as_cpp(value), to_cpp(options)), length);
    } catch (...) {
        return nullptr;
    }
}

char* wjson_format(const char* text, size_t length, const wjson_parse_options* parse_options,
                   const wjson_write_options* write_options, size_t* out_length, wjson_error* error) {
    if (out_length) *out_length = 0;
    std::string_view view;
    if (!get_text(text, length, view)) {
        set_error(error, WJSON_ERROR_INVALID_ARGUMENT);
        return nullptr;
    }
    try {
        const auto result = wjson::parse(view, to_cpp(parse_options));
        set_error(error, result.error);
        if (!result) return nullptr;
        char* out = to_c_string(wjson::serialize(*result.value, to_cpp(write_options)), out_length);
        if (!out) set_error(error, WJSON_ERROR_OUT_OF_MEMORY);
        return out;
    } catch (...) {
        set_error(error, WJSON_ERROR_OUT_OF_MEMORY);
        return nullptr;
    }
}

void wjson_free(void* pointer) { std::free(pointer); }

}  // extern "C"
