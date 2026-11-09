// The editor of wjson Viewer: syntax colors, undo/redo, paste, and the link
// between the text and the tree. The RichEditBox stores line breaks as "\r";
// DocumentFile.Save restores the file's own line breaks.
// SPDX-License-Identifier: MIT
using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.UI;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Text;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Windows.ApplicationModel.DataTransfer;
using Wjson.Viewer.Core;

namespace Wjson.Viewer
{
    public sealed partial class MainWindow
    {
        /// <summary>Above this many spans, a repaint reloads the whole text as RTF (one call) instead.</summary>
        private const int MaxSpansPerRepaint = 4000;

        private const string EditorFontName = "Cascadia Mono";

        private RichEditTextDocument Doc => Editor.Document;

        private void InitializeEditor()
        {
            try
            {
                // Coloring changes the formatting, which the control would record
                // as undo steps of its own: the window keeps its own history.
                Doc.UndoLimit = 0;
            }
            catch (Exception e)
            {
                App.Log(e);
            }

            // Does the control report an extra paragraph mark at the end of its text?
            Doc.SetText(TextSetOptions.None, "x");
            Doc.GetText(TextGetOptions.None, out var probe);
            _trailingBreak = probe == "x\r";
            Doc.SetText(TextSetOptions.None, "");
            UpdateSelectionBrush();
        }

        private void UpdateSelectionBrush()
        {
            try
            {
                // Keep the selection visible while the tree has the focus.
                Editor.SelectionHighlightColorWhenNotFocused = new SolidColorBrush(
                    IsDark ? ColorHelper.FromArgb(255, 0x26, 0x4F, 0x78) : ColorHelper.FromArgb(255, 0xAD, 0xD6, 0xFF));
            }
            catch (Exception e)
            {
                App.Log(e);
            }
        }

        private static string ToEditorText(string text) => text.Replace("\r\n", "\r").Replace('\n', '\r');

        private static Windows.UI.Color ToColor(Rgb c) => ColorHelper.FromArgb(255, c.R, c.G, c.B);

        private double EditorFontPoints => Editor.FontSize * 0.75;

        private string ReadEditorText()
        {
            Doc.GetText(TextGetOptions.None, out var text);
            if (_trailingBreak && text.EndsWith('\r')) text = text.Substring(0, text.Length - 1);
            return text;
        }

        private string SelectedEditorText()
        {
            Doc.Selection.GetText(TextGetOptions.None, out var text);
            return text;
        }

        // ------------------------------------------------------------------
        // Loading and coloring
        // ------------------------------------------------------------------

        /// <summary>Replaces the whole text (not recorded in the history) and colors it.</summary>
        private void LoadEditorText(string text)
        {
            text = ToEditorText(text);
            ++_ignoreSelection;
            var readOnly = Editor.IsReadOnly;
            Editor.IsReadOnly = false; // a read-only RichEditBox ignores SetText
            try
            {
                _text = text;
                _highlight = text.Length <= JsonSyntax.HighlightLimit;
                _spans = _highlight ? JsonSyntax.Tokenize(text) : new List<SyntaxSpan>();
                if (_highlight && !_rtfFailed)
                {
                    // One call loads and colors the whole document.
                    Doc.SetText(TextSetOptions.FormatRtf, JsonSyntax.ToRtf(text, _spans, IsDark, EditorFontName, EditorFontPoints));
                    if (ReadEditorText() == text) return;
                    _rtfFailed = true;
                    App.Log(new InvalidOperationException("Loading RTF changed the text; coloring with formatting calls instead."));
                }
                Doc.SetText(TextSetOptions.None, text);
                Paint(0, text.Length);
            }
            finally
            {
                Editor.IsReadOnly = readOnly;
                DispatcherQueue.TryEnqueue(DispatcherQueuePriority.Low, () => --_ignoreSelection);
            }
        }

        /// <summary>Loads the current text again (new theme, or too many colors to repaint), keeping the selection.</summary>
        private void ReloadEditorColors()
        {
            UpdateSelectionBrush();
            if (_largeFile != null) return;
            var start = Doc.Selection.StartPosition;
            var end = Doc.Selection.EndPosition;
            LoadEditorText(_text);
            SelectInEditor(start, end);
        }

        /// <summary>Paints [start, end) with the colors of <see cref="_spans"/>.</summary>
        private void Paint(int start, int end)
        {
            if (end <= start) return;
            var dark = IsDark;
            Doc.BatchDisplayUpdates();
            try
            {
                Doc.GetRange(start, end).CharacterFormat.ForegroundColor = ToColor(SyntaxColors.Default(dark));
                if (!_highlight) return;
                for (var i = FirstSpanEndingAfter(start); i < _spans.Count && _spans[i].Start < end; ++i)
                {
                    var span = _spans[i];
                    Doc.GetRange(Math.Max(span.Start, start), Math.Min(span.End, end)).CharacterFormat.ForegroundColor =
                        ToColor(SyntaxColors.For(span.Kind, dark));
                }
            }
            finally
            {
                Doc.ApplyDisplayUpdates();
            }
        }

        private int FirstSpanEndingAfter(int position)
        {
            int lo = 0, hi = _spans.Count;
            while (lo < hi)
            {
                var mid = (lo + hi) / 2;
                if (_spans[mid].End <= position) lo = mid + 1;
                else hi = mid;
            }
            return lo;
        }

        private static int LineStart(string text, int position)
        {
            if (position <= 0) return 0;
            var i = text.LastIndexOf('\r', Math.Min(position, text.Length) - 1);
            return i + 1;
        }

        private static int LineEnd(string text, int position)
        {
            if (position >= text.Length) return text.Length;
            var i = text.IndexOf('\r', position);
            return i < 0 ? text.Length : i;
        }

        // ------------------------------------------------------------------
        // Edits
        // ------------------------------------------------------------------

        private void Editor_TextChanged(object sender, RoutedEventArgs e)
        {
            try
            {
                ProcessTextChange();
            }
            catch (Exception ex)
            {
                App.Log(ex);
            }
        }

        /// <summary>Records and colors whatever changed since the last call.</summary>
        private void ProcessTextChange()
        {
            if (_largeFile != null) return;
            var text = ReadEditorText();
            if (text == _text) return; // only the formatting changed (our own coloring)
            var caret = Doc.Selection.EndPosition;
            var edit = TextEdit.Between(_text, text, caret);
            if (!_applyingHistory) _history.Record(edit, DateTime.UtcNow);
            _text = text;
            UpdateColors(edit, caret);
            _pasteLength = 0;
            AfterTextChanged();
        }

        private void AfterTextChanged()
        {
            var dirty = _text != _cleanText;
            if (dirty != _dirty)
            {
                _dirty = dirty;
                UpdateTitle();
            }
            _validateTimer.Stop();
            _validateTimer.Start();
        }

        private void UpdateColors(TextEdit edit, int caret)
        {
            var wasHighlighted = _highlight;
            _highlight = _text.Length <= JsonSyntax.HighlightLimit;
            if (!_highlight)
            {
                if (wasHighlighted)
                {
                    _spans = new List<SyntaxSpan>();
                    Paint(0, _text.Length);
                }
                return;
            }
            var spans = JsonSyntax.Tokenize(_text);
            if (!wasHighlighted)
            {
                _spans = spans;
                DispatcherQueue.TryEnqueue(ReloadEditorColors);
                return;
            }

            var range = JsonSyntax.RepaintRange(_spans, spans, edit);
            int start = range?.Start ?? int.MaxValue, end = range?.End ?? int.MinValue;
            // Typed or pasted text ends at the caret; the diff may have placed it elsewhere ("a" typed next to "a").
            var typed = Math.Max(edit.Inserted.Length, _pasteLength);
            if (typed > 0)
            {
                start = Math.Min(start, Math.Max(0, caret - typed));
                end = Math.Max(end, Math.Min(_text.Length, caret));
            }
            _spans = spans;
            if (start >= end) return;
            // Whole lines: cheap, and covers any remaining doubt about where the edit was.
            start = LineStart(_text, start);
            end = LineEnd(_text, end);
            if (FirstSpanEndingAfter(end) - FirstSpanEndingAfter(start) > MaxSpansPerRepaint)
                DispatcherQueue.TryEnqueue(ReloadEditorColors);
            else
                Paint(start, end);
        }

        /// <summary>Replaces the whole text as one undo step (Format, Minify).</summary>
        private void ReplaceEditorText(string text)
        {
            text = ToEditorText(text);
            var edit = TextEdit.Between(_text, text);
            if (edit.IsEmpty) return;
            _history.Record(edit, DateTime.UtcNow, canMerge: false);
            LoadEditorText(text);
            AfterTextChanged();
        }

        private void Undo() => ApplyHistory(_history.Undo());

        private void Redo() => ApplyHistory(_history.Redo());

        private void ApplyHistory(TextEdit? edit)
        {
            if (edit == null || Editor.IsReadOnly || _largeFile != null) return;
            _applyingHistory = true;
            try
            {
                var caret = edit.Start + edit.Inserted.Length;
                if (edit.Removed.Length + edit.Inserted.Length > 200_000)
                {
                    LoadEditorText(edit.ApplyTo(_text));
                    AfterTextChanged();
                }
                else
                {
                    Doc.GetRange(edit.Start, edit.Start + edit.Removed.Length).SetText(TextSetOptions.None, edit.Inserted);
                    Doc.Selection.SetRange(caret, caret);
                    ProcessTextChange();
                }
                SelectInEditor(caret, caret);
            }
            finally
            {
                _applyingHistory = false;
            }
        }

        private void Editor_PreviewKeyDown(object sender, KeyRoutedEventArgs e)
        {
            static bool Down(Windows.System.VirtualKey key) =>
                Microsoft.UI.Input.InputKeyboardSource.GetKeyStateForCurrentThread(key).HasFlag(Windows.UI.Core.CoreVirtualKeyStates.Down);
            if (!Down(Windows.System.VirtualKey.Control) || Down(Windows.System.VirtualKey.Menu)) return;
            if (e.Key == Windows.System.VirtualKey.Z)
            {
                e.Handled = true;
                if (Down(Windows.System.VirtualKey.Shift)) Redo();
                else Undo();
            }
            else if (e.Key == Windows.System.VirtualKey.Y)
            {
                e.Handled = true;
                Redo();
            }
        }

        /// <summary>Pastes plain text only (no fonts or colors from other apps).</summary>
        private async void Editor_Paste(object sender, TextControlPasteEventArgs e)
        {
            e.Handled = true;
            if (Editor.IsReadOnly) return;
            try
            {
                var content = Clipboard.GetContent();
                if (!content.Contains(StandardDataFormats.Text)) return;
                var text = ToEditorText(await content.GetTextAsync());
                var start = Doc.Selection.StartPosition;
                _pasteLength = text.Length;
                Doc.Selection.SetText(TextSetOptions.None, text);
                Doc.Selection.SetRange(start + text.Length, start + text.Length);
                ProcessTextChange();
            }
            catch (Exception ex)
            {
                App.Log(ex);
            }
        }

        // ------------------------------------------------------------------
        // Text <-> tree
        // ------------------------------------------------------------------

        /// <summary>True when the tree describes exactly the text in the editor.</summary>
        private bool TreeMatchesText => _largeFile == null && _result?.IsValid == true && _result.Root != null && _resultText == _text;

        private void Editor_SelectionChanged(object sender, RoutedEventArgs e)
        {
            if (_ignoreSelection > 0 || _largeFile != null || Editor.FocusState == FocusState.Unfocused) return;
            _caretTimer.Stop();
            _caretTimer.Start();
        }

        /// <summary>Selects in the tree the value under the caret.</summary>
        private async Task SyncTreeToCaretAsync()
        {
            _caretTimer.Stop();
            if (!TreeMatchesText) return;
            var text = _text;
            var caret = Doc.Selection.StartPosition;
            var pointer = text.Length > 200_000
                ? await Task.Run(() => JsonLocator.PointerAtCaret(text, caret))
                : JsonLocator.PointerAtCaret(text, caret);
            if (pointer == null || text != _text) return;
            if (_selected != null && _selected.Kind != NodeKind.Range && _selected.Pointer == pointer) return;
            NavigateTo(pointer, fromEditor: true);
        }

        /// <summary>Selects in the editor the text of the value at a pointer.</summary>
        private void SelectInEditor(string pointer)
        {
            if (!TreeMatchesText) return;
            if (JsonLocator.Find(_text, pointer) is { } span) SelectInEditor(span.Start, span.End);
        }

        private void SelectInEditor(int start, int end)
        {
            ++_ignoreSelection;
            try
            {
                Doc.Selection.SetRange(start, end);
                Doc.Selection.ScrollIntoView(PointOptions.Start);
            }
            catch (Exception e)
            {
                App.Log(e);
            }
            finally
            {
                DispatcherQueue.TryEnqueue(DispatcherQueuePriority.Low, () => --_ignoreSelection);
            }
        }

        // ------------------------------------------------------------------
        // Progress of long operations
        // ------------------------------------------------------------------

        /// <summary>Starts a cancellable operation; the panel appears if it takes more than half a second.</summary>
        private CancellationToken BeginBusy(string text)
        {
            _busy?.Cancel();
            _busy = new CancellationTokenSource();
            _busyStart = DateTime.UtcNow;
            BusyText.Text = text;
            BusyElapsed.Text = "";
            _busyTimer.Start();
            return _busy.Token;
        }

        private void EndBusy(CancellationToken token)
        {
            if (_busy == null || _busy.Token != token) return; // a newer operation owns the panel
            _busy.Dispose();
            _busy = null;
            _busyTimer.Stop();
            BusyPanel.Visibility = Visibility.Collapsed;
        }

        private void UpdateBusyElapsed()
        {
            if (_busy == null) return;
            var seconds = (DateTime.UtcNow - _busyStart).TotalSeconds;
            BusyPanel.Visibility = Visibility.Visible;
            BusyElapsed.Text = seconds >= 1 ? Strings.Format("Elapsed", Math.Floor(seconds)) : "";
        }

        private void BusyCancel_Click(object sender, RoutedEventArgs e) => _busy?.Cancel();
    }
}
