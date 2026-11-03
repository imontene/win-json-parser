// wjson command-line tool. A thin front-end over the wjson library: all JSON
// logic lives in the library so other front-ends (GUI, bindings) can reuse it.
// SPDX-License-Identifier: TBD
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "wjson/wjson.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif

#ifndef WJSON_VERSION
#define WJSON_VERSION "0.0.0"
#endif

namespace {

enum ExitCode { Ok = 0, InvalidJson = 1, UsageOrIoError = 2 };

constexpr const char* usage_text =
    "Usage: wjson <command> [options] [file...]\n"
    "\n"
    "Commands:\n"
    "  validate [file...]            Check that each file is valid JSON\n"
    "  format [--indent N] [file]    Pretty-print JSON (default indent: 2)\n"
    "  minify [file]                 Print JSON without whitespace\n"
    "\n"
    "Options:\n"
    "  --ascii                       Escape non-ASCII characters as \\uXXXX\n"
    "  -h, --help                    Show this help\n"
    "  --version                     Show version\n"
    "\n"
    "Reads standard input when no file (or '-') is given.\n"
    "Exit codes: 0 = valid, 1 = invalid JSON, 2 = usage or I/O error.\n";

#ifdef _WIN32
std::string narrow(const wchar_t* s) {
    const int len = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<std::size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, -1, out.data(), len, nullptr, nullptr);
    return out;
}

std::filesystem::path to_path(const std::string& utf8) {
    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(len > 0 ? len - 1 : 0), L'\0');
    if (len > 0) MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide.data(), len);
    return std::filesystem::path(wide);
}
#else
std::filesystem::path to_path(const std::string& utf8) { return std::filesystem::path(utf8); }
#endif

struct Input {
    std::string name;  // for messages (UTF-8)
    std::string text;  // decoded UTF-8 content
};

// Reads a file (or stdin for "-") and decodes it to UTF-8.
// Returns std::nullopt and prints an error on failure.
std::optional<Input> read_input(const std::string& name, int& exit_code) {
    std::string raw;
    if (name == "-") {
        raw.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>{});
    } else {
        std::ifstream in(to_path(name), std::ios::binary);
        if (!in.is_open()) {
            std::cerr << "wjson: " << name << ": cannot open file\n";
            exit_code = UsageOrIoError;
            return std::nullopt;
        }
        raw.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>{});
    }
    auto text = wjson::decode_text(raw);
    if (!text) {
        std::cerr << (name == "-" ? "<stdin>" : name) << ": error: "
                  << wjson::message(wjson::ErrorCode::InvalidEncoding) << "\n";
        exit_code = InvalidJson;
        return std::nullopt;
    }
    return Input{name == "-" ? "<stdin>" : name, std::move(*text)};
}

bool is_continuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

// Prints "file:line:col: error: message" followed by the offending line and a
// caret. Long lines (e.g. minified JSON) are cut to a window around the error.
void report(const Input& input, const wjson::ParseError& error) {
    std::cerr << input.name << ':' << error.line << ':' << error.column
              << ": error: " << error.message() << '\n';

    const std::string& t = input.text;
    const std::size_t offset = std::min(error.offset, t.size());
    std::size_t line_start = t.find_last_of("\r\n", offset == 0 ? 0 : offset - 1);
    line_start = (line_start == std::string::npos || offset == 0) ? 0 : line_start + 1;
    std::size_t line_end = t.find_first_of("\r\n", offset);
    if (line_end == std::string::npos) line_end = t.size();

    constexpr std::size_t window = 60;
    std::size_t from = offset > line_start + window ? offset - window : line_start;
    while (from > line_start && is_continuation(t[from])) --from;
    std::size_t to = std::min(line_end, offset + window);
    while (to < line_end && is_continuation(t[to])) ++to;

    std::string snippet = from > line_start ? "..." : "";
    std::size_t caret = snippet.size();
    for (std::size_t i = from; i < to; ++i) {
        const char c = t[i];
        snippet.push_back(c == '\t' ? ' ' : c);
        if (i < offset && !is_continuation(c)) ++caret;
    }
    if (to < line_end) snippet += "...";

    std::cerr << "  " << snippet << '\n' << "  " << std::string(caret, ' ') << "^\n";
}

int run(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << usage_text;
        return UsageOrIoError;
    }
    const std::string& command = args[0];
    if (command == "-h" || command == "--help" || command == "help") {
        std::cout << usage_text;
        return Ok;
    }
    if (command == "--version") {
        std::cout << "wjson " << WJSON_VERSION << '\n';
        return Ok;
    }
    if (command != "validate" && command != "format" && command != "minify") {
        std::cerr << "wjson: unknown command '" << command << "'\n\n" << usage_text;
        return UsageOrIoError;
    }

    wjson::WriteOptions write_options;
    write_options.indent = command == "format" ? 2 : 0;
    std::vector<std::string> files;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--ascii") {
            write_options.ascii_only = true;
        } else if (a == "--indent" && command == "format" && i + 1 < args.size()) {
            const int n = std::atoi(args[++i].c_str());
            if (n < 1 || n > 16) {
                std::cerr << "wjson: --indent must be between 1 and 16\n";
                return UsageOrIoError;
            }
            write_options.indent = n;
        } else if (a.size() > 1 && a[0] == '-' && a != "-") {
            std::cerr << "wjson: unknown option '" << a << "'\n";
            return UsageOrIoError;
        } else {
            files.push_back(a);
        }
    }
    if (files.empty()) files.emplace_back("-");
    if (command != "validate" && files.size() > 1) {
        std::cerr << "wjson: " << command << " accepts a single file\n";
        return UsageOrIoError;
    }

    int exit_code = Ok;
    for (const auto& file : files) {
        auto input = read_input(file, exit_code);
        if (!input) continue;
        const auto result = wjson::parse(input->text);
        if (!result) {
            report(*input, result.error);
            if (exit_code == Ok) exit_code = InvalidJson;
            continue;
        }
        if (command == "validate") {
            if (files.size() > 1) std::cout << input->name << ": ok\n";
        } else {
            std::cout << wjson::serialize(*result.value, write_options) << '\n';
        }
    }
    return exit_code;
}

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    // UTF-8 console output and no CRLF translation of JSON data.
    SetConsoleOutputCP(CP_UTF8);
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.push_back(narrow(argv[i]));
    return run(args);
}
#else
int main(int argc, char** argv) {
    return run(std::vector<std::string>(argv + 1, argv + argc));
}
#endif
