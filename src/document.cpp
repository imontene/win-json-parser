// Compact read-only documents (wjson::Document / wjson::ValueRef).
//
// Layout: every value is a 16-byte DocumentNode in document order, so the
// root is node 0. A container's node stores how many children it has and
// where its block of links starts: arrays in `elements_` (node indices),
// objects in `members_` (key position in the string buffer + value node).
// Each container's block is contiguous, so element i is found in O(1).
// SPDX-License-Identifier: MIT
#include <cstring>
#include <fstream>
#include <istream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <variant>
#include <vector>

#include "pointer.hpp"
#include "reader.hpp"
#include "sources.hpp"
#include "wjson/wjson.hpp"

namespace wjson {

namespace detail {

struct DocumentAccess {
    static std::vector<DocumentNode>& nodes(Document& d) noexcept { return d.nodes_; }
    static std::vector<std::uint32_t>& elements(Document& d) noexcept { return d.elements_; }
    static std::vector<DocumentMember>& members(Document& d) noexcept { return d.members_; }
    static std::string& strings(Document& d) noexcept { return d.strings_; }
};

}  // namespace detail

namespace {

using detail::DocumentMember;
using detail::DocumentNode;

constexpr std::uint64_t max_u32 = std::numeric_limits<std::uint32_t>::max();

// Parser sink that fills a Document. Children of each open container are
// collected in a scratch buffer for its depth and copied into the shared link
// arrays as one contiguous block when the container ends.
class DocumentBuilder {
public:
    explicit DocumentBuilder(Document& doc)
        : nodes_(detail::DocumentAccess::nodes(doc)),
          elements_(detail::DocumentAccess::elements(doc)),
          members_(detail::DocumentAccess::members(doc)),
          strings_(detail::DocumentAccess::strings(doc)) {}

    bool null_value() { return add(Type::Null, 0, 0); }
    bool boolean(bool b) { return add(Type::Boolean, 0, b ? 1 : 0); }
    bool integer(std::int64_t i) { return add(Type::Integer, 0, bits(i)); }
    bool number(double d) { return add(Type::Double, 0, bits(d)); }

    bool string(std::string& s) {
        if (s.size() > max_u32) return too_large();
        const std::uint64_t offset = strings_.size();
        strings_.append(s);
        return add(Type::String, static_cast<std::uint32_t>(s.size()), offset);
    }

    bool key(std::string& k) {
        // Keys are addressed with 32 bits, so they must start within 4 GiB.
        if (strings_.size() + k.size() > max_u32) return too_large();
        Frame& frame = frames_.back();
        frame.key_offset = static_cast<std::uint32_t>(strings_.size());
        frame.key_length = static_cast<std::uint32_t>(k.size());
        strings_.append(k);
        return true;
    }

    bool start_object() { return open(Type::Object); }
    bool start_array() { return open(Type::Array); }

    bool end_object() {
        auto& scratch = member_scratch_[frames_.size() - 1];
        if (scratch.size() > max_u32) return too_large();
        DocumentNode& node = nodes_[frames_.back().node];
        node.count = static_cast<std::uint32_t>(scratch.size());
        node.payload = members_.size();
        members_.insert(members_.end(), scratch.begin(), scratch.end());
        scratch.clear();
        frames_.pop_back();
        return true;
    }

    bool end_array() {
        auto& scratch = element_scratch_[frames_.size() - 1];
        if (scratch.size() > max_u32) return too_large();
        DocumentNode& node = nodes_[frames_.back().node];
        node.count = static_cast<std::uint32_t>(scratch.size());
        node.payload = elements_.size();
        elements_.insert(elements_.end(), scratch.begin(), scratch.end());
        scratch.clear();
        frames_.pop_back();
        return true;
    }

    bool exceeded_limits() const noexcept { return too_large_; }

private:
    struct Frame {
        std::uint32_t node;
        bool is_object;
        std::uint32_t key_offset;
        std::uint32_t key_length;
    };

    template <typename T>
    static std::uint64_t bits(T value) noexcept {
        std::uint64_t b;
        std::memcpy(&b, &value, sizeof b);
        return b;
    }

    bool too_large() noexcept {
        too_large_ = true;
        return false;  // stops the parser; reported as DocumentTooLarge
    }

    // Appends a node and links it to the open container, if any.
    bool add(Type type, std::uint32_t count, std::uint64_t payload) {
        if (nodes_.size() >= max_u32) return too_large();
        const auto index = static_cast<std::uint32_t>(nodes_.size());
        nodes_.push_back(DocumentNode{static_cast<std::uint8_t>(type), {0, 0, 0}, count, payload});
        if (!frames_.empty()) {
            const Frame& parent = frames_.back();
            const std::size_t level = frames_.size() - 1;
            if (parent.is_object) {
                member_scratch_[level].push_back(DocumentMember{parent.key_offset, parent.key_length, index});
            } else {
                element_scratch_[level].push_back(index);
            }
        }
        return true;
    }

    bool open(Type type) {
        const auto index = static_cast<std::uint32_t>(nodes_.size());
        if (!add(type, 0, 0)) return false;
        frames_.push_back(Frame{index, type == Type::Object, 0, 0});
        if (element_scratch_.size() < frames_.size()) {
            element_scratch_.resize(frames_.size());
            member_scratch_.resize(frames_.size());
        }
        return true;
    }

    std::vector<DocumentNode>& nodes_;
    std::vector<std::uint32_t>& elements_;
    std::vector<DocumentMember>& members_;
    std::string& strings_;

    std::vector<Frame> frames_;
    std::vector<std::vector<std::uint32_t>> element_scratch_;
    std::vector<std::vector<DocumentMember>> member_scratch_;
    bool too_large_ = false;
};

template <typename Source>
DocumentResult build_document(Source& source, const ParseOptions& options) {
    DocumentResult result;
    Document& doc = result.document.emplace();
    DocumentBuilder builder(doc);
    result.error = detail::read(source, builder, options);
    if (builder.exceeded_limits()) result.error.code = ErrorCode::DocumentTooLarge;
    // No shrink_to_fit: copying the arrays would raise peak memory and cost
    // time, while their unused capacity is never touched and so mostly does
    // not occupy physical memory.
    if (!result.error.ok()) result.document.reset();
    return result;
}

DocumentResult failure(ErrorCode code) {
    DocumentResult result;
    result.error.code = code;
    return result;
}

[[noreturn]] void wrong_type() { throw std::bad_variant_access(); }

}  // namespace

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

DocumentResult parse_document(std::string_view utf8_text, const ParseOptions& options) {
    detail::MemorySource source(utf8_text, options.max_document_size);
    return build_document(source, options);
}

DocumentResult parse_document_stream(std::istream& in, const ParseOptions& options) {
    detail::StreamSource source(in, options);
    return build_document(source, options);
}

DocumentResult parse_document_file(const std::filesystem::path& path, const ParseOptions& options) {
    // Same handling as parse_file(): whole file, encoding detected from the BOM.
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return failure(ErrorCode::IoError);
    const std::string raw(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>{});
    const auto text = decode_text(raw);
    if (!text) return failure(ErrorCode::InvalidEncoding);
    return parse_document(*text, options);
}

std::size_t Document::memory_usage() const noexcept {
    return nodes_.capacity() * sizeof(detail::DocumentNode) + elements_.capacity() * sizeof(std::uint32_t) +
           members_.capacity() * sizeof(detail::DocumentMember) + strings_.capacity();
}

// ---------------------------------------------------------------------------
// ValueRef
// ---------------------------------------------------------------------------

bool ValueRef::as_bool() const {
    if (!is_bool()) wrong_type();
    return node().payload != 0;
}

std::int64_t ValueRef::as_integer() const {
    if (!is_integer()) wrong_type();
    std::int64_t i;
    std::memcpy(&i, &node().payload, sizeof i);
    return i;
}

double ValueRef::as_double() const {
    if (is_integer()) return static_cast<double>(as_integer());
    if (!is_double()) wrong_type();
    double d;
    std::memcpy(&d, &node().payload, sizeof d);
    return d;
}

std::string_view ValueRef::as_string() const {
    if (!is_string()) wrong_type();
    const DocumentNode& n = node();
    return std::string_view(doc_->strings_.data() + n.payload, n.count);
}

std::size_t ValueRef::size() const noexcept { return is_array() || is_object() ? node().count : 0; }

ValueRef ValueRef::operator[](std::size_t index) const {
    if (!is_array()) wrong_type();
    const DocumentNode& n = node();
    if (index >= n.count) throw std::out_of_range("wjson::ValueRef: array index out of range");
    return ValueRef(doc_, doc_->elements_[n.payload + index]);
}

std::string_view ValueRef::key(std::size_t index) const {
    if (!is_object()) wrong_type();
    const DocumentNode& n = node();
    if (index >= n.count) throw std::out_of_range("wjson::ValueRef: member index out of range");
    const DocumentMember& m = doc_->members_[n.payload + index];
    return std::string_view(doc_->strings_.data() + m.key_offset, m.key_length);
}

ValueRef ValueRef::value(std::size_t index) const {
    if (!is_object()) wrong_type();
    const DocumentNode& n = node();
    if (index >= n.count) throw std::out_of_range("wjson::ValueRef: member index out of range");
    return ValueRef(doc_, doc_->members_[n.payload + index].value);
}

std::optional<ValueRef> ValueRef::find(std::string_view key) const noexcept {
    if (!is_object()) return std::nullopt;
    const DocumentNode& n = node();
    // Reverse search: the last duplicate wins, as in Value::find().
    for (std::size_t i = n.count; i-- > 0;) {
        const DocumentMember& m = doc_->members_[n.payload + i];
        if (std::string_view(doc_->strings_.data() + m.key_offset, m.key_length) == key)
            return ValueRef(doc_, m.value);
    }
    return std::nullopt;
}

std::optional<ValueRef> ValueRef::at_pointer(std::string_view pointer) const noexcept {
    if (pointer.empty()) return *this;
    if (pointer[0] != '/') return std::nullopt;
    ValueRef current = *this;
    std::string token;
    try {
        while (!pointer.empty()) {
            pointer.remove_prefix(1);  // '/'
            const std::size_t end = pointer.find('/');
            const std::string_view raw = pointer.substr(0, end);
            pointer = end == std::string_view::npos ? std::string_view{} : pointer.substr(end);

            if (current.is_object()) {
                if (!detail::unescape_token(raw, token)) return std::nullopt;
                const auto next = current.find(token);
                if (!next) return std::nullopt;
                current = *next;
            } else if (current.is_array()) {
                std::size_t index = 0;
                // "-" (one past the end) never refers to an existing element.
                if (!detail::parse_index(raw, index) || index >= current.size()) return std::nullopt;
                current = current[index];
            } else {
                return std::nullopt;
            }
        }
    } catch (...) {
        return std::nullopt;  // std::bad_alloc while unescaping
    }
    return current;
}

Value ValueRef::to_value() const {
    switch (type()) {
        case Type::Null: return Value(nullptr);
        case Type::Boolean: return Value(as_bool());
        case Type::Integer: return Value(as_integer());
        case Type::Double: return Value(as_double());
        case Type::String: return Value(as_string());
        case Type::Array: {
            Array array;
            array.reserve(size());
            for (std::size_t i = 0; i < size(); ++i) array.push_back((*this)[i].to_value());
            return Value(std::move(array));
        }
        case Type::Object: {
            Object object;
            object.reserve(size());
            for (std::size_t i = 0; i < size(); ++i) object.emplace_back(std::string(key(i)), value(i).to_value());
            return Value(std::move(object));
        }
    }
    return Value();
}

}  // namespace wjson
