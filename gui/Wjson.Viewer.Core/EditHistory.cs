// Text edits and the editor's undo/redo history. The editor keeps its own
// history because coloring the text changes its formatting, which the
// control's built-in undo would record as separate steps.
// SPDX-License-Identifier: MIT
using System;
using System.Collections.Generic;

namespace Wjson.Viewer.Core
{
    /// <summary>Replacement of <see cref="Removed"/> at <see cref="Start"/> by <see cref="Inserted"/>.</summary>
    public sealed class TextEdit
    {
        public TextEdit(int start, string removed, string inserted)
        {
            Start = start;
            Removed = removed;
            Inserted = inserted;
        }

        public int Start { get; }
        public string Removed { get; }
        public string Inserted { get; }
        public bool IsEmpty => Removed.Length == 0 && Inserted.Length == 0;

        /// <summary>The edit that undoes this one.</summary>
        public TextEdit Inverse() => new TextEdit(Start, Inserted, Removed);

        public string ApplyTo(string text) => text.Substring(0, Start) + Inserted + text.Substring(Start + Removed.Length);

        /// <summary>The single replacement that turns <paramref name="before"/> into <paramref name="after"/> (common prefix and suffix kept).</summary>
        public static TextEdit Between(string before, string after)
        {
            var max = Math.Min(before.Length, after.Length);
            var prefix = 0;
            while (prefix < max && before[prefix] == after[prefix]) ++prefix;
            var suffix = 0;
            while (suffix < max - prefix && before[before.Length - 1 - suffix] == after[after.Length - 1 - suffix]) ++suffix;
            // Do not split a surrogate pair.
            if (prefix > 0 && char.IsHighSurrogate(before[prefix - 1])) --prefix;
            if (suffix > 0 && char.IsLowSurrogate(before[before.Length - suffix])) --suffix;
            return new TextEdit(prefix, before.Substring(prefix, before.Length - prefix - suffix),
                                after.Substring(prefix, after.Length - prefix - suffix));
        }

        /// <summary>
        /// Like <see cref="Between(string, string)"/>, but for an insertion
        /// that could have happened at several places (typing "a" next to
        /// another "a"), picks the one that ends at the caret.
        /// </summary>
        public static TextEdit Between(string before, string after, int caretAfter)
        {
            var edit = Between(before, after);
            var length = after.Length - before.Length;
            if (edit.Removed.Length != 0 || length <= 0) return edit;
            var start = caretAfter - length;
            if (start == edit.Start || start < 0 || start > before.Length) return edit;
            if (string.CompareOrdinal(before, 0, after, 0, start) != 0) return edit;
            if (string.CompareOrdinal(before, start, after, start + length, before.Length - start) != 0) return edit;
            return new TextEdit(start, "", after.Substring(start, length));
        }

        public override string ToString() => $"@{Start} -\"{Removed}\" +\"{Inserted}\"";
    }

    /// <summary>Undo/redo stack. Consecutive typing (or deleting) is merged into one step.</summary>
    public sealed class EditHistory
    {
        /// <summary>Typing paused for longer than this starts a new undo step.</summary>
        public static readonly TimeSpan MergeWindow = TimeSpan.FromSeconds(1.5);
        public const int MaxSteps = 500;
        /// <summary>Total characters kept (removed + inserted) before the oldest steps are dropped.</summary>
        public const long MaxCharacters = 32L * 1024 * 1024;

        private readonly List<TextEdit> _undo = new List<TextEdit>();
        private readonly List<TextEdit> _redo = new List<TextEdit>();
        private DateTime _lastTime;
        private bool _canMerge;
        private long _characters;

        public bool CanUndo => _undo.Count > 0;
        public bool CanRedo => _redo.Count > 0;

        public void Clear()
        {
            _undo.Clear();
            _redo.Clear();
            _characters = 0;
            _canMerge = false;
        }

        /// <summary>
        /// Records an edit made by the user. With <paramref name="canMerge"/>
        /// false (e.g. Format), the edit is always a step of its own.
        /// </summary>
        public void Record(TextEdit edit, DateTime now, bool canMerge = true)
        {
            if (edit.IsEmpty) return;
            _redo.Clear();
            if (!canMerge)
            {
                Push(edit);
                _canMerge = false;
                return;
            }
            if (_canMerge && _undo.Count > 0 && now - _lastTime <= MergeWindow && TryMerge(_undo[_undo.Count - 1], edit, out var merged))
            {
                _characters += edit.Removed.Length + edit.Inserted.Length;
                _undo[_undo.Count - 1] = merged;
            }
            else
            {
                Push(edit);
            }
            _lastTime = now;
            _canMerge = IsTyping(edit) || IsDeleting(edit);
        }

        /// <summary>The edit to apply to undo the last step, or null.</summary>
        public TextEdit? Undo()
        {
            if (_undo.Count == 0) return null;
            var edit = _undo[_undo.Count - 1];
            _undo.RemoveAt(_undo.Count - 1);
            _redo.Add(edit);
            _canMerge = false;
            return edit.Inverse();
        }

        /// <summary>The edit to apply to redo the last undone step, or null.</summary>
        public TextEdit? Redo()
        {
            if (_redo.Count == 0) return null;
            var edit = _redo[_redo.Count - 1];
            _redo.RemoveAt(_redo.Count - 1);
            _undo.Add(edit);
            _canMerge = false;
            return edit;
        }

        private void Push(TextEdit edit)
        {
            _undo.Add(edit);
            _characters += edit.Removed.Length + edit.Inserted.Length;
            while (_undo.Count > 1 && (_undo.Count > MaxSteps || _characters > MaxCharacters))
            {
                _characters -= _undo[0].Removed.Length + _undo[0].Inserted.Length;
                _undo.RemoveAt(0);
            }
        }

        private static bool IsTyping(TextEdit e) => e.Removed.Length == 0 && e.Inserted.Length == 1 && e.Inserted != "\r" && e.Inserted != "\n";
        private static bool IsDeleting(TextEdit e) => e.Inserted.Length == 0 && e.Removed.Length == 1;

        private static bool TryMerge(TextEdit last, TextEdit next, out TextEdit merged)
        {
            merged = last;
            // Typing: "ab" + "c" right after it. A space ends a word: the next word is a new step.
            if (IsTyping(next) && last.Removed.Length == 0 && next.Start == last.Start + last.Inserted.Length &&
                !(last.Inserted.EndsWith(" ", StringComparison.Ordinal) && next.Inserted != " "))
            {
                merged = new TextEdit(last.Start, "", last.Inserted + next.Inserted);
                return true;
            }
            if (IsDeleting(next) && last.Inserted.Length == 0)
            {
                if (next.Start + 1 == last.Start) // backspace
                {
                    merged = new TextEdit(next.Start, next.Removed + last.Removed, "");
                    return true;
                }
                if (next.Start == last.Start) // delete key
                {
                    merged = new TextEdit(last.Start, last.Removed + next.Removed, "");
                    return true;
                }
            }
            return false;
        }
    }
}
