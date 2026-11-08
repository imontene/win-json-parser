// Fuzz target for wjson (libFuzzer API).
//
// Besides catching crashes, undefined behaviour and leaks (with sanitizers),
// it checks properties that must hold for every input:
//   1. DOM and SAX parsing report the same result and error position.
//   2. Parsing from memory and from a stream (with a small, varying buffer)
//      agree.
//   3. Any accepted document survives serialize -> parse unchanged, in both
//      compact and pretty form.
//   4. The C API agrees with the C++ API.
//   5. The compact Document agrees with the Value tree.
//
// The first input byte selects the parse options, so the fuzzer also explores
// JSONC, depth limits and stream buffer sizes.
// SPDX-License-Identifier: MIT
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <string_view>

#include "wjson/wjson.h"
#include "wjson/wjson.hpp"

namespace {

[[noreturn]] void violated(const char* property) {
    std::fprintf(stderr, "wjson fuzz: property violated: %s\n", property);
    std::abort();
}

bool starts_with_bom(std::string_view s) {
    return s.substr(0, 3) == "\xEF\xBB\xBF" || s.substr(0, 2) == "\xFF\xFE" || s.substr(0, 2) == "\xFE\xFF";
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size == 0) return 0;
    const unsigned flags = data[0];
    const std::string_view text(reinterpret_cast<const char*>(data + 1), size - 1);

    wjson::ParseOptions options;
    options.allow_comments = (flags & 1u) != 0;
    options.allow_trailing_commas = (flags & 2u) != 0;
    options.max_depth = (flags & 4u) != 0 ? 16 : 512;
    options.stream_buffer_size = 4 + (flags >> 3);  // 4..35 bytes: tokens get split

    // 1. DOM vs SAX
    const wjson::ParseResult dom = wjson::parse(text, options);
    wjson::Handler validator;
    const wjson::ParseError sax = wjson::parse(text, validator, options);
    if (dom.error.code != sax.code || dom.error.offset != sax.offset || dom.error.line != sax.line ||
        dom.error.column != sax.column)
        violated("DOM and SAX disagree");
    if (static_cast<bool>(dom) != sax.ok()) violated("DOM result and SAX status disagree");

    // 2. Memory vs stream (the stream decodes BOMs, so skip inputs with one)
    if (!starts_with_bom(text)) {
        std::istringstream in{std::string(text)};
        wjson::Handler stream_validator;
        const wjson::ParseError stream = wjson::parse_stream(in, stream_validator, options);
        if (stream.code != sax.code || stream.offset != sax.offset) violated("memory and stream disagree");
    }

    // 3. Round trip
    if (dom) {
        const wjson::Value& value = *dom.value;
        for (int indent : {0, 2}) {
            wjson::WriteOptions write;
            write.indent = indent;
            write.ascii_only = (flags & 0x80u) != 0;
            const std::string out = wjson::serialize(value, write);
            const wjson::ParseResult again = wjson::parse(out);  // output is always strict JSON
            if (!again) violated("serialized output does not parse");
            if (!(*again.value == value)) violated("round trip changed the value");
        }
        // Exercise JSON Pointer with arbitrary input; must not crash.
        (void)value.at_pointer(text);
    }

    // 5. Compact Document vs Value tree
    const wjson::DocumentResult document = wjson::parse_document(text, options);
    if (document.error.code != dom.error.code || document.error.offset != dom.error.offset)
        violated("Document and Value disagree");
    if (dom && document) {
        if (wjson::serialize(document.document->root()) != wjson::serialize(*dom.value))
            violated("Document serializes differently from Value");
        (void)document.document->root().at_pointer(text);
    }

    // 4. C API
    wjson_parse_options c_options;
    wjson_parse_options_init(&c_options);
    c_options.allow_comments = options.allow_comments ? 1 : 0;
    c_options.allow_trailing_commas = options.allow_trailing_commas ? 1 : 0;
    c_options.max_depth = options.max_depth;
    wjson_error c_error;
    const int c_valid = wjson_validate(text.data(), text.size(), &c_options, &c_error);
    if (c_valid != (dom ? 1 : 0) || static_cast<int>(c_error.code) != static_cast<int>(dom.error.code))
        violated("C API disagrees");

    return 0;
}
