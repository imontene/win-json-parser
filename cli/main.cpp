// wjson command-line tool. A thin front-end over the wjson library: all JSON
// logic lives in the library so other front-ends (GUI, bindings) can reuse it.
// SPDX-License-Identifier: MIT
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "diff.hpp"
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

enum ExitCode { Ok = 0, InvalidJson = 1, UsageOrIoError = 2, NotFound = 3, Different = 4 };

constexpr const char* usage_text =
    "Usage: wjson <command> [options] [file...]\n"
    "\n"
    "Commands:\n"
    "  validate [file...]            Check that each file is valid JSON\n"
    "  format [--indent N] [file]    Pretty-print JSON (default indent: 2)\n"
    "  minify [file]                 Print JSON without whitespace\n"
    "  query <pointer> [file]        Print the value at a JSON Pointer, e.g. /items/0/name\n"
    "  diff <left> <right>           Show the differences between two JSON documents\n"
    "\n"
    "Options:\n"
    "  --jsonc                       Accept comments and trailing commas (JSONC)\n"
    "  --max-depth N                 Maximum nesting depth (default: 512)\n"
    "  --ascii                       Escape non-ASCII characters as \\uXXXX\n"
    "  --raw                         query: print strings without quotes\n"
    "  -q, --quiet                   diff: print nothing, only set the exit code\n"
    "  -h, --help                    Show this help\n"
    "  --version                     Show version\n"
    "\n"
    "Reads standard input when no file (or '-') is given. 'validate' streams\n"
    "files, so it checks files of any size with constant memory.\n"
    "\n"
    "'diff' compares objects ignoring member order, aligns array elements (an\n"
    "insertion is reported once) and compares numbers by value (1 = 1.0). Each\n"
    "difference is one line: '+ /pointer: value' (only in <right>),\n"
    "'- /pointer: value' (only in <left>) or '~ /pointer: old -> new'.\n"
    "\n"
    "Exit codes: 0 = ok (diff: equal), 1 = invalid JSON, 2 = usage or I/O error,\n"
    "            3 = query pointer not found, 4 = diff: the documents differ.\n";

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
void print_error_header(const std::string& name, const wjson::ParseError& error) {
    if (error.line == 0) {
        std::cerr << name << ": error: " << error.message() << '\n';
    } else {
        std::cerr << name << ':' << error.line << ':' << error.column << ": error: " << error.message() << '\n';
    }
}

void report(const Input& input, const wjson::ParseError& error) {
    print_error_header(input.name, error);
    if (error.line == 0) return;

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

// Files up to this size are re-read after a streaming error to show the
// offending line; bigger files only get the line and column.
constexpr std::uintmax_t max_snippet_file_size = 64u * 1024 * 1024;

// Streams a file through the parser without building a tree.
bool validate_file(const std::string& name, const wjson::ParseOptions& options, int& exit_code) {
    wjson::Handler validator;
    const auto error = wjson::parse_file(to_path(name), validator, options);
    if (error.ok()) return true;
    if (error.code == wjson::ErrorCode::IoError) {
        std::cerr << "wjson: " << name << ": cannot open file\n";
        exit_code = UsageOrIoError;
        return false;
    }
    std::error_code ec;
    const auto size = std::filesystem::file_size(to_path(name), ec);
    int ignored = Ok;
    std::optional<Input> input;
    if (!ec && size <= max_snippet_file_size && error.line != 0) input = read_input(name, ignored);
    if (input) {
        report(*input, error);
    } else {
        print_error_header(name, error);
    }
    if (exit_code == Ok) exit_code = InvalidJson;
    return false;
}

bool parse_size(const std::string& text, std::size_t& out) {
    if (text.empty() || text.size() > 9) return false;
    std::size_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<std::size_t>(c - '0');
    }
    out = value;
    return true;
}

int run_diff(const std::vector<std::string>& files, const wjson::ParseOptions& options, bool ascii_only,
             bool quiet) {
    if (files.size() != 2) {
        std::cerr << "wjson: diff needs two files, for example: wjson diff old.json new.json\n";
        return UsageOrIoError;
    }
    if (files[0] == "-" && files[1] == "-") {
        std::cerr << "wjson: diff can read only one of the two documents from standard input\n";
        return UsageOrIoError;
    }
    int exit_code = Ok;
    wjson::DocumentResult documents[2];
    for (std::size_t i = 0; i < 2; ++i) {
        const auto input = read_input(files[i], exit_code);
        if (!input) return exit_code;
        documents[i] = wjson::parse_document(input->text, options);
        if (!documents[i]) {
            report(*input, documents[i].error);
            return InvalidJson;
        }
    }
    const std::size_t count = wjson_cli::diff(
        documents[0].document->root(), documents[1].document->root(), [&](const wjson_cli::DiffEntry& entry) {
            if (!quiet) std::cout << wjson_cli::to_string(entry, ascii_only) << '\n';
        });
    return count == 0 ? Ok : Different;
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
    if (command != "validate" && command != "format" && command != "minify" && command != "query" &&
        command != "diff") {
        std::cerr << "wjson: unknown command '" << command << "'\n\n" << usage_text;
        return UsageOrIoError;
    }

    wjson::ParseOptions parse_options;
    wjson::WriteOptions write_options;
    write_options.indent = command == "format" ? 2 : 0;
    bool raw_strings = false;
    bool quiet = false;
    std::optional<std::string> pointer;
    std::vector<std::string> files;

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--ascii") {
            write_options.ascii_only = true;
        } else if (a == "--jsonc") {
            parse_options.allow_comments = true;
            parse_options.allow_trailing_commas = true;
        } else if (a == "--raw" && command == "query") {
            raw_strings = true;
        } else if ((a == "-q" || a == "--quiet") && command == "diff") {
            quiet = true;
        } else if (a == "--indent" && command == "format") {
            std::size_t n = 0;
            if (i + 1 >= args.size() || !parse_size(args[++i], n) || n < 1 || n > 16) {
                std::cerr << "wjson: --indent must be a number between 1 and 16\n";
                return UsageOrIoError;
            }
            write_options.indent = static_cast<int>(n);
        } else if (a == "--max-depth") {
            if (i + 1 >= args.size() || !parse_size(args[++i], parse_options.max_depth) ||
                parse_options.max_depth == 0) {
                std::cerr << "wjson: --max-depth must be a positive number\n";
                return UsageOrIoError;
            }
        } else if (a.size() > 1 && a[0] == '-' && a != "-") {
            std::cerr << "wjson: unknown option '" << a << "'\n";
            return UsageOrIoError;
        } else if (command == "query" && !pointer) {
            pointer = a;
        } else {
            files.push_back(a);
        }
    }
    if (command == "query" && !pointer) {
        std::cerr << "wjson: query needs a JSON Pointer, for example: wjson query /items/0 data.json\n";
        return UsageOrIoError;
    }
    if (command == "diff") return run_diff(files, parse_options, write_options.ascii_only, quiet);
    if (files.empty()) files.emplace_back("-");
    if (command != "validate" && files.size() > 1) {
        std::cerr << "wjson: " << command << " accepts a single file\n";
        return UsageOrIoError;
    }

    int exit_code = Ok;
    for (const auto& file : files) {
        if (command == "validate" && file != "-") {
            if (validate_file(file, parse_options, exit_code) && files.size() > 1)
                std::cout << file << ": ok\n";
            continue;
        }

        auto input = read_input(file, exit_code);
        if (!input) continue;
        // A compact read-only Document: about half the memory of a Value tree.
        const auto result = wjson::parse_document(input->text, parse_options);
        if (!result) {
            report(*input, result.error);
            if (exit_code == Ok) exit_code = InvalidJson;
            continue;
        }
        if (command == "validate") {
            if (files.size() > 1) std::cout << input->name << ": ok\n";
        } else if (command == "query") {
            const auto found = result.document->root().at_pointer(*pointer);
            if (!found) {
                std::cerr << "wjson: " << input->name << ": no value at '" << *pointer << "'\n";
                return NotFound;
            }
            if (raw_strings && found->is_string()) {
                std::cout << found->as_string() << '\n';
            } else {
                std::cout << wjson::serialize(*found, write_options) << '\n';
            }
        } else {
            std::cout << wjson::serialize(result.document->root(), write_options) << '\n';
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
