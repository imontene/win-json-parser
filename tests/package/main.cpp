// Minimal program built against the installed wjson package.
#include <wjson/wjson.hpp>

#include <cstdio>

int main() {
    const auto result = wjson::parse(R"({"package": {"name": "wjson", "ok": true}})");
    if (!result) {
        std::printf("parse failed: %s\n", result.error.message());
        return 1;
    }
    const wjson::Value* ok = result.value->at_pointer("/package/ok");
    if (!ok || !ok->as_bool()) return 1;
    std::printf("found wjson package: %s\n", result.value->at_pointer("/package/name")->as_string().c_str());
    return 0;
}
