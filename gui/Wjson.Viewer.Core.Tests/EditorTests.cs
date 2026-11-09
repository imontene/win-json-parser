using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using Wjson;
using Wjson.Viewer.Core;
using Xunit;

public class SyntaxTests
{
    private static string Describe(string text) =>
        string.Join(" ", JsonSyntax.Tokenize(text).Select(s => $"{s.Kind}:{text.Substring(s.Start, s.Length)}"));

    [Fact]
    public void Colors_keys_values_and_comments()
    {
        Assert.Equal("Key:\"a\" Number:-1.5e3 Key:\"b\" Literal:true Literal:null String:\"x\" Literal:false",
                     Describe("{\"a\": -1.5e3, \"b\": [true, null, \"x\", false]}"));
        Assert.Equal("Comment:// note Key:\"k\" Comment:/* c */ Number:1 Comment:/* multi\r\nline */",
                     Describe("// note\r\n{\"k\" /* c */ : 1 /* multi\r\nline */}"));
        // Escaped quotes stay inside the string; a key is followed by ':' even after a comment.
        Assert.Equal("Key:\"q\\\"r\" String:\"\\\\\"", Describe("{\"q\\\"r\" : \"\\\\\"}"));
        Assert.Equal("Key:\"k\" Comment:/*x*/", Describe("{\"k\"/*x*/:"));
    }

    [Fact]
    public void Tolerates_invalid_text()
    {
        // Unterminated string ends at the line break; unknown words are not colored.
        Assert.Equal("Key:\"a\" String:\"open", Describe("{\"a\": \"open\n}"));
        Assert.Equal("Number:1", Describe("{tru: 1, nul}"));
        Assert.Equal("Comment:/* never closed", Describe("/* never closed"));
        Assert.Empty(JsonSyntax.Tokenize(""));
    }

    // Simulates the editor: paint everything, apply an edit (typed text takes
    // the color of the character before it), repaint only RepaintRange, and
    // compare with painting the new text from scratch.
    private static void CheckRepaint(string before, int start, int removed, string inserted)
    {
        var after = before.Substring(0, start) + inserted + before.Substring(start + removed);
        var oldSpans = JsonSyntax.Tokenize(before);
        var colors = Paint(before, oldSpans).ToList();
        var inherited = start > 0 ? colors[start - 1] : -1;
        colors.RemoveRange(start, removed);
        colors.InsertRange(start, Enumerable.Repeat(inherited, inserted.Length));

        var newSpans = JsonSyntax.Tokenize(after);
        // The real edit (the editor gets it from TextEdit.Between with the caret as a hint).
        var edit = new TextEdit(start, before.Substring(start, removed), inserted);
        var range = JsonSyntax.RepaintRange(oldSpans, newSpans, edit);
        if (range is (int a, int b))
        {
            var fresh = Paint(after, newSpans);
            for (var i = a; i < b; ++i) colors[i] = fresh[i];
        }
        Assert.Equal(Paint(after, newSpans), colors.ToArray());
    }

    private static int[] Paint(string text, List<SyntaxSpan> spans)
    {
        var colors = Enumerable.Repeat(-1, text.Length).ToArray();
        foreach (var s in spans)
            for (var i = s.Start; i < s.End; ++i) colors[i] = (int)s.Kind;
        return colors;
    }

    [Fact]
    public void Repaint_range_covers_every_change()
    {
        const string doc = "{\"name\": \"wjson\", \"n\": 42, \"ok\": true, \"list\": [1, 2, null]} // end";
        for (var start = 0; start <= doc.Length; ++start)
        {
            foreach (var insert in new[] { "x", "\"", "1", ",", "/*", " ", "e", ":" })
                CheckRepaint(doc, start, 0, insert);
            if (start < doc.Length) CheckRepaint(doc, start, 1, "");
            if (start + 3 <= doc.Length) CheckRepaint(doc, start, 3, "\"k\":");
        }
        CheckRepaint(doc, 0, doc.Length, "[true]");
        CheckRepaint("", 0, 0, "{\"a\": 1}");
    }

    [Fact]
    public void Random_edits_repaint_correctly()
    {
        var random = new Random(12345);
        const string alphabet = "{}[]:,\" \r\n/*tfnul0123456789.-ex\\abc";
        for (var round = 0; round < 3000; ++round)
        {
            var length = random.Next(0, 40);
            var sb = new StringBuilder();
            for (var i = 0; i < length; ++i) sb.Append(alphabet[random.Next(alphabet.Length)]);
            var text = sb.ToString();
            var start = random.Next(0, text.Length + 1);
            var removed = random.Next(0, Math.Min(4, text.Length - start) + 1);
            var inserted = new string(Enumerable.Range(0, random.Next(0, 4)).Select(_ => alphabet[random.Next(alphabet.Length)]).ToArray());
            CheckRepaint(text, start, removed, inserted);
        }
    }

    [Fact]
    public void Rtf_escapes_text_and_colors_spans()
    {
        var text = "{\"k\\\\\": \"á€😀\",\r\n\t\"n\": 1}";
        var rtf = JsonSyntax.ToRtf(text, JsonSyntax.Tokenize(text), dark: false, "Cascadia Mono", 9.75);
        Assert.StartsWith("{\\rtf1", rtf);
        Assert.Contains("{\\fonttbl{\\f0\\fmodern\\fprq1 Cascadia Mono;}}", rtf);
        Assert.Contains("\\fs20", rtf);
        Assert.Contains("\\cf2 \"k\\\\\\\\\"\\cf1 ", rtf);                     // key, backslashes escaped
        Assert.Contains("\\u225?\\u8364?\\u-10179?\\u-8704?", rtf);              // á € and a surrogate pair
        Assert.Contains("\\par \\tab ", rtf);                                    // CRLF is one paragraph break
        Assert.Contains("\\cf4 1\\cf1 \\}", rtf);                                // number, then an escaped brace
        Assert.EndsWith("}", rtf);
    }
}

public class LocatorTests
{
    private const string Doc = "{\r\n  \"name\": \"wjson\",\r\n  \"tags\": [\"a\", {\"x/y\": 2}],\r\n  // comment\r\n  \"n\": 42,\r\n}";

    private static int At(string what, int skip = 0) => Doc.IndexOf(what, StringComparison.Ordinal) + skip;

    [Fact]
    public void Pointer_at_a_position()
    {
        Assert.Equal("/name", JsonLocator.PointerAt(Doc, At("\"name\"")));             // the key
        Assert.Equal("/name", JsonLocator.PointerAt(Doc, At("\"wjson\"", 3)));          // the value
        Assert.Equal("/name", JsonLocator.PointerAt(Doc, At(": \"wjson\"")));           // the colon
        Assert.Equal("/tags/0", JsonLocator.PointerAt(Doc, At("\"a\"")));
        Assert.Equal("/tags", JsonLocator.PointerAt(Doc, At(", {")));                   // comma between elements
        Assert.Equal("/tags/1/x~1y", JsonLocator.PointerAt(Doc, At("2}")));
        Assert.Equal("/tags/1", JsonLocator.PointerAt(Doc, At("}]")));
        Assert.Equal("", JsonLocator.PointerAt(Doc, At("// comment")));                 // between members: the object
        Assert.Equal("/n", JsonLocator.PointerAt(Doc, At("42", 1)));
        Assert.Equal("", JsonLocator.PointerAt(Doc, 0));
        Assert.Null(JsonLocator.PointerAt(Doc, Doc.Length));
    }

    [Fact]
    public void Pointer_at_a_caret_prefers_the_value_before_it()
    {
        Assert.Equal("/n", JsonLocator.PointerAtCaret(Doc, At("42", 2)));              // right after 42
        Assert.Equal("/name", JsonLocator.PointerAtCaret(Doc, At("\"wjson\"", 7)));    // right after the closing quote
        Assert.Equal("/tags/0", JsonLocator.PointerAtCaret(Doc, At("\"a\"")));         // right before "a"
    }

    [Fact]
    public void Text_range_of_a_pointer()
    {
        void Check(string pointer, string expected, string? key)
        {
            var span = JsonLocator.Find(Doc, pointer);
            Assert.NotNull(span);
            Assert.Equal(expected, Doc.Substring(span!.Value.Start, span.Value.Length));
            Assert.Equal(key, span.Value.KeyStart is int k ? Doc.Substring(k, key?.Length ?? 0) : null);
        }
        Check("", Doc, null);
        Check("/name", "\"wjson\"", "\"name\"");
        Check("/tags", "[\"a\", {\"x/y\": 2}]", "\"tags\"");
        Check("/tags/1", "{\"x/y\": 2}", null);
        Check("/tags/1/x~1y", "2", "\"x/y\"");
        Check("/n", "42", "\"n\"");
        Assert.Null(JsonLocator.Find(Doc, "/missing"));
        Assert.Null(JsonLocator.Find(Doc, "/tags/2"));
        Assert.Null(JsonLocator.Find(Doc, "/tags/01"));
        Assert.Null(JsonLocator.Find(Doc, "/n/0"));
        Assert.Null(JsonLocator.Find(Doc, "bad"));
    }

    [Fact]
    public void Escaped_keys_and_duplicates()
    {
        const string doc = "{\"a\\u0062\": 1, \"k\": 1, \"k\": [true]}";
        Assert.Equal("1", Substring(doc, JsonLocator.Find(doc, "/ab")));
        Assert.Equal("[true]", Substring(doc, JsonLocator.Find(doc, "/k")));   // the last duplicate wins
        Assert.Equal("/ab", JsonLocator.PointerAt(doc, 3));
    }

    private static string Substring(string text, TextSpanInfo? span) => text.Substring(span!.Value.Start, span.Value.Length);

    [Fact]
    public void Every_tree_node_is_found_in_the_text_and_back()
    {
        var text = System.IO.File.ReadAllText(SampleDiffTestsPath("sample.json")).Replace("\r\n", "\r").Replace('\n', '\r');
        var root = JsonTree.CreateRoot(WJson.Parse(text));
        var checkedNodes = 0;
        void Walk(JsonTreeNode node)
        {
            if (node.Kind != NodeKind.Range)
            {
                var span = JsonLocator.Find(text, node.Pointer);
                Assert.True(span != null, node.Pointer);
                Assert.True(JsonDiff.DeepEquals(node.Value, WJson.Parse(text.Substring(span!.Value.Start, span.Value.Length))), node.Pointer);
                Assert.Equal(node.Pointer, JsonLocator.PointerAt(text, span.Value.Start));
                ++checkedNodes;
            }
            foreach (var child in node.GetChildren()) Walk(child);
        }
        Walk(root);
        Assert.True(checkedNodes > 2000);
    }

    internal static string SampleDiffTestsPath(string name)
    {
        for (var dir = new System.IO.DirectoryInfo(AppContext.BaseDirectory); dir != null; dir = dir.Parent)
        {
            var candidate = System.IO.Path.Combine(dir.FullName, "gui", "samples", name);
            if (System.IO.File.Exists(candidate)) return candidate;
        }
        throw new System.IO.FileNotFoundException(name);
    }
}

public class HistoryTests
{
    private static readonly DateTime T0 = new DateTime(2026, 1, 1);

    [Fact]
    public void Edit_between_two_texts()
    {
        var e = TextEdit.Between("{\"a\": 1}", "{\"a\": 12}");
        Assert.Equal(7, e.Start);
        Assert.Equal("", e.Removed);
        Assert.Equal("2", e.Inserted);
        Assert.Equal("{\"a\": 1}", e.Inverse().ApplyTo("{\"a\": 12}"));
        Assert.True(TextEdit.Between("same", "same").IsEmpty);
        // A surrogate pair is never split.
        var pair = TextEdit.Between("a\U0001F600b", "a\U0001F601b");
        Assert.Equal(1, pair.Start);
        Assert.Equal("\U0001F600", pair.Removed);

        // Typing "a" at the start of "aa" looks like typing it at the end; the caret tells where it was.
        Assert.Equal(2, TextEdit.Between("aa", "aaa").Start);
        Assert.Equal(0, TextEdit.Between("aa", "aaa", caretAfter: 1).Start);
        Assert.Equal(1, TextEdit.Between("aa", "aaa", caretAfter: 2).Start);
        Assert.Equal(2, TextEdit.Between("aa", "aaa", caretAfter: 0).Start);   // impossible hint: ignored
        Assert.Equal(0, TextEdit.Between("x\rx\r", "x\rx\rx\r", caretAfter: 2).Start);
    }

    [Fact]
    public void Typing_is_one_step_and_redo_is_cleared_by_new_edits()
    {
        var history = new EditHistory();
        var text = "";
        void Type(string s, DateTime when)
        {
            foreach (var c in s)
            {
                var next = text + c;
                history.Record(TextEdit.Between(text, next), when);
                text = next;
            }
        }
        Type("hello", T0);
        Type(" world", T0.AddSeconds(0.5));
        Type("!", T0.AddSeconds(5));   // after a pause: new step

        text = history.Undo()!.ApplyTo(text);
        Assert.Equal("hello world", text);
        text = history.Undo()!.ApplyTo(text);
        Assert.Equal("hello ", text);   // the space stays with the word before it
        text = history.Undo()!.ApplyTo(text);
        Assert.Equal("", text);
        Assert.Null(history.Undo());

        text = history.Redo()!.ApplyTo(text);
        Assert.Equal("hello ", text);
        Assert.True(history.CanRedo);
        history.Record(TextEdit.Between(text, text + "?"), T0.AddSeconds(10));
        Assert.False(history.CanRedo);
    }

    [Fact]
    public void Backspace_and_delete_are_merged()
    {
        var history = new EditHistory();
        var text = "abcdef";
        void Apply(string next)
        {
            history.Record(TextEdit.Between(text, next), T0);
            text = next;
        }
        Apply("abcde");   // backspace x3
        Apply("abcd");
        Apply("abc");
        text = history.Undo()!.ApplyTo(text);
        Assert.Equal("abcdef", text);

        Apply("bcdef");   // delete key x2 at the start
        Apply("cdef");
        text = history.Undo()!.ApplyTo(text);
        Assert.Equal("abcdef", text);
    }

    [Fact]
    public void Large_replacements_are_separate_steps()
    {
        var history = new EditHistory();
        history.Record(TextEdit.Between("{}", "{\"a\": 1}"), T0);
        history.Record(TextEdit.Between("{\"a\": 1}", "{\n  \"a\": 1\n}"), T0);
        Assert.Equal("{\"a\": 1}", history.Undo()!.ApplyTo("{\n  \"a\": 1\n}"));
        Assert.Equal("{}", history.Undo()!.ApplyTo("{\"a\": 1}"));
    }
}

public class AnalyzeFileTests
{
    [Fact]
    public void Reports_stages_and_can_be_cancelled()
    {
        var path = System.IO.Path.GetTempFileName();
        try
        {
            System.IO.File.WriteAllText(path, "{\"a\": [1, 2, {\"b\": null}]}");
            var file = DocumentFile.Load(path);
            var stages = new List<AnalysisStage>();
            var result = DocumentAnalysis.AnalyzeFile(file, jsonc: false, stages.Add);
            Assert.True(result.IsValid);
            Assert.Equal(6, result.ValueCount);
            Assert.Equal(new[] { AnalysisStage.Validating, AnalysisStage.Reading, AnalysisStage.BuildingTree }, stages);

            using var cancel = new System.Threading.CancellationTokenSource();
            cancel.Cancel();
            Assert.Throws<OperationCanceledException>(() => DocumentAnalysis.AnalyzeFile(file, jsonc: false, null, cancel.Token));

            System.IO.File.WriteAllText(path, "[1,");
            stages.Clear();
            Assert.False(DocumentAnalysis.AnalyzeFile(DocumentFile.Load(path), jsonc: false, stages.Add).IsValid);
            Assert.Equal(new[] { AnalysisStage.Validating }, stages);
        }
        finally
        {
            System.IO.File.Delete(path);
        }
    }
}
