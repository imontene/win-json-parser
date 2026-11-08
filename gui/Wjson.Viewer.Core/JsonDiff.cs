// Structural comparison of two parsed JSON documents.
// SPDX-License-Identifier: MIT
using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using Wjson;

namespace Wjson.Viewer.Core
{
    public enum DiffKind { Added, Removed, Changed }

    /// <summary>One difference between the left (current) and the right (other) document.</summary>
    public sealed class DiffEntry
    {
        internal DiffEntry(DiffKind kind, string? leftPointer, string? rightPointer, object? left, object? right)
        {
            Kind = kind;
            LeftPointer = leftPointer;
            RightPointer = rightPointer;
            Left = left;
            Right = right;
        }

        public DiffKind Kind { get; }
        /// <summary>Where the value is in the left document (null for Added).</summary>
        public string? LeftPointer { get; }
        /// <summary>Where the value is in the right document (null for Removed).</summary>
        public string? RightPointer { get; }
        public object? Left { get; }
        public object? Right { get; }

        /// <summary>The pointer to show: the left one when there is one.</summary>
        public string Pointer => LeftPointer ?? RightPointer ?? "";

        public override string ToString() => Kind switch
        {
            DiffKind.Added => $"+ {Pointer}: {JsonDiff.Preview(Right)}",
            DiffKind.Removed => $"- {Pointer}: {JsonDiff.Preview(Left)}",
            _ => $"~ {Pointer}: {JsonDiff.Preview(Left)} -> {JsonDiff.Preview(Right)}",
        };
    }

    public sealed class DiffResult
    {
        internal DiffResult(List<DiffEntry> entries, bool truncated)
        {
            Entries = entries;
            Truncated = truncated;
        }

        public IReadOnlyList<DiffEntry> Entries { get; }
        /// <summary>True if the comparison stopped at JsonDiff.MaxEntries differences.</summary>
        public bool Truncated { get; }
        public bool AreEqual => Entries.Count == 0;

        public int Count(DiffKind kind)
        {
            var n = 0;
            foreach (var e in Entries)
                if (e.Kind == kind) ++n;
            return n;
        }
    }

    /// <summary>
    /// Compares two documents: objects member by member (order does not
    /// matter), arrays element by element aligned with a longest common
    /// subsequence so that an insertion in the middle is reported once, and
    /// numbers by value (1 equals 1.0).
    /// </summary>
    public static class JsonDiff
    {
        /// <summary>The comparison stops after this many differences.</summary>
        public const int MaxEntries = 10_000;

        /// <summary>Arrays whose lengths multiply to more than this are compared by position.</summary>
        public const long MaxAlignmentCells = 4_000_000;

        public static DiffResult Compare(object? left, object? right)
        {
            var differ = new Differ();
            differ.Compare(left, right, "", "");
            return new DiffResult(differ.Entries, differ.Truncated);
        }

        /// <summary>Deep equality with JSON semantics (numbers by value, objects ignoring member order).</summary>
        public static bool DeepEquals(object? a, object? b)
        {
            if (IsNumber(a) && IsNumber(b)) return NumbersEqual(a!, b!);
            switch (a)
            {
                case null:
                    return b == null;
                case bool boolA:
                    return b is bool boolB && boolA == boolB;
                case string textA:
                    return b is string textB && string.Equals(textA, textB, StringComparison.Ordinal);
                case JsonArray arrayA:
                {
                    if (b is not JsonArray arrayB || arrayA.Count != arrayB.Count) return false;
                    for (var i = 0; i < arrayA.Count; ++i)
                        if (!DeepEquals(arrayA[i], arrayB[i])) return false;
                    return true;
                }
                case JsonObject objectA:
                {
                    if (b is not JsonObject objectB || objectA.Count != objectB.Count) return false;
                    foreach (DictionaryEntry entry in objectA)
                        if (!objectB.TryGetValue((string)entry.Key, out var other) || !DeepEquals(entry.Value, other)) return false;
                    return true;
                }
                default:
                    return false;
            }
        }

        /// <summary>Short one-line JSON preview of a value.</summary>
        public static string Preview(object? value, int maxLength = 80)
        {
            var text = JsonWriter.Write(value, 0);
            return text.Length <= maxLength ? text : text.Substring(0, maxLength - 1) + "…";
        }

        private static bool IsNumber(object? v) => v is long || v is double;

        /// <summary>
        /// A string that is equal for two values exactly when DeepEquals is true:
        /// object members sorted by key, and integral numbers written the same
        /// whether they were parsed as long or double.
        /// </summary>
        internal static string Canonical(object? value)
        {
            var sb = new StringBuilder();
            WriteCanonical(sb, value);
            return sb.ToString();
        }

        private static void WriteCanonical(StringBuilder sb, object? value)
        {
            switch (value)
            {
                case JsonObject obj:
                    sb.Append('{');
                    foreach (var key in obj.Keys.Cast<string>().OrderBy(k => k, StringComparer.Ordinal))
                    {
                        sb.Append(JsonWriter.Write(key, 0)).Append(':');
                        WriteCanonical(sb, obj[key]);
                        sb.Append(',');
                    }
                    sb.Append('}');
                    break;
                case JsonArray array:
                    sb.Append('[');
                    foreach (var item in array)
                    {
                        WriteCanonical(sb, item);
                        sb.Append(',');
                    }
                    sb.Append(']');
                    break;
                case double d when Math.Floor(d) == d && Math.Abs(d) < 9.2e18:
                    sb.Append(((long)d).ToString(CultureInfo.InvariantCulture));
                    break;
                default:
                    sb.Append(JsonWriter.Write(value, 0));
                    break;
            }
        }

        // Exact: an integer equals a double only if the double is that integer
        // (no rounding through double above 2^53).
        private static bool NumbersEqual(object a, object b)
        {
            if (a is long x && b is long y) return x == y;
            if (a is double p && b is double q) return p == q;
            var l = a is long la ? la : (long)b;
            var d = a is double da ? da : (double)b;
            return Math.Floor(d) == d && Math.Abs(d) < 9.2e18 && (long)d == l;
        }

        private sealed class Differ
        {
            public readonly List<DiffEntry> Entries = new List<DiffEntry>();
            public bool Truncated;

            private bool Full => Entries.Count >= MaxEntries;

            private void Add(DiffEntry entry)
            {
                if (Full)
                {
                    Truncated = true;
                    return;
                }
                Entries.Add(entry);
            }

            public void Compare(object? left, object? right, string leftPath, string rightPath)
            {
                if (Full)
                {
                    Truncated = true;
                    return;
                }
                if (left is JsonObject lo && right is JsonObject ro)
                {
                    CompareObjects(lo, ro, leftPath, rightPath);
                    return;
                }
                if (left is JsonArray la && right is JsonArray ra)
                {
                    CompareArrays(la, ra, leftPath, rightPath);
                    return;
                }
                if (!DeepEquals(left, right)) Add(new DiffEntry(DiffKind.Changed, leftPath, rightPath, left, right));
            }

            private void CompareObjects(JsonObject left, JsonObject right, string leftPath, string rightPath)
            {
                foreach (DictionaryEntry entry in left)
                {
                    var key = (string)entry.Key;
                    var token = "/" + JsonTree.EscapeToken(key);
                    if (right.TryGetValue(key, out var other)) Compare(entry.Value, other, leftPath + token, rightPath + token);
                    else Add(new DiffEntry(DiffKind.Removed, leftPath + token, null, entry.Value, null));
                }
                foreach (DictionaryEntry entry in right)
                {
                    var key = (string)entry.Key;
                    if (!left.Contains(key))
                        Add(new DiffEntry(DiffKind.Added, null, rightPath + "/" + JsonTree.EscapeToken(key), null, entry.Value));
                }
            }

            private void CompareArrays(JsonArray left, JsonArray right, string leftPath, string rightPath)
            {
                // Skip the common prefix and suffix: the usual case (a few edits) stays cheap.
                var start = 0;
                while (start < left.Count && start < right.Count && DeepEquals(left[start], right[start])) ++start;
                var leftEnd = left.Count;
                var rightEnd = right.Count;
                while (leftEnd > start && rightEnd > start && DeepEquals(left[leftEnd - 1], right[rightEnd - 1]))
                {
                    --leftEnd;
                    --rightEnd;
                }

                var n = leftEnd - start;
                var m = rightEnd - start;
                if ((long)n * m > MaxAlignmentCells)
                {
                    ComparePositionally(left, right, start, leftEnd, rightEnd, leftPath, rightPath);
                    return;
                }

                // Give equal elements the same id (via a canonical form), so the
                // alignment compares integers instead of whole subtrees.
                var ids = new Dictionary<string, int>(StringComparer.Ordinal);
                int Id(object? v)
                {
                    var key = Canonical(v);
                    if (!ids.TryGetValue(key, out var id)) ids[key] = id = ids.Count;
                    return id;
                }
                var leftIds = new int[n];
                var rightIds = new int[m];
                for (var i = 0; i < n; ++i) leftIds[i] = Id(left[start + i]);
                for (var j = 0; j < m; ++j) rightIds[j] = Id(right[start + j]);

                // Longest common subsequence of the middle part.
                var lcs = new int[n + 1, m + 1];
                for (var i = n - 1; i >= 0; --i)
                    for (var j = m - 1; j >= 0; --j)
                        lcs[i, j] = leftIds[i] == rightIds[j] ? lcs[i + 1, j + 1] + 1 : Math.Max(lcs[i + 1, j], lcs[i, j + 1]);

                // Walk the alignment. Between two matches, removed and added
                // elements are paired up as changes (and compared in depth).
                var removed = new List<int>();
                var added = new List<int>();
                int a = 0, b = 0;
                while (a < n || b < m)
                {
                    if (a < n && b < m && leftIds[a] == rightIds[b])
                    {
                        Flush(left, right, removed, added, leftPath, rightPath);
                        ++a;
                        ++b;
                    }
                    else if (b < m && (a == n || lcs[a, b + 1] >= lcs[a + 1, b]))
                    {
                        added.Add(start + b++);
                    }
                    else
                    {
                        removed.Add(start + a++);
                    }
                }
                Flush(left, right, removed, added, leftPath, rightPath);
            }

            private void Flush(JsonArray left, JsonArray right, List<int> removed, List<int> added, string leftPath, string rightPath)
            {
                var pairs = Math.Min(removed.Count, added.Count);
                for (var k = 0; k < pairs; ++k)
                    Compare(left[removed[k]], right[added[k]], Index(leftPath, removed[k]), Index(rightPath, added[k]));
                for (var k = pairs; k < removed.Count; ++k)
                    Add(new DiffEntry(DiffKind.Removed, Index(leftPath, removed[k]), null, left[removed[k]], null));
                for (var k = pairs; k < added.Count; ++k)
                    Add(new DiffEntry(DiffKind.Added, null, Index(rightPath, added[k]), null, right[added[k]]));
                removed.Clear();
                added.Clear();
            }

            private void ComparePositionally(JsonArray left, JsonArray right, int start, int leftEnd, int rightEnd,
                                             string leftPath, string rightPath)
            {
                var common = Math.Min(leftEnd, rightEnd);
                for (var i = start; i < common && !Full; ++i)
                    Compare(left[i], right[i], Index(leftPath, i), Index(rightPath, i));
                for (var i = common; i < leftEnd; ++i)
                    Add(new DiffEntry(DiffKind.Removed, Index(leftPath, i), null, left[i], null));
                for (var i = common; i < rightEnd; ++i)
                    Add(new DiffEntry(DiffKind.Added, null, Index(rightPath, i), null, right[i]));
            }

            private static string Index(string path, int i) => path + "/" + i.ToString(CultureInfo.InvariantCulture);
        }
    }
}
