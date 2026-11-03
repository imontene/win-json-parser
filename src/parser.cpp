// SPDX-License-Identifier: MIT
#include <fstream>
#include <istream>
#include <utility>
#include <vector>

#include "reader.hpp"
#include "sources.hpp"
#include "wjson/wjson.hpp"

namespace wjson {

namespace {

// Builds a Value tree from parse events.
class DomBuilder {
public:
    bool null_value() { return add(Value(nullptr)); }
    bool boolean(bool b) { return add(Value(b)); }
    bool integer(std::int64_t i) { return add(Value(i)); }
    bool number(double d) { return add(Value(d)); }
    bool string(std::string& s) { return add(Value(std::move(s))); }
    bool key(std::string& k) {
        keys_.push_back(std::move(k));
        return true;
    }
    bool start_object() {
        stack_.emplace_back(Object{});
        return true;
    }
    bool start_array() {
        stack_.emplace_back(Array{});
        return true;
    }
    bool end_object() { return end_container(); }
    bool end_array() { return end_container(); }

    Value take() { return std::move(root_); }

private:
    bool end_container() {
        Value done = std::move(stack_.back());
        stack_.pop_back();
        return add(std::move(done));
    }

    bool add(Value v) {
        if (stack_.empty()) {
            root_ = std::move(v);
        } else if (Value& parent = stack_.back(); parent.is_array()) {
            parent.as_array().push_back(std::move(v));
        } else {
            parent.as_object().emplace_back(std::move(keys_.back()), std::move(v));
            keys_.pop_back();
        }
        return true;
    }

    std::vector<Value> stack_;  // containers being filled
    std::vector<std::string> keys_;
    Value root_;
};

// Forwards parse events to a user-supplied Handler.
class HandlerSink {
public:
    explicit HandlerSink(Handler& h) noexcept : h_(h) {}
    bool null_value() { return h_.on_null(); }
    bool boolean(bool b) { return h_.on_bool(b); }
    bool integer(std::int64_t i) { return h_.on_integer(i); }
    bool number(double d) { return h_.on_double(d); }
    bool string(std::string& s) { return h_.on_string(s); }
    bool key(std::string& k) { return h_.on_key(k); }
    bool start_object() { return h_.on_start_object(); }
    bool end_object() { return h_.on_end_object(); }
    bool start_array() { return h_.on_start_array(); }
    bool end_array() { return h_.on_end_array(); }

private:
    Handler& h_;
};

template <typename Source>
ParseResult build_dom(Source& source, const ParseOptions& options) {
    DomBuilder builder;
    ParseResult result;
    result.error = detail::read(source, builder, options);
    if (result.error.ok()) result.value = builder.take();
    return result;
}

ParseError io_error() {
    ParseError e;
    e.code = ErrorCode::IoError;
    return e;
}

}  // namespace

ParseResult parse(std::string_view utf8_text, const ParseOptions& options) {
    detail::MemorySource source(utf8_text, options.max_document_size);
    return build_dom(source, options);
}

ParseResult parse_stream(std::istream& in, const ParseOptions& options) {
    detail::StreamSource source(in, options);
    return build_dom(source, options);
}

ParseError parse(std::string_view utf8_text, Handler& handler, const ParseOptions& options) {
    detail::MemorySource source(utf8_text, options.max_document_size);
    HandlerSink sink(handler);
    return detail::read(source, sink, options);
}

ParseError parse_stream(std::istream& in, Handler& handler, const ParseOptions& options) {
    detail::StreamSource source(in, options);
    HandlerSink sink(handler);
    return detail::read(source, sink, options);
}

ParseError parse_file(const std::filesystem::path& path, Handler& handler, const ParseOptions& options) {
    // std::ifstream takes a wide path on Windows, so Unicode file names work.
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return io_error();
    return parse_stream(in, handler, options);
}

}  // namespace wjson
