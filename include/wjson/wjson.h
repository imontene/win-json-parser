/* wjson C API - a stable ABI for C and for language bindings (.NET,
 * PowerShell, Python...).
 *
 * Conventions:
 *  - All text is UTF-8. Lengths are in bytes; pass WJSON_NUL_TERMINATED to use
 *    strlen() on a NUL-terminated string.
 *  - Pointers to wjson_value are owned by their wjson_document and stay valid
 *    until wjson_document_free() is called.
 *  - Strings returned by wjson_serialize()/wjson_format() must be released with
 *    wjson_free().
 *  - Option structs carry their own size (set by the *_init functions) so new
 *    fields can be added without breaking binary compatibility.
 *  - No function throws or aborts; errors are reported through wjson_error.
 *
 * SPDX-License-Identifier: MIT */
#ifndef WJSON_H
#define WJSON_H

#include <stddef.h>
#include <stdint.h>

#include "wjson/export.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WJSON_NUL_TERMINATED SIZE_MAX

typedef enum wjson_type {
    WJSON_TYPE_NULL = 0,
    WJSON_TYPE_BOOL = 1,
    WJSON_TYPE_INTEGER = 2,
    WJSON_TYPE_DOUBLE = 3,
    WJSON_TYPE_STRING = 4,
    WJSON_TYPE_ARRAY = 5,
    WJSON_TYPE_OBJECT = 6
} wjson_type;

/* Values match wjson::ErrorCode in the C++ API. */
typedef enum wjson_error_code {
    WJSON_OK = 0,
    WJSON_ERROR_UNEXPECTED_END = 1,
    WJSON_ERROR_UNEXPECTED_CHARACTER = 2,
    WJSON_ERROR_TRAILING_CHARACTERS = 3,
    WJSON_ERROR_EXPECTED_KEY = 4,
    WJSON_ERROR_EXPECTED_COLON = 5,
    WJSON_ERROR_EXPECTED_COMMA_OR_BRACE = 6,
    WJSON_ERROR_EXPECTED_COMMA_OR_BRACKET = 7,
    WJSON_ERROR_INVALID_LITERAL = 8,
    WJSON_ERROR_INVALID_NUMBER = 9,
    WJSON_ERROR_NUMBER_OUT_OF_RANGE = 10,
    WJSON_ERROR_UNTERMINATED_STRING = 11,
    WJSON_ERROR_CONTROL_CHARACTER_IN_STRING = 12,
    WJSON_ERROR_INVALID_ESCAPE = 13,
    WJSON_ERROR_INVALID_UNICODE_ESCAPE = 14,
    WJSON_ERROR_INVALID_SURROGATE = 15,
    WJSON_ERROR_INVALID_UTF8 = 16,
    WJSON_ERROR_INVALID_ENCODING = 17,
    WJSON_ERROR_DEPTH_LIMIT_EXCEEDED = 18,
    WJSON_ERROR_IO = 19,
    WJSON_ERROR_INVALID_COMMENT = 20,
    WJSON_ERROR_UNTERMINATED_COMMENT = 21,
    WJSON_ERROR_STRING_TOO_LONG = 22,
    WJSON_ERROR_DOCUMENT_TOO_LARGE = 23,
    WJSON_ERROR_CANCELLED = 24,
    /* Errors specific to the C API */
    WJSON_ERROR_OUT_OF_MEMORY = 100,
    WJSON_ERROR_INVALID_ARGUMENT = 101
} wjson_error_code;

typedef struct wjson_error {
    wjson_error_code code;
    size_t offset; /* byte offset in the UTF-8 text */
    size_t line;   /* 1-based; 0 when not applicable */
    size_t column; /* 1-based, in Unicode code points */
} wjson_error;

typedef struct wjson_parse_options {
    size_t struct_size; /* set by wjson_parse_options_init() */
    size_t max_depth;
    size_t max_string_length;
    size_t max_document_size;
    int allow_comments;
    int allow_trailing_commas;
} wjson_parse_options;

typedef struct wjson_write_options {
    size_t struct_size; /* set by wjson_write_options_init() */
    int indent;         /* 0 = compact */
    int ascii_only;
} wjson_write_options;

typedef struct wjson_document wjson_document;
typedef struct wjson_value wjson_value;

/* --- Library information --- */
WJSON_API const char* wjson_version(void);
/* English description of an error code. Never returns NULL. */
WJSON_API const char* wjson_error_message(wjson_error_code code);

/* --- Options --- */
/* Fills in the defaults (strict RFC 8259, max_depth 512, no size limits). */
WJSON_API void wjson_parse_options_init(wjson_parse_options* options);
WJSON_API void wjson_write_options_init(wjson_write_options* options);

/* --- Parsing into a document --- */
/* options and error may be NULL. Returns NULL on failure. */
WJSON_API wjson_document* wjson_parse(const char* text, size_t length, const wjson_parse_options* options,
                                      wjson_error* error);
/* Reads a file (UTF-8 or UTF-16 with BOM). path is UTF-8, also on Windows. */
WJSON_API wjson_document* wjson_parse_file(const char* path, const wjson_parse_options* options, wjson_error* error);
WJSON_API void wjson_document_free(wjson_document* document);
WJSON_API const wjson_value* wjson_document_root(const wjson_document* document);

/* --- Validation without building a tree (returns 1 if valid, 0 otherwise) --- */
WJSON_API int wjson_validate(const char* text, size_t length, const wjson_parse_options* options, wjson_error* error);
/* Streams the file with constant memory, whatever its size. */
WJSON_API int wjson_validate_file(const char* path, const wjson_parse_options* options, wjson_error* error);

/* --- Reading values (all accept NULL and return a neutral result) --- */
WJSON_API wjson_type wjson_value_type(const wjson_value* value);
WJSON_API int wjson_value_get_bool(const wjson_value* value);        /* 0 if not a bool */
WJSON_API int64_t wjson_value_get_integer(const wjson_value* value); /* 0 if not an integer */
WJSON_API double wjson_value_get_double(const wjson_value* value);   /* integers are converted */
/* NUL-terminated UTF-8 (may also contain embedded NULs); NULL if not a string. */
WJSON_API const char* wjson_value_get_string(const wjson_value* value, size_t* length);
/* Number of elements of an array or members of an object; 0 otherwise. */
WJSON_API size_t wjson_value_size(const wjson_value* value);
WJSON_API const wjson_value* wjson_array_at(const wjson_value* array, size_t index);
WJSON_API const char* wjson_object_key_at(const wjson_value* object, size_t index, size_t* length);
WJSON_API const wjson_value* wjson_object_value_at(const wjson_value* object, size_t index);
/* Last member with that key (duplicates are kept), or NULL. */
WJSON_API const wjson_value* wjson_object_find(const wjson_value* object, const char* key, size_t key_length);
/* JSON Pointer (RFC 6901), e.g. "/items/0/name". */
WJSON_API const wjson_value* wjson_value_at_pointer(const wjson_value* value, const char* pointer, size_t length);

/* --- Serialization (free the result with wjson_free) --- */
WJSON_API char* wjson_serialize(const wjson_value* value, const wjson_write_options* options, size_t* length);
/* Parses and re-serializes in one call (format or minify). */
WJSON_API char* wjson_format(const char* text, size_t length, const wjson_parse_options* parse_options,
                             const wjson_write_options* write_options, size_t* out_length, wjson_error* error);
WJSON_API void wjson_free(void* pointer);

#ifdef __cplusplus
}
#endif

#endif /* WJSON_H */
