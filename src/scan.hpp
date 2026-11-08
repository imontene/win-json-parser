// Byte scanning for the parser's fast paths. Internal.
//
// SSE2 (always present on x64, and MSVC's default on x86) checks 16 bytes per
// step. Elsewhere a portable SWAR loop checks 8 bytes per step. Both are only
// used to find where a run ends; the caller handles the byte that ends it.
// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define WJSON_HAVE_SSE2 1
#include <emmintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#endif

namespace wjson::detail {

#if defined(WJSON_HAVE_SSE2)
// Index of the lowest set bit; mask must not be 0.
inline unsigned lowest_bit(unsigned mask) noexcept {
#if defined(_MSC_VER)
    unsigned long index;
    _BitScanForward(&index, mask);
    return static_cast<unsigned>(index);
#else
    return static_cast<unsigned>(__builtin_ctz(mask));
#endif
}
#endif

inline bool is_plain_string_byte(unsigned char c) noexcept { return c != '"' && c != '\\' && c >= 0x20 && c < 0x80; }

// Length of the leading run of bytes that need no special handling in a
// string: not '"', '\\', a control character or a non-ASCII byte.
inline std::size_t plain_run(const char* p, std::size_t n) noexcept {
    std::size_t i = 0;
#if defined(WJSON_HAVE_SSE2)
    const __m128i quote = _mm_set1_epi8('"');
    const __m128i backslash = _mm_set1_epi8('\\');
    const __m128i space = _mm_set1_epi8(0x20);
    for (; i + 16 <= n; i += 16) {
        const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + i));
        // Signed comparison: bytes >= 0x80 are negative, so "< 0x20" also
        // catches every non-ASCII byte.
        const __m128i special =
            _mm_or_si128(_mm_or_si128(_mm_cmpeq_epi8(v, quote), _mm_cmpeq_epi8(v, backslash)), _mm_cmplt_epi8(v, space));
        const auto mask = static_cast<unsigned>(_mm_movemask_epi8(special));
        if (mask != 0) return i + lowest_bit(mask);
    }
#endif
    // SWAR: 8 bytes per step. A false positive only falls back to the byte loop.
    constexpr std::uint64_t ones = 0x0101010101010101ULL;
    constexpr std::uint64_t highs = 0x8080808080808080ULL;
    for (; i + 8 <= n; i += 8) {
        std::uint64_t x;
        std::memcpy(&x, p + i, 8);
        const std::uint64_t q = x ^ (ones * std::uint64_t{'"'});
        const std::uint64_t b = x ^ (ones * std::uint64_t{'\\'});
        const std::uint64_t special = ((q - ones) & ~q) | ((b - ones) & ~b) | ((x - ones * std::uint64_t{0x20}) & ~x) | x;
        if ((special & highs) != 0) break;
    }
    while (i < n && is_plain_string_byte(static_cast<unsigned char>(p[i]))) ++i;
    return i;
}

inline bool is_json_space(char c) noexcept { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }

// Length of the leading run of JSON whitespace.
inline std::size_t whitespace_run(const char* p, std::size_t n) noexcept {
    std::size_t i = 0;
#if defined(WJSON_HAVE_SSE2)
    const __m128i space = _mm_set1_epi8(' ');
    const __m128i newline = _mm_set1_epi8('\n');
    const __m128i carriage_return = _mm_set1_epi8('\r');
    const __m128i tab = _mm_set1_epi8('\t');
    for (; i + 16 <= n; i += 16) {
        const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + i));
        const __m128i ws = _mm_or_si128(_mm_or_si128(_mm_cmpeq_epi8(v, space), _mm_cmpeq_epi8(v, newline)),
                                        _mm_or_si128(_mm_cmpeq_epi8(v, carriage_return), _mm_cmpeq_epi8(v, tab)));
        const auto mask = static_cast<unsigned>(_mm_movemask_epi8(ws));
        if (mask != 0xFFFFu) return i + lowest_bit(~mask & 0xFFFFu);
    }
#endif
    // Indentation: 8 spaces per step.
    for (; i + 8 <= n; i += 8) {
        std::uint64_t x;
        std::memcpy(&x, p + i, 8);
        if (x != 0x2020202020202020ULL) break;
    }
    while (i < n && is_json_space(p[i])) ++i;
    return i;
}

}  // namespace wjson::detail
