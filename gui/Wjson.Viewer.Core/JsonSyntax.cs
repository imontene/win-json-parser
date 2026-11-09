// Syntax highlighting for the editor: a tolerant tokenizer (it also colors
// invalid or half-typed JSON), the colors, the smallest range to repaint after
// an edit, and RTF output to load a whole colored document at once.
// SPDX-License-Identifier: MIT
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace Wjson.Viewer.Core
{
    /// <summary>What a colored piece of text is. Punctuation and whitespace have no span (default color).</summary>
    public enum SyntaxKind : byte { Key, String, Number, Literal, Comment }

    public readonly struct SyntaxSpan : IEquatable<SyntaxSpan>
    {
        public SyntaxSpan(int start, int length, SyntaxKind kind)
        {
            Start = start;
            Length = length;
            Kind = kind;
        }

        public int Start { get; }
        public int Length { get; }
        public int End => Start + Length;
        public SyntaxKind Kind { get; }

        public bool Equals(SyntaxSpan other) => Start == other.Start && Length == other.Length && Kind == other.Kind;
        public override bool Equals(object? obj) => obj is SyntaxSpan other && Equals(other);
        public override int GetHashCode() => HashCode.Combine(Start, Length, Kind);
        public override string ToString() => $"{Kind} {Start}+{Length}";
    }

    /// <summary>An RGB color, independent of any UI framework.</summary>
    public readonly struct Rgb : IEquatable<Rgb>
    {
        public Rgb(byte r, byte g, byte b)
        {
            R = r;
            G = g;
            B = b;
        }

        public byte R { get; }
        public byte G { get; }
        public byte B { get; }

        public bool Equals(Rgb other) => R == other.R && G == other.G && B == other.B;
        public override bool Equals(object? obj) => obj is Rgb other && Equals(other);
        public override int GetHashCode() => (R << 16) | (G << 8) | B;
        public override string ToString() => $"#{R:X2}{G:X2}{B:X2}";
    }

    public static class SyntaxColors
    {
        /// <summary>Color of text without a span (punctuation, whitespace).</summary>
        public static Rgb Default(bool dark) => dark ? new Rgb(0xE6, 0xE6, 0xE6) : new Rgb(0x1B, 0x1B, 0x1B);

        /// <summary>Same hues as the values in the tree, plus keys and comments.</summary>
        public static Rgb For(SyntaxKind kind, bool dark) => kind switch
        {
            SyntaxKind.Key => dark ? new Rgb(0xF0, 0xB0, 0x78) : new Rgb(0xA3, 0x4A, 0x00),
            SyntaxKind.String => dark ? new Rgb(0x8C, 0xD9, 0x9C) : new Rgb(0x0B, 0x7A, 0x3E),
            SyntaxKind.Number => dark ? new Rgb(0x8C, 0xC4, 0xFF) : new Rgb(0x04, 0x51, 0xA5),
            SyntaxKind.Literal => dark ? new Rgb(0xD2, 0xA8, 0xFF) : new Rgb(0x80, 0x2A, 0xC8),
            _ => dark ? new Rgb(0x9A, 0x9A, 0x9A) : new Rgb(0x6B, 0x6B, 0x6B),
        };
    }

    public static class JsonSyntax
    {
        /// <summary>Documents longer than this (in characters) are not colored: the editor stays responsive.</summary>
        public const int HighlightLimit = 2_000_000;

        /// <summary>Colored spans of a text, in order and without overlaps.</summary>
        public static List<SyntaxSpan> Tokenize(string text)
        {
            var spans = new List<SyntaxSpan>(Math.Min(text.Length / 4, 1 << 20));
            var i = 0;
            var n = text.Length;
            while (i < n)
            {
                var c = text[i];
                if (c == '"')
                {
                    var end = StringEnd(text, i);
                    var kind = NextSignificant(text, end) is int colon && text[colon] == ':' ? SyntaxKind.Key : SyntaxKind.String;
                    spans.Add(new SyntaxSpan(i, end - i, kind));
                    i = end;
                }
                else if (c == '/' && i + 1 < n && (text[i + 1] == '/' || text[i + 1] == '*'))
                {
                    var end = CommentEnd(text, i);
                    spans.Add(new SyntaxSpan(i, end - i, SyntaxKind.Comment));
                    i = end;
                }
                else if (c == '-' || (c >= '0' && c <= '9'))
                {
                    var end = i + 1;
                    while (end < n && IsNumberChar(text[end])) ++end;
                    spans.Add(new SyntaxSpan(i, end - i, SyntaxKind.Number));
                    i = end;
                }
                else if (IsWordChar(c))
                {
                    var end = i + 1;
                    while (end < n && IsWordChar(text[end])) ++end;
                    var word = text.AsSpan(i, end - i);
                    if (word.SequenceEqual("true") || word.SequenceEqual("false") || word.SequenceEqual("null"))
                        spans.Add(new SyntaxSpan(i, end - i, SyntaxKind.Literal));
                    i = end;
                }
                else
                {
                    ++i;
                }
            }
            return spans;
        }

        private static bool IsNumberChar(char c) =>
            (c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-';

        private static bool IsWordChar(char c) => (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';

        /// <summary>End (exclusive) of a string starting at a quote; an unterminated string ends at the line break.</summary>
        internal static int StringEnd(string text, int start)
        {
            var i = start + 1;
            while (i < text.Length)
            {
                var c = text[i];
                if (c == '"') return i + 1;
                if (c == '\r' || c == '\n') return i;
                i += c == '\\' && i + 1 < text.Length && text[i + 1] != '\r' && text[i + 1] != '\n' ? 2 : 1;
            }
            return text.Length;
        }

        /// <summary>End (exclusive) of a // or /* comment starting at <paramref name="start"/>.</summary>
        internal static int CommentEnd(string text, int start)
        {
            if (text[start + 1] == '/')
            {
                var lineEnd = text.IndexOfAny(LineBreaks, start + 2);
                return lineEnd < 0 ? text.Length : lineEnd;
            }
            var close = text.IndexOf("*/", start + 2, StringComparison.Ordinal);
            return close < 0 ? text.Length : close + 2;
        }

        private static readonly char[] LineBreaks = { '\r', '\n' };

        /// <summary>Index of the next character that is not whitespace or inside a comment, or null.</summary>
        internal static int? NextSignificant(string text, int i)
        {
            while (i < text.Length)
            {
                var c = text[i];
                if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
                {
                    ++i;
                }
                else if (c == '/' && i + 1 < text.Length && (text[i + 1] == '/' || text[i + 1] == '*'))
                {
                    i = CommentEnd(text, i);
                }
                else
                {
                    return i;
                }
            }
            return null;
        }

        /// <summary>
        /// The range [Start, End) of the new text whose colors must be painted
        /// again after an edit: the edited text itself (typed characters take
        /// the color of their neighbour) plus every span that changed. Spans
        /// after the edit are compared shifted by the change in length.
        /// Returns null when nothing needs painting.
        /// </summary>
        public static (int Start, int End)? RepaintRange(IReadOnlyList<SyntaxSpan> oldSpans, IReadOnlyList<SyntaxSpan> newSpans, TextEdit edit)
        {
            var delta = edit.Inserted.Length - edit.Removed.Length;
            var oldEditEnd = edit.Start + edit.Removed.Length;

            var prefix = 0;
            var max = Math.Min(oldSpans.Count, newSpans.Count);
            while (prefix < max && oldSpans[prefix].Equals(newSpans[prefix]) && oldSpans[prefix].End <= edit.Start) ++prefix;

            var suffix = 0;
            while (suffix < max - prefix)
            {
                var o = oldSpans[oldSpans.Count - 1 - suffix];
                var nw = newSpans[newSpans.Count - 1 - suffix];
                if (o.Start < oldEditEnd || o.Start + delta != nw.Start || o.Length != nw.Length || o.Kind != nw.Kind) break;
                ++suffix;
            }

            int start = int.MaxValue, end = int.MinValue;
            if (edit.Inserted.Length > 0 || edit.Removed.Length > 0)
            {
                start = edit.Start;
                end = edit.Start + edit.Inserted.Length;
            }
            // Old spans that changed: repaint what is left of them in the new text.
            // A span start is the position of its first character, a span end
            // the position after its last one; this matters for an insertion
            // exactly at a span boundary.
            int Map(int p, bool startOfRange)
            {
                if (startOfRange)
                {
                    if (p >= oldEditEnd) return p + delta;
                    return p < edit.Start ? p : edit.Start;
                }
                if (p <= edit.Start) return p;
                return p >= oldEditEnd ? p + delta : edit.Start + edit.Inserted.Length; // inside the removed text
            }
            for (var k = prefix; k < oldSpans.Count - suffix; ++k)
            {
                start = Math.Min(start, Map(oldSpans[k].Start, true));
                end = Math.Max(end, Map(oldSpans[k].End, false));
            }
            for (var k = prefix; k < newSpans.Count - suffix; ++k)
            {
                start = Math.Min(start, newSpans[k].Start);
                end = Math.Max(end, newSpans[k].End);
            }
            if (start > end) return null;
            if (start == end && edit.Inserted.Length == 0) return null;
            return (start, end);
        }

        /// <summary>
        /// The document as RTF with every character colored, so that a whole
        /// document is loaded and colored in one call. Line breaks may be
        /// "\r", "\n" or "\r\n" (each becomes one paragraph break).
        /// </summary>
        public static string ToRtf(string text, IReadOnlyList<SyntaxSpan> spans, bool dark, string fontName, double fontSizePoints)
        {
            var sb = new StringBuilder(text.Length + text.Length / 4 + spans.Count * 6 + 256);
            sb.Append(@"{\rtf1\ansi\deff0{\fonttbl{\f0\fmodern\fprq1 ").Append(EscapeFontName(fontName)).Append(@";}}");
            sb.Append(@"{\colortbl;");
            AppendColor(sb, SyntaxColors.Default(dark));                // \cf1
            foreach (SyntaxKind kind in Enum.GetValues(typeof(SyntaxKind)))
                AppendColor(sb, SyntaxColors.For(kind, dark));          // \cf2 ... in enum order
            sb.Append('}');
            var halfPoints = Math.Max(2, (int)Math.Round(fontSizePoints * 2));
            sb.Append(@"\f0\fs").Append(halfPoints.ToString(CultureInfo.InvariantCulture)).Append(@"\cf1 ");

            var pos = 0;
            foreach (var span in spans)
            {
                AppendText(sb, text, pos, span.Start);
                sb.Append(@"\cf").Append(((int)span.Kind + 2).ToString(CultureInfo.InvariantCulture)).Append(' ');
                AppendText(sb, text, span.Start, span.End);
                sb.Append(@"\cf1 ");
                pos = span.End;
            }
            AppendText(sb, text, pos, text.Length);
            return sb.Append('}').ToString();
        }

        private static string EscapeFontName(string name) => name.Replace("\\", "").Replace("{", "").Replace("}", "").Replace(";", "");

        private static void AppendColor(StringBuilder sb, Rgb c) =>
            sb.Append(@"\red").Append(c.R.ToString(CultureInfo.InvariantCulture))
              .Append(@"\green").Append(c.G.ToString(CultureInfo.InvariantCulture))
              .Append(@"\blue").Append(c.B.ToString(CultureInfo.InvariantCulture)).Append(';');

        private static void AppendText(StringBuilder sb, string text, int start, int end)
        {
            for (var i = start; i < end; ++i)
            {
                var c = text[i];
                switch (c)
                {
                    case '\\': sb.Append(@"\\"); break;
                    case '{': sb.Append(@"\{"); break;
                    case '}': sb.Append(@"\}"); break;
                    case '\t': sb.Append(@"\tab "); break;
                    case '\r':
                        if (i + 1 < end && text[i + 1] == '\n') ++i;
                        sb.Append(@"\par ");
                        break;
                    case '\n': sb.Append(@"\par "); break;
                    default:
                        if (c < 0x20)
                        {
                            sb.Append('?');
                        }
                        else if (c < 0x80)
                        {
                            sb.Append(c);
                        }
                        else
                        {
                            // \uN takes a signed 16-bit number; '?' is the fallback for old readers.
                            sb.Append(@"\u").Append(((short)c).ToString(CultureInfo.InvariantCulture)).Append('?');
                        }
                        break;
                }
            }
        }
    }
}
