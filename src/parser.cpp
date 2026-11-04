// SPDX-License-Identifier: MIT
#include <fstream>
#include <istream>
#include <iterator>
#include <utility>
#include <vector>

#include "reader.hpp"
#include "sources.hpp"
#include "wjson/wjson.hpp"

namespace wjson {

namespace {

// Builds a Value tree from parse events.
//
// The children of each open container are collected in a scratch buffer for
// its depth, reused across containers. When the container is complete, small
// ones (the vast majority) are moved into a vector allocated once at exactly
// the right size, instead of growing by repeated reallocation; large ones take
// over the scratch buffer itself, so their elements are never copied twice.
class DomBuilder {
public:
    bool null_value() { return add(Value(nullptr)); }
    bool boolean(bool b) { return add(Value(b)); }
    bool integer(std::int64_t i) { return add(Value(i)); }
    bool number(double d) { return add(Value(d)); }
    bool string(std::string& s) { return add(Value(std::move(s))); }
    bool key(std::string& k) {
        frames_.back().key = std::move(k);
        return true;
    }
    bool start_object() { return open(true); }
    bool start_array() { return open(false); }

    bool end_object() {
        Object object = finish(objects_[frames_.size() - 1]);
        frames_.pop_back();
        return add(Value(std::move(object)));
    }

    bool end_array() {
        Array array = finish(arrays_[frames_.size() - 1]);
        frames_.pop_back();
        return add(Value(std::move(array)));
    }

    Value take() { return std::move(root_); }

private:
    struct Frame {
        bool is_object;
        std::string key;  // pending key of an object member
    };

    // Containers above this size take over the scratch buffer instead of
    // being copied out of it.
    static constexpr std::size_t steal_threshold = 256;

    template <typename T>
    static std::vector<T> finish(std::vector<T>& scratch) {
        if (scratch.size() > steal_threshold) {
            std::vector<T> result = std::move(scratch);
            scratch = std::vector<T>();  // moved-from vectors are valid but unspecified
            return result;
        }
        std::vector<T> result(std::make_move_iterator(scratch.begin()), std::make_move_iterator(scratch.end()));
        scratch.clear();
        return result;
    }

    bool open(bool is_object) {
        frames_.push_back(Frame{is_object, {}});
        const std::size_t depth = frames_.size();
        if (arrays_.size() < depth) {
            arrays_.resize(depth);
            objects_.resize(depth);
        }
        return true;
    }

    bool add(Value v) {
        if (frames_.empty()) {
            root_ = std::move(v);
            return true;
        }
        Frame& frame = frames_.back();
        const std::size_t level = frames_.size() - 1;
        if (frame.is_object) {
            objects_[level].emplace_back(std::move(frame.key), std::move(v));
        } else {
            arrays_[level].push_back(std::move(v));
        }
        return true;
    }

    std::vector<Frame> frames_;                 // open containers, innermost last
    std::vector<std::vector<Value>> arrays_;    // scratch items per depth
    std::vector<std::vector<Member>> objects_;  // scratch members per depth
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
