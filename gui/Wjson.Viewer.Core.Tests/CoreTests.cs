using System;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using Wjson;
using Wjson.Viewer.Core;
using Xunit;

// Tests change Strings.Culture (shared state), so run them one at a time.
[assembly: CollectionBehavior(DisableTestParallelization = true)]

public class TreeTests
{
    private static JsonTreeNode Root(string json) => DocumentAnalysis.Analyze(json, jsonc: false).Root!;

    [Fact]
    public void Builds_labels_values_and_pointers()
    {
        Strings.Culture = CultureInfo.GetCultureInfo("en-US");
        var root = Root("{\"name\": \"wjson\", \"n\": 42, \"pi\": 3.5, \"ok\": true, \"none\": null, \"a/b\": [1, {\"x~y\": 2}]}");
        Assert.Equal(NodeKind.Object, root.Kind);
        Assert.Equal("6 members", root.ValueText);
        Assert.Equal("", root.Pointer);

        var children = root.GetChildren();
        Assert.Equal(new[] { "name", "n", "pi", "ok", "none", "a/b" }, children.Select(c => c.Label));
        Assert.Equal(new[] { "\"wjson\"", "42", "3.5", "true", "null", "2 items" }, children.Select(c => c.ValueText));
        Assert.Equal("/a~1b", children[5].Pointer);

        var nested = children[5].GetChildren()[1].GetChildren()[0];
        Assert.Equal("x~y", nested.Label);
        Assert.Equal("/a~1b/1/x~0y", nested.Pointer);
        Assert.False(nested.HasChildren);
    }

    [Fact]
    public void Splits_big_arrays_into_ranges()
    {
        var json = "[" + string.Join(",", Enumerable.Range(0, 2500)) + "]";
        var root = Root(json);
        var ranges = root.GetChildren();
        Assert.Equal(3, ranges.Count);
        Assert.All(ranges, r => Assert.Equal(NodeKind.Range, r.Kind));
        Assert.Equal("[0 … 999]", ranges[0].Label);
        Assert.Equal("[2000 … 2499]", ranges[2].Label);
        Assert.Equal(500, ranges[2].ChildCount);
        var last = ranges[2].GetChildren().Last();
        Assert.Equal("[2499]", last.Label);
        Assert.Equal("/2499", last.Pointer);
        Assert.Equal("2499", last.ValueText);
    }

    [Fact]
    public void Finds_paths_through_ranges()
    {
        var json = "{\"items\": [" + string.Join(",", Enumerable.Range(0, 1500).Select(i => $"{{\"id\": {i}}}")) + "]}";
        var root = Root(json);
        var path = JsonTree.FindPath(root, "/items/1234/id");
        Assert.NotNull(path);
        Assert.Same(root, path![0]);
        Assert.Equal(new[] { "items", "[1000 … 1499]", "[1234]", "id" }, path.Skip(1).Select(n => n.Label));
        Assert.Equal("1234", path!.Last().ValueText);

        Assert.Same(root, JsonTree.FindPath(root, "")!.Single());
        Assert.Null(JsonTree.FindPath(root, "/items/1500"));
        Assert.Null(JsonTree.FindPath(root, "/items/01"));
        Assert.Null(JsonTree.FindPath(root, "/nope"));
        Assert.Null(JsonTree.FindPath(root, "items"));
        Assert.Null(JsonTree.FindPath(root, "/items/0/id/deeper"));
    }

    [Fact]
    public void Parses_and_escapes_pointers()
    {
        Assert.Equal(new[] { "a/b", "m~n", "" }, JsonTree.ParsePointer("/a~1b/m~0n/"));
        Assert.Empty(JsonTree.ParsePointer("")!);
        Assert.Null(JsonTree.ParsePointer("x"));
        Assert.Null(JsonTree.ParsePointer("/a~2"));
        Assert.Equal("a~1b~0c", JsonTree.EscapeToken("a/b~c"));
    }

    [Fact]
    public void Quotes_and_truncates_strings()
    {
        Assert.Equal("\"a\\\"b\\nc\"", JsonTree.Quote("a\"b\nc", 100));
        Assert.Equal("\"\\u0001\"", JsonTree.Quote("\u0001", 100));
        var quoted = JsonTree.Quote(new string('x', 500), 10);
        Assert.EndsWith("…\"", quoted);
        Assert.True(quoted.Length < 20);
    }

    [Fact]
    public void Counts_values()
    {
        Assert.Equal(6, DocumentAnalysis.Analyze("{\"a\": [1, 2], \"b\": {\"c\": null}}", false).ValueCount);
    }
}

public class AnalysisTests
{
    [Fact]
    public void Reports_errors_localized()
    {
        Strings.Culture = CultureInfo.GetCultureInfo("es-ES");
        var result = DocumentAnalysis.Analyze("{\n  \"a\": [1, 2,]\n}", jsonc: false);
        Assert.False(result.IsValid);
        Assert.Equal(JsonErrorCode.UnexpectedCharacter, result.Error!.Code);
        Assert.Equal("Línea 2, columna 14: carácter inesperado", result.Message);

        Strings.Culture = CultureInfo.GetCultureInfo("en-US");
        Assert.Equal("Line 2, column 14: unexpected character", result.Message);
        Assert.Equal("Valid JSON", DocumentAnalysis.Analyze("[]", false).Message);
    }

    [Fact]
    public void Every_error_code_has_a_spanish_message()
    {
        Strings.Culture = CultureInfo.GetCultureInfo("es-ES");
        foreach (JsonErrorCode code in Enum.GetValues(typeof(JsonErrorCode)))
        {
            if (code == JsonErrorCode.None) continue;
            // Errors are produced by the native library; check a real one per code where possible
            // and otherwise that the table covers the code.
            var message = typeof(Strings).GetField("SpanishErrors",
                System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Static)!.GetValue(null)
                as System.Collections.Generic.Dictionary<JsonErrorCode, string>;
            Assert.True(message!.ContainsKey(code), $"missing Spanish text for {code}");
        }
    }

    [Fact]
    public void Jsonc_toggle()
    {
        const string text = "{ // c\n \"a\": 1, }";
        Assert.False(DocumentAnalysis.Analyze(text, jsonc: false).IsValid);
        Assert.True(DocumentAnalysis.Analyze(text, jsonc: true).IsValid);
    }
}

public class TextTests
{
    [Theory]
    [InlineData("abc\ndef", 2, 2, 5)]      // LF
    [InlineData("abc\r\ndef", 2, 2, 6)]    // CRLF counts once
    [InlineData("abc\rdef", 2, 1, 4)]      // WinUI TextBox uses lone CR
    [InlineData("abc", 1, 1, 0)]
    [InlineData("abc", 9, 9, 3)]           // clamped
    public void Line_and_column_to_index(string text, long line, long column, int expected)
    {
        Assert.Equal(expected, TextPositions.IndexOf(text, line, column));
    }

    [Fact]
    public void Columns_count_code_points()
    {
        // "😀" is one code point but two UTF-16 chars.
        var text = "[\"\U0001F600\", x]";
        var result = DocumentAnalysis.Analyze(text, false);
        var index = TextPositions.IndexOf(text, result.Error!.Line, result.Error.Column);
        Assert.Equal('x', text[index]);
    }

    [Fact]
    public void Error_position_matches_text_with_cr_line_breaks()
    {
        var text = "{\r  \"a\": 1,\r  \"b\": tru\r}";
        var result = DocumentAnalysis.Analyze(text, false);
        Assert.Equal(3, result.Error!.Line);
        Assert.Equal('t', text[TextPositions.IndexOf(text, result.Error.Line, result.Error.Column)]);
    }

    [Fact]
    public void Newline_detection_and_normalization()
    {
        Assert.Equal(NewlineStyle.CrLf, Newlines.Detect("a\r\nb\nc"));
        Assert.Equal(NewlineStyle.Lf, Newlines.Detect("a\nb\r\nc"));
        Assert.Equal(NewlineStyle.Lf, Newlines.Detect("abc"));
        Assert.Equal("a\r\nb\r\nc", Newlines.Normalize("a\rb\nc", NewlineStyle.CrLf));
        Assert.Equal("a\nb\nc\n", Newlines.Normalize("a\r\nb\rc\r", NewlineStyle.Lf));
    }

    [Fact]
    public void Sizes_are_human_readable()
    {
        Strings.Culture = CultureInfo.GetCultureInfo("en-US");
        Assert.Equal("512 B", Strings.Size(512));
        Assert.Equal("1.5 KB", Strings.Size(1536));
        Assert.Equal("8 MB", Strings.Size(8L * 1024 * 1024));
    }
}

public class FileTests : IDisposable
{
    private readonly string _dir = Path.Combine(Path.GetTempPath(), "wjson-viewer-" + Guid.NewGuid().ToString("N"));

    public FileTests() => Directory.CreateDirectory(_dir);
    public void Dispose() => Directory.Delete(_dir, recursive: true);

    [Fact]
    public void Round_trips_encoding_and_line_endings()
    {
        var path = Path.Combine(_dir, "utf16.json");
        File.WriteAllText(path, "{\r\n  \"a\": \"\u00e9\"\r\n}", new UnicodeEncoding(false, true));

        var doc = DocumentFile.Load(path);
        Assert.False(doc.IsLarge);
        Assert.Equal("UTF-16 LE", doc.EncodingName);
        Assert.Equal(NewlineStyle.CrLf, doc.NewlineStyle);

        // Simulate the WinUI TextBox, which turns line breaks into "\r".
        var edited = doc.Text!.Replace("\r\n", "\r").Replace("\u00e9", "\u00e8");
        DocumentFile.Save(path, edited, doc.Encoding, doc.NewlineStyle);

        var bytes = File.ReadAllBytes(path);
        Assert.Equal(new byte[] { 0xFF, 0xFE }, bytes.Take(2));
        Assert.Equal("{\r\n  \"a\": \"\u00e8\"\r\n}", File.ReadAllText(path));
    }

    [Fact]
    public void Utf8_without_bom_stays_without_bom()
    {
        var path = Path.Combine(_dir, "plain.json");
        File.WriteAllText(path, "[1]\n", new UTF8Encoding(false));
        var doc = DocumentFile.Load(path);
        Assert.Equal("UTF-8", doc.EncodingName);
        DocumentFile.Save(path, "[2]\r", doc.Encoding, doc.NewlineStyle);
        Assert.Equal(Encoding.ASCII.GetBytes("[2]\n"), File.ReadAllBytes(path));
    }

    [Fact]
    public void Large_files_are_streamed()
    {
        var path = Path.Combine(_dir, "large.json");
        using (var w = new StreamWriter(path))
        {
            w.Write('[');
            for (var i = 0; i < 1_200_000; ++i) w.Write(i == 0 ? "\"value\"" : ",\"value\"");
            w.Write(']');
        }
        var doc = DocumentFile.Load(path);
        Assert.True(doc.IsLarge);
        Assert.Null(doc.Text);
        var result = DocumentAnalysis.AnalyzeFile(doc, jsonc: false);
        Assert.True(result.IsValid);
        Assert.Equal(1_200_001, result.ValueCount);

        File.AppendAllText(path, ",");
        var broken = DocumentAnalysis.AnalyzeFile(DocumentFile.Load(path), jsonc: false);
        Assert.Equal(JsonErrorCode.TrailingCharacters, broken.Error!.Code);
    }
}

public class WriterTests
{
    [Fact]
    public void Writes_compact_and_indented()
    {
        var value = WJson.Parse("{\"a\": [1, 2.5, true, null], \"b\": {}, \"c\": []}");
        Assert.Equal("{\"a\":[1,2.5,true,null],\"b\":{},\"c\":[]}", JsonWriter.Write(value, 0));
        Assert.Equal("{\n  \"a\": [\n    1\n  ]\n}", JsonWriter.Write(WJson.Parse("{\"a\":[1]}"), 2));
        Assert.Equal("3.0", JsonWriter.Write(3.0));
    }

    [Fact]
    public void Round_trips_through_the_native_parser()
    {
        const string json = "{\"s\": \"q\\\"b\\\\n\\n\\t\\u0001\\u00e9\\uD83D\\uDE00\", \"n\": -12, \"big\": 1e300, \"small\": 1e-7, \"x\": [[], {}, [null]]}";
        var value = WJson.Parse(json);
        foreach (var indent in new[] { 0, 2 })
        {
            var again = WJson.Parse(JsonWriter.Write(value, indent));
            Assert.Equal(JsonWriter.Write(value, 0), JsonWriter.Write(again, 0));
        }
        Assert.Equal(WJson.Minify(json), JsonWriter.Write(value, 0));
    }
}
