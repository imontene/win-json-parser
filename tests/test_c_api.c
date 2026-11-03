/* Tests for the C API. Written in C (not C++) on purpose, so it also checks
 * that wjson.h compiles as plain C. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wjson/wjson.h"

static int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

static void test_parse_and_read(void) {
    const char* text = "{\"name\": \"Iv\xC3\xA1n\", \"n\": [1, 2.5, true, null], \"k\": 1, \"k\": 2}";
    wjson_error error;
    wjson_document* doc = wjson_parse(text, WJSON_NUL_TERMINATED, NULL, &error);
    const wjson_value* root;
    const wjson_value* n;
    size_t length = 0;

    CHECK(doc != NULL);
    CHECK(error.code == WJSON_OK);
    if (!doc) return;
    root = wjson_document_root(doc);
    CHECK(wjson_value_type(root) == WJSON_TYPE_OBJECT);
    CHECK(wjson_value_size(root) == 4);

    CHECK(strcmp(wjson_value_get_string(wjson_object_find(root, "name", WJSON_NUL_TERMINATED), &length),
                 "Iv\xC3\xA1n") == 0);
    CHECK(length == 5);
    CHECK(strcmp(wjson_object_key_at(root, 1, &length), "n") == 0);
    CHECK(wjson_value_get_integer(wjson_object_find(root, "k", 1)) == 2); /* last duplicate wins */

    n = wjson_object_value_at(root, 1);
    CHECK(wjson_value_type(n) == WJSON_TYPE_ARRAY);
    CHECK(wjson_value_size(n) == 4);
    CHECK(wjson_value_get_integer(wjson_array_at(n, 0)) == 1);
    CHECK(wjson_value_get_double(wjson_array_at(n, 0)) == 1.0);
    CHECK(wjson_value_get_double(wjson_array_at(n, 1)) == 2.5);
    CHECK(wjson_value_get_bool(wjson_array_at(n, 2)) == 1);
    CHECK(wjson_value_type(wjson_array_at(n, 3)) == WJSON_TYPE_NULL);
    CHECK(wjson_array_at(n, 4) == NULL);

    CHECK(wjson_value_get_double(wjson_value_at_pointer(root, "/n/1", WJSON_NUL_TERMINATED)) == 2.5);
    CHECK(wjson_value_at_pointer(root, "/missing", WJSON_NUL_TERMINATED) == NULL);

    /* Wrong types and NULL return neutral values instead of crashing. */
    CHECK(wjson_value_get_string(n, &length) == NULL && length == 0);
    CHECK(wjson_value_get_integer(NULL) == 0);
    CHECK(wjson_value_size(NULL) == 0);
    CHECK(wjson_object_find(n, "x", 1) == NULL);

    wjson_document_free(doc);
    wjson_document_free(NULL);
}

static void test_errors(void) {
    wjson_error error;
    wjson_parse_options options;

    CHECK(wjson_parse("{\n  \"a\": [1, 2,]\n}", WJSON_NUL_TERMINATED, NULL, &error) == NULL);
    CHECK(error.code == WJSON_ERROR_UNEXPECTED_CHARACTER);
    CHECK(error.line == 2);
    CHECK(error.column == 14);
    CHECK(strcmp(wjson_error_message(error.code), "unexpected character") == 0);

    CHECK(wjson_parse(NULL, 3, NULL, &error) == NULL);
    CHECK(error.code == WJSON_ERROR_INVALID_ARGUMENT);
    {
        wjson_document* doc = wjson_parse("[1]", 3, NULL, NULL); /* options and error may be NULL */
        CHECK(doc != NULL);
        wjson_document_free(doc);
    }

    wjson_parse_options_init(&options);
    CHECK(options.max_depth == 512);
    options.max_depth = 2;
    CHECK(wjson_validate("[[1]]", WJSON_NUL_TERMINATED, &options, &error) == 1);
    CHECK(wjson_validate("[[[1]]]", WJSON_NUL_TERMINATED, &options, &error) == 0);
    CHECK(error.code == WJSON_ERROR_DEPTH_LIMIT_EXCEEDED);

    /* An older caller that only knows the first fields still works. */
    options.struct_size = sizeof(size_t) * 2; /* struct_size + max_depth */
    options.allow_comments = 1;               /* ignored: outside the declared size */
    CHECK(wjson_validate("[1] // c", WJSON_NUL_TERMINATED, &options, &error) == 0);
}

static void test_jsonc_and_format(void) {
    wjson_parse_options parse_options;
    wjson_write_options write_options;
    wjson_error error;
    size_t length = 0;
    char* out;

    wjson_parse_options_init(&parse_options);
    parse_options.allow_comments = 1;
    parse_options.allow_trailing_commas = 1;
    wjson_write_options_init(&write_options);

    out = wjson_format("{\"a\": [1, 2,], /* c */}", WJSON_NUL_TERMINATED, &parse_options, &write_options, &length,
                       &error);
    CHECK(out != NULL);
    if (out) {
        CHECK(strcmp(out, "{\"a\":[1,2]}") == 0);
        CHECK(length == strlen(out));
        wjson_free(out);
    }

    write_options.indent = 2;
    write_options.ascii_only = 1;
    out = wjson_format("[\"\xC3\xA9\"]", WJSON_NUL_TERMINATED, NULL, &write_options, &length, &error);
    CHECK(out != NULL);
    if (out) {
        CHECK(strcmp(out, "[\n  \"\\u00e9\"\n]") == 0);
        wjson_free(out);
    }

    CHECK(wjson_format("[1,", WJSON_NUL_TERMINATED, NULL, NULL, &length, &error) == NULL);
    CHECK(error.code == WJSON_ERROR_UNEXPECTED_END);
    CHECK(length == 0);
}

static void test_files(const char* valid_path, const char* invalid_path) {
    wjson_error error;
    wjson_document* doc = wjson_parse_file(valid_path, NULL, &error);
    CHECK(doc != NULL);
    if (doc) {
        CHECK(wjson_value_type(wjson_document_root(doc)) == WJSON_TYPE_OBJECT);
        wjson_document_free(doc);
    }
    CHECK(wjson_validate_file(valid_path, NULL, &error) == 1);
    CHECK(wjson_validate_file(invalid_path, NULL, &error) == 0);
    CHECK(error.code == WJSON_ERROR_UNEXPECTED_CHARACTER && error.line == 3);
    CHECK(wjson_validate_file("does/not/exist.json", NULL, &error) == 0);
    CHECK(error.code == WJSON_ERROR_IO);
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: wjson_c_tests <valid.json> <invalid.json>\n");
        return 2;
    }
    CHECK(strlen(wjson_version()) > 0);
    test_parse_and_read();
    test_errors();
    test_jsonc_and_format();
    test_files(argv[1], argv[2]);
    if (failures == 0) printf("C API: all checks passed (wjson %s)\n", wjson_version());
    return failures == 0 ? 0 : 1;
}
