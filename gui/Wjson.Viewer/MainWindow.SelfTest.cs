// Scripted checks of the editor, run by CI with --self-test on gui/samples/sample.json:
// colors, typing, undo/redo, Format, and the link between the text and the tree.
// SPDX-License-Identifier: MIT
using System;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using Microsoft.UI.Text;
using Microsoft.UI.Xaml;
using Wjson.Viewer.Core;

namespace Wjson.Viewer
{
    public sealed partial class MainWindow
    {
        /// <summary>Runs the checks, writes "PASS name" / "FAIL name: detail" lines to a file and returns the failures.</summary>
        public async Task<int> RunSelfTestAsync(string logPath)
        {
            var log = new StringBuilder();
            var failures = 0;
            void Check(string name, bool ok, string detail = "")
            {
                log.Append(ok ? "PASS " : "FAIL ").Append(name);
                if (!ok && detail.Length > 0) log.Append(": ").Append(detail);
                log.AppendLine();
                if (!ok) ++failures;
            }
            // Lets queued events (TextChanged, SelectionChanged) and timers run.
            static async Task Settle()
            {
                for (var i = 0; i < 6; ++i) await Task.Delay(100);
            }
            Rgb ColorAt(int index)
            {
                var c = Doc.GetRange(index, index + 1).CharacterFormat.ForegroundColor;
                return new Rgb(c.R, c.G, c.B);
            }
            int At(string what, int skip = 0)
            {
                var i = _text.IndexOf(what, StringComparison.Ordinal);
                if (i < 0) throw new InvalidOperationException("not found in the text: " + what);
                return i + skip;
            }
            void CheckColor(string name, int index, Rgb expected)
            {
                var actual = ColorAt(index);
                Check(name, actual.Equals(expected), $"{actual} at {index}, expected {expected}");
            }

            try
            {
                await Settle();
                var dark = IsDark;
                var original = _text;
                Check("text loaded unchanged", original.Length > 1000 && ReadEditorText() == original && !_dirty,
                      $"{original.Length} chars, read back {ReadEditorText().Length}");
                Check("loaded as RTF", !_rtfFailed);
                Check("valid", _result?.IsValid == true && _resultText == _text);

                CheckColor("key color", At("\"proyecto\"", 1), SyntaxColors.For(SyntaxKind.Key, dark));
                CheckColor("string color", At("\"wjson Viewer\"", 1), SyntaxColors.For(SyntaxKind.String, dark));
                CheckColor("number color", At("33059.12"), SyntaxColors.For(SyntaxKind.Number, dark));
                CheckColor("literal color", At("true"), SyntaxColors.For(SyntaxKind.Literal, dark));
                CheckColor("punctuation color", At(": \"wjson"), SyntaxColors.Default(dark));

                // Tree -> text
                NavigateTo("/tiendas/1/ciudad");
                Check("tree selects the text", SelectedEditorText() == "\"Sevilla\"", SelectedEditorText());

                // Text -> tree
                Editor.Focus(FocusState.Programmatic);
                var linus = At("\"Linus\"", 3);
                Doc.Selection.SetRange(linus, linus);
                await SyncTreeToCaretAsync();
                Check("caret selects the tree node", _selected?.Pointer == "/autores/1/nombre" &&
                      Tree.SelectedNode?.Content is NodeItem { Node.Pointer: "/autores/1/nombre" }, _selected?.Pointer ?? "none");

                // Typing over a selection
                var version = At("1.1.0");
                Doc.Selection.SetRange(version, version + 5);
                Doc.Selection.TypeText("2.0.0");
                await Settle();
                Check("typing", _text.Contains("\"2.0.0\"") && _dirty && Title.StartsWith("●"), Title);
                CheckColor("typed text keeps the string color", version + 2, SyntaxColors.For(SyntaxKind.String, dark));

                // A new member typed character by character after "{"
                Doc.Selection.SetRange(1, 1);
                foreach (var c in "\"nuevo\": 7, ") Doc.Selection.TypeText(c.ToString());
                await Settle();
                Check("typed member", _text.StartsWith("{\"nuevo\": 7, \r", StringComparison.Ordinal), _text.Substring(0, 20));
                CheckColor("typed key color", 2, SyntaxColors.For(SyntaxKind.Key, dark));
                CheckColor("typed number color", 10, SyntaxColors.For(SyntaxKind.Number, dark));
                CheckColor("typed comma color", 11, SyntaxColors.Default(dark));

                // Undo everything, redo everything, undo again
                var steps = 0;
                while (_history.CanUndo && steps++ < 50) Undo();
                await Settle();
                Check("undo restores the text", _text == original && ReadEditorText() == original && !_dirty, $"{steps} steps");
                CheckColor("undo restores the colors", version + 2, SyntaxColors.For(SyntaxKind.String, dark));
                CheckColor("undo restores the key color", At("\"proyecto\"", 1), SyntaxColors.For(SyntaxKind.Key, dark));
                while (_history.CanRedo) Redo();
                await Settle();
                Check("redo", _text.StartsWith("{\"nuevo\": 7, ", StringComparison.Ordinal) && _text.Contains("\"2.0.0\""));
                while (_history.CanUndo) Undo();
                await Settle();
                Check("undo again", _text == original);

                // Minify is one undo step
                await ValidateAsync();
                Reformat(indent: 0);
                await Settle();
                Check("minify", !_text.Contains('\r') && _text.Length < original.Length);
                CheckColor("minified colors", 2, SyntaxColors.For(SyntaxKind.Key, dark));
                Undo();
                await Settle();
                Check("undo minify", _text == original && !_dirty);

                // The tree follows the text after an edit
                await ValidateAsync();
                Check("tree matches the text", TreeMatchesText);
            }
            catch (Exception e)
            {
                Check("no exception", false, e.ToString());
            }
            File.WriteAllText(logPath, log.ToString());
            return failures;
        }
    }
}
