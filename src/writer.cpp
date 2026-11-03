// SPDX-License-Identifier: TBD
#include <charconv>
#include <cmath>

#include "utf8.hpp"
#include "wjson/wjson.hpp"

namespace wjson {

namespace {

constexpr char hex_digits[] = "0123456789abcdef";

void append_u_escape(std::string& out, std::uint32_t unit) {
    out += "\\u";
    out.push_back(hex_digits[(unit >> 12) & 0xF]);
    out.push_back(hex_digits[(unit >> 8) & 0xF]);
    out.push_back(hex_digits[(unit >> 4) & 0xF]);
    out.push_back(hex_digits[unit & 0xF]);
}

class Writer {
public:
    explicit Writer(const WriteOptions& options) : options_(options) {}

    std::string take() { return std::move(out_); }

    void write(const Value& v, int level) {
        switch (v.type()) {
            case Type::Null: out_ += "null"; break;
            case Type::Boolean: out_ += v.as_bool() ? "true" : "false"; break;
            case Type::Integer: write_integer(v.as_integer()); break;
            case Type::Double: write_double(v.as_double()); break;
            case Type::String: write_string(v.as_string()); break;
            case Type::Array: write_array(v.as_array(), level); break;
            case Type::Object: write_object(v.as_object(), level); break;
        }
    }

private:
    bool pretty() const noexcept { return options_.indent > 0; }

    void newline(int level) {
        if (!pretty()) return;
        out_.push_back('\n');
        out_.append(static_cast<std::size_t>(level) * static_cast<std::size_t>(options_.indent), ' ');
    }

    void write_integer(std::int64_t i) {
        char buf[32];
        const auto r = std::to_chars(buf, buf + sizeof buf, i);
        out_.append(buf, r.ptr);
    }

    void write_double(double d) {
        // JSON has no NaN/Infinity; follow JSON.stringify and emit null.
        if (!std::isfinite(d)) {
            out_ += "null";
            return;
        }
        char buf[64];
        const auto r = std::to_chars(buf, buf + sizeof buf, d);  // shortest round-trip form
        const std::string_view s(buf, static_cast<std::size_t>(r.ptr - buf));
        out_ += s;
        // Keep the value a double when it is parsed back ("1" would become an integer).
        if (s.find_first_of(".eE") == std::string_view::npos) out_ += ".0";
    }

    void write_string(std::string_view s) {
        out_.push_back('"');
        std::size_t i = 0;
        while (i < s.size()) {
            const auto c = static_cast<unsigned char>(s[i]);
            if (c >= 0x80) {
                if (options_.ascii_only) {
                    const std::uint32_t cp = detail::decode_utf8(s, i);
                    if (cp >= 0x10000) {
                        const std::uint32_t v = cp - 0x10000;
                        append_u_escape(out_, 0xD800 + (v >> 10));
                        append_u_escape(out_, 0xDC00 + (v & 0x3FF));
                    } else {
                        append_u_escape(out_, cp);
                    }
                } else {
                    const std::size_t len = detail::utf8_sequence_length(s, i);
                    if (len == 0) {
                        detail::append_utf8(out_, detail::replacement_character);
                        ++i;
                    } else {
                        out_.append(s.data() + i, len);
                        i += len;
                    }
                }
                continue;
            }
            switch (c) {
                case '"': out_ += "\\\""; break;
                case '\\': out_ += "\\\\"; break;
                case '\b': out_ += "\\b"; break;
                case '\f': out_ += "\\f"; break;
                case '\n': out_ += "\\n"; break;
                case '\r': out_ += "\\r"; break;
                case '\t': out_ += "\\t"; break;
                default:
                    if (c < 0x20) append_u_escape(out_, c);
                    else out_.push_back(static_cast<char>(c));
            }
            ++i;
        }
        out_.push_back('"');
    }

    void write_array(const Array& array, int level) {
        out_.push_back('[');
        for (std::size_t i = 0; i < array.size(); ++i) {
            if (i > 0) out_.push_back(',');
            newline(level + 1);
            write(array[i], level + 1);
        }
        if (!array.empty()) newline(level);
        out_.push_back(']');
    }

    void write_object(const Object& object, int level) {
        out_.push_back('{');
        for (std::size_t i = 0; i < object.size(); ++i) {
            if (i > 0) out_.push_back(',');
            newline(level + 1);
            write_string(object[i].first);
            out_ += pretty() ? ": " : ":";
            write(object[i].second, level + 1);
        }
        if (!object.empty()) newline(level);
        out_.push_back('}');
    }

    const WriteOptions& options_;
    std::string out_;
};

}  // namespace

std::string serialize(const Value& value, const WriteOptions& options) {
    Writer writer(options);
    writer.write(value, 0);
    return writer.take();
}

}  // namespace wjson
