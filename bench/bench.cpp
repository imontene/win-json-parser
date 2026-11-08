// Throughput benchmark for wjson.
//
// Usage: wjson_bench [--markdown] [--min-time SECONDS] file.json...
//
// For each file, measures parsing into a tree (DOM), streaming validation
// with a no-op handler (SAX, from memory and from a stream) and
// serialization, and reports the best throughput in MB/s.
// SPDX-License-Identifier: MIT
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "wjson/wjson.hpp"

namespace {

using Clock = std::chrono::steady_clock;

// Runs fn repeatedly for at least min_seconds (and at least 3 times) and
// returns the fastest run in seconds. The best run is the least noisy figure
// on shared CI machines.
double best_time(double min_seconds, const std::function<void()>& fn) {
    double best = 1e300;
    double total = 0.0;
    int runs = 0;
    while (runs < 3 || total < min_seconds) {
        const auto start = Clock::now();
        fn();
        const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
        best = std::min(best, elapsed);
        total += elapsed;
        ++runs;
    }
    return best;
}

struct Row {
    std::string file;
    double size_mb;
    double dom, document, sax, stream, write;
};

}  // namespace

int main(int argc, char** argv) {
    bool markdown = false;
    double min_seconds = 1.0;
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--markdown") {
            markdown = true;
        } else if (a == "--min-time" && i + 1 < argc) {
            min_seconds = std::atof(argv[++i]);
        } else {
            files.push_back(a);
        }
    }
    if (files.empty()) {
        std::cerr << "usage: wjson_bench [--markdown] [--min-time SECONDS] file.json...\n";
        return 2;
    }

    std::vector<Row> rows;
    for (const auto& file : files) {
        std::ifstream in(file, std::ios::binary);
        if (!in) {
            std::cerr << "cannot open " << file << '\n';
            return 2;
        }
        std::ostringstream buffer;
        buffer << in.rdbuf();
        const std::string text = buffer.str();

        const auto parsed = wjson::parse(text);
        if (!parsed) {
            std::cerr << file << ": " << parsed.error.message() << '\n';
            return 1;
        }

        Row row;
        row.file = file.substr(file.find_last_of("/\\") + 1);
        row.size_mb = static_cast<double>(text.size()) / 1e6;
        row.dom = best_time(min_seconds, [&] {
            const auto r = wjson::parse(text);
            if (!r) std::abort();
        });
        row.document = best_time(min_seconds, [&] {
            const auto r = wjson::parse_document(text);
            if (!r) std::abort();
        });
        row.sax = best_time(min_seconds, [&] {
            wjson::Handler validator;
            if (!wjson::parse(text, validator).ok()) std::abort();
        });
        row.stream = best_time(min_seconds, [&] {
            std::istringstream stream(text);
            wjson::Handler validator;
            if (!wjson::parse_stream(stream, validator).ok()) std::abort();
        });
        std::size_t sink = 0;
        row.write = best_time(min_seconds, [&] { sink += wjson::serialize(*parsed.value).size(); });
        if (sink == 0) std::abort();
        rows.push_back(row);
    }

    auto mbps = [](const Row& r, double seconds) { return r.size_mb / seconds; };
    if (markdown) {
        std::printf("| File | Size (MB) | DOM parse (MB/s) | Document parse (MB/s) | SAX parse (MB/s) | SAX stream (MB/s) | Serialize (MB/s) |\n");
        std::printf("|---|---:|---:|---:|---:|---:|---:|\n");
        for (const auto& r : rows)
            std::printf("| %s | %.2f | %.0f | %.0f | %.0f | %.0f | %.0f |\n", r.file.c_str(), r.size_mb, mbps(r, r.dom),
                        mbps(r, r.document), mbps(r, r.sax), mbps(r, r.stream), mbps(r, r.write));
    } else {
        std::printf("%-22s %9s %10s %10s %10s %10s %10s\n", "file", "MB", "dom MB/s", "document", "sax MB/s", "stream",
                    "write");
        for (const auto& r : rows)
            std::printf("%-22s %9.2f %10.0f %10.0f %10.0f %10.0f %10.0f\n", r.file.c_str(), r.size_mb, mbps(r, r.dom),
                        mbps(r, r.document), mbps(r, r.sax), mbps(r, r.stream), mbps(r, r.write));
    }
    return 0;
}
