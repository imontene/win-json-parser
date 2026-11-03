// Input sources for the grammar in reader.hpp. Not part of the public API.
// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <string>
#include <string_view>
#include <vector>

#include "utf8.hpp"
#include "wjson/wjson.hpp"

namespace wjson::detail {

// Position of a byte in the input. Sources that cannot compute line/column
// cheaply on the fly leave them at 0 and fill them in make_error().
struct Position {
    std::size_t offset = 0;
    std::size_t line = 0;
    std::size_t column = 0;
};

// Source interface used by Reader:
//   int peek()          next byte (0-255) or -1 at the end / on source error
//   void advance()      consume the byte returned by peek()
//   Position mark()     position of the next byte
//   ErrorCode error()   error raised by the source itself (I/O, encoding, size)
//   ParseError make_error(ErrorCode, Position)

// Whole document in memory. Line and column are computed only on error.
class MemorySource {
public:
    MemorySource(std::string_view text, std::size_t max_document_size) noexcept
        : text_(text), size_(std::min(text.size(), max_document_size)) {}

    int peek() noexcept {
        if (pos_ < size_) return static_cast<unsigned char>(text_[pos_]);
        if (size_ < text_.size()) error_ = ErrorCode::DocumentTooLarge;
        return -1;
    }
    void advance() noexcept { ++pos_; }
    Position mark() const noexcept { return {pos_, 0, 0}; }
    ErrorCode error() const noexcept { return error_; }

    ParseError make_error(ErrorCode code, Position p) const noexcept {
        ParseError e;
        e.code = code;
        e.offset = p.offset;
        auto [line, column] = line_and_column(text_, p.offset);
        e.line = line;
        e.column = column;
        return e;
    }

private:
    std::string_view text_;
    std::size_t size_;
    std::size_t pos_ = 0;
    ErrorCode error_ = ErrorCode::None;
};

// Reads a std::istream in fixed-size chunks. UTF-16 input (detected by BOM)
// is transcoded to UTF-8 chunk by chunk, so memory use stays constant.
class StreamSource {
public:
    StreamSource(std::istream& in, const ParseOptions& options)
        : in_(in),
          chunk_size_(std::max<std::size_t>(options.stream_buffer_size, 4)),
          max_document_size_(options.max_document_size) {
        detect_encoding();
    }

    int peek() {
        if (pos_ < len_) return static_cast<unsigned char>(buf_[pos_]);
        return refill() ? static_cast<unsigned char>(buf_[pos_]) : -1;
    }

    void advance() noexcept {
        const auto c = static_cast<unsigned char>(buf_[pos_++]);
        if (c == '\n') {
            if (!prev_cr_) ++line_;
            column_ = 1;
            prev_cr_ = false;
        } else if (c == '\r') {
            ++line_;
            column_ = 1;
            prev_cr_ = true;
        } else {
            prev_cr_ = false;
            if ((c & 0xC0) != 0x80) ++column_;
        }
    }

    Position mark() const noexcept { return {base_ + pos_, line_, column_}; }
    ErrorCode error() const noexcept { return error_; }

    ParseError make_error(ErrorCode code, Position p) const noexcept {
        ParseError e;
        e.code = code;
        e.offset = p.offset;
        e.line = code == ErrorCode::IoError ? 0 : p.line;
        e.column = code == ErrorCode::IoError ? 0 : p.column;
        return e;
    }

private:
    enum class Encoding { Utf8, Utf16LE, Utf16BE };

    // Reads up to n raw bytes into dst; returns the count. Flags I/O errors.
    std::size_t read_raw(char* dst, std::size_t n) {
        if (n == 0 || eof_) return 0;
        in_.read(dst, static_cast<std::streamsize>(n));
        const auto got = static_cast<std::size_t>(in_.gcount());
        if (in_.bad()) {
            error_ = ErrorCode::IoError;
            eof_ = true;
            return 0;
        }
        if (got < n) eof_ = true;
        return got;
    }

    void detect_encoding() {
        // Read up to 3 bytes and push back what is not a BOM.
        char head[3];
        const std::size_t n = read_raw(head, 3);
        const std::string_view h(head, n);
        std::size_t skip = 0;
        if (h.size() == 3 && h == "\xEF\xBB\xBF") {
            skip = 3;
        } else if (h.substr(0, 2) == "\xFF\xFE") {
            encoding_ = Encoding::Utf16LE;
            skip = 2;
        } else if (h.substr(0, 2) == "\xFE\xFF") {
            encoding_ = Encoding::Utf16BE;
            skip = 2;
        }
        pending_raw_.assign(head + skip, head + n);
    }

    // Loads the next chunk of UTF-8 into buf_. Returns false at the end.
    bool refill() {
        if (finished_) return false;  // stay at the end once there
        finished_ = !refill_chunk();
        return !finished_;
    }

    bool refill_chunk() {
        const bool leftover = buf_.size() > len_;  // bytes cut off by the size limit
        base_ += len_;
        pos_ = 0;
        len_ = 0;
        if (error_ != ErrorCode::None) return false;
        if (base_ >= max_document_size_) {
            // Anything beyond the limit is an error, not a silent truncation.
            if (leftover || !pending_raw_.empty() || has_more_input()) error_ = ErrorCode::DocumentTooLarge;
            return false;
        }
        buf_.clear();
        while (buf_.empty()) {
            if (encoding_ == Encoding::Utf8) {
                fill_utf8();
            } else if (!fill_utf16()) {
                return false;
            }
            if (buf_.empty() && eof_ && pending_raw_.empty()) break;
        }
        len_ = buf_.size();
        if (base_ + len_ > max_document_size_) len_ = max_document_size_ - base_;
        return len_ > 0;
    }

    bool has_more_input() {
        if (eof_) return false;
        return in_.peek() != std::istream::traits_type::eof();
    }

    void fill_utf8() {
        buf_.append(pending_raw_);
        pending_raw_.clear();
        const std::size_t have = buf_.size();
        buf_.resize(chunk_size_ > have ? chunk_size_ : have);
        const std::size_t got = read_raw(buf_.data() + have, buf_.size() - have);
        buf_.resize(have + got);
    }

    // Transcodes one chunk of UTF-16 to UTF-8. Returns false on bad input.
    bool fill_utf16() {
        std::string& raw = raw_;
        raw.assign(pending_raw_);
        pending_raw_.clear();
        const std::size_t have = raw.size();
        raw.resize(have + chunk_size_);
        raw.resize(have + read_raw(raw.data() + have, chunk_size_));

        const bool big_endian = encoding_ == Encoding::Utf16BE;
        std::size_t i = 0;
        for (; i + 1 < raw.size(); i += 2) {
            const auto b0 = static_cast<unsigned char>(raw[i]);
            const auto b1 = static_cast<unsigned char>(raw[i + 1]);
            const std::uint32_t unit = big_endian ? (std::uint32_t{b0} << 8) | b1 : (std::uint32_t{b1} << 8) | b0;
            if (high_surrogate_ != 0) {
                if (unit < 0xDC00 || unit > 0xDFFF) return encoding_error();
                append_utf8(buf_, 0x10000 + ((high_surrogate_ - 0xD800) << 10) + (unit - 0xDC00));
                high_surrogate_ = 0;
            } else if (unit >= 0xD800 && unit <= 0xDBFF) {
                high_surrogate_ = unit;
            } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
                return encoding_error();
            } else {
                append_utf8(buf_, unit);
            }
        }
        pending_raw_.assign(raw, i, std::string::npos);  // odd trailing byte, if any
        if (eof_ && (high_surrogate_ != 0 || !pending_raw_.empty())) return encoding_error();
        return true;
    }

    bool encoding_error() {
        // Report the problem at the end of what was decoded successfully.
        error_ = ErrorCode::InvalidEncoding;
        buf_.clear();
        pending_raw_.clear();
        return false;
    }

    std::istream& in_;
    std::size_t chunk_size_;
    std::size_t max_document_size_;
    Encoding encoding_ = Encoding::Utf8;

    std::string buf_;          // decoded UTF-8 chunk
    std::string pending_raw_;  // raw bytes read but not yet decoded
    std::string raw_;          // UTF-16 read buffer (reused between chunks)
    std::size_t pos_ = 0;      // position in buf_
    std::size_t len_ = 0;      // usable bytes in buf_
    std::size_t base_ = 0;     // document offset of buf_[0]
    std::uint32_t high_surrogate_ = 0;
    bool eof_ = false;       // no more raw input
    bool finished_ = false;  // no more decoded input

    std::size_t line_ = 1;
    std::size_t column_ = 1;
    bool prev_cr_ = false;

    ErrorCode error_ = ErrorCode::None;
};

}  // namespace wjson::detail
