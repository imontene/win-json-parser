# Imontene.Wjson

.NET bindings for [wjson](https://github.com/imontene/win-json-parser), a strict, fast JSON parser for Windows written in C++.

- Strict RFC 8259 parsing with line/column errors
- JSONC (comments and trailing commas), JSON Pointer queries
- UTF-8 and UTF-16 files (with BOM), streaming validation of files of any size
- Native builds for win-x64, win-x86, win-arm64 and linux-x64

```csharp
using Wjson;

var config = (JsonObject)WJson.ParseFile("settings.json", ParseOptions.Jsonc)!;
Console.WriteLine(config["editor.fontSize"]);

if (!WJson.TryValidateFile("huge.json", out var error))
    Console.WriteLine($"Invalid JSON at {error!.Line}:{error.Column}: {error.Message}");

string pretty = WJson.Format("{\"a\":[1,2]}", indent: 4);
WJson.TryQuery(json, "/items/0/name", out var name);
```

Values map to: `null`, `bool`, `long`, `double`, `string`, `JsonArray` (a `List<object?>`) and `JsonObject` (an ordered dictionary).

License: MIT.
