// Links the text and the tree: the JSON Pointer of the value at a text
// position, and the text range of the value at a JSON Pointer. Works on the
// editor text directly (positions are .NET string indices), accepts JSONC and
// any line break style, and returns null for text it cannot follow.
// SPDX-License-Identifier: MIT
using System;
using System.Globalization;
using System.Text;

namespace Wjson.Viewer.Core
{
    /// <summary>Where a value is in the text: [Start, End), and the start of its key for object members.</summary>
    public readonly struct TextSpanInfo
    {
        public TextSpanInfo(int start, int end, int? keyStart)
        {
            Start = start;
            End = end;
            KeyStart = keyStart;
        }

        public int Start { get; }
        public int End { get; }
        public int Length => End - Start;
        public int? KeyStart { get; }
    }

    public static class JsonLocator
    {
        /// <summary>
        /// Pointer of the innermost value that contains the character at
        /// <paramref name="offset"/>; a key counts as part of its member. Null if
        /// the offset is outside the top-level value or the text is malformed.
        /// </summary>
        public static string? PointerAt(string text, int offset)
        {
            if (offset < 0 || offset >= text.Length) return null;
            var scanner = new Scanner(text, offset);
            try
            {
                scanner.SkipInsignificant();
                return scanner.AtEnd ? null : scanner.Visit();
            }
            catch (FormatException)
            {
                return null;
            }
        }

        /// <summary>
        /// Pointer for a caret position (between characters): the deeper of the
        /// values at the character after and before the caret, so that a caret
        /// right after <c>42</c> or <c>"text"</c> still finds that value.
        /// </summary>
        public static string? PointerAtCaret(string text, int caret)
        {
            var after = PointerAt(text, caret);
            var before = caret > 0 ? PointerAt(text, caret - 1) : null;
            if (before == null) return after;
            if (after == null) return before;
            return Depth(before) > Depth(after) ? before : after;
        }

        private static int Depth(string pointer)
        {
            var n = 0;
            foreach (var c in pointer)
                if (c == '/') ++n;
            return n;
        }

        /// <summary>Text range of the value at a JSON Pointer, or null if there is none.</summary>
        public static TextSpanInfo? Find(string text, string pointer)
        {
            var tokens = JsonTree.ParsePointer(pointer);
            if (tokens == null) return null;
            var scanner = new Scanner(text, -1);
            try
            {
                return scanner.Find(tokens);
            }
            catch (FormatException)
            {
                return null;
            }
        }

        private sealed class Scanner
        {
            private readonly string _text;
            private readonly int _target;  // PointerAt: the offset looked for
            private int _pos;

            public Scanner(string text, int target)
            {
                _text = text;
                _target = target;
            }

            public bool AtEnd => _pos >= _text.Length;

            private char Current => _pos < _text.Length ? _text[_pos] : throw new FormatException();

            public void SkipInsignificant() => _pos = JsonSyntax.NextSignificant(_text, _pos) ?? _text.Length;

            private void Expect(char c)
            {
                SkipInsignificant();
                if (Current != c) throw new FormatException();
                ++_pos;
            }

            // Path to the value being visited: an array index, or the key's text range. The
            // pointer string is only built for the result, so scanning stays cheap.
            private readonly System.Collections.Generic.List<(int Index, int KeyStart, int KeyEnd)> _path =
                new System.Collections.Generic.List<(int, int, int)>();

            private string PathString(int extraKeyStart = -1, int extraKeyEnd = -1)
            {
                var sb = new StringBuilder();
                foreach (var (index, keyStart, keyEnd) in _path)
                {
                    sb.Append('/');
                    if (index >= 0) sb.Append(index.ToString(CultureInfo.InvariantCulture));
                    else sb.Append(JsonTree.EscapeToken(Unescape(_text, keyStart, keyEnd)));
                }
                if (extraKeyStart >= 0) sb.Append('/').Append(JsonTree.EscapeToken(Unescape(_text, extraKeyStart, extraKeyEnd)));
                return sb.ToString();
            }

            /// <summary>Parses the value at the current position; returns a pointer if it contains the target.</summary>
            public string? Visit()
            {
                if (_path.Count > 10_000) throw new FormatException();
                var start = _pos;
                var c = Current;
                if (c == '{')
                {
                    ++_pos;
                    while (true)
                    {
                        SkipInsignificant();
                        if (_pos > _target) return PathString();    // the target is between members: the object itself
                        if (Current == '}')
                        {
                            ++_pos;
                            break;
                        }
                        if (Current != '"') throw new FormatException();
                        var keyStart = _pos;
                        _pos = JsonSyntax.StringEnd(_text, _pos);
                        var keyEnd = _pos;
                        if (_target >= keyStart && _target < keyEnd) return PathString(keyStart, keyEnd);
                        Expect(':');
                        if (_target < _pos) return PathString(keyStart, keyEnd);   // on the colon or before it
                        SkipInsignificant();
                        if (_pos > _target) return PathString(keyStart, keyEnd);   // between the colon and the value
                        _path.Add((-1, keyStart, keyEnd));
                        var found = Visit();
                        _path.RemoveAt(_path.Count - 1);
                        if (found != null) return found;
                        SkipInsignificant();
                        if (_pos < _text.Length && _text[_pos] == ',') ++_pos;
                    }
                }
                else if (c == '[')
                {
                    ++_pos;
                    for (var index = 0; ; ++index)
                    {
                        SkipInsignificant();
                        if (_pos > _target) return PathString();
                        if (Current == ']')
                        {
                            ++_pos;
                            break;
                        }
                        _path.Add((index, 0, 0));
                        var found = Visit();
                        _path.RemoveAt(_path.Count - 1);
                        if (found != null) return found;
                        SkipInsignificant();
                        if (_pos < _text.Length && _text[_pos] == ',') ++_pos;
                    }
                }
                else
                {
                    SkipScalar();
                }
                return _target >= start && _target < _pos ? PathString() : null;
            }

            private void SkipScalar()
            {
                if (Current == '"')
                {
                    var end = JsonSyntax.StringEnd(_text, _pos);
                    if (_text[end - 1] != '"' || end - _pos < 2) throw new FormatException();
                    _pos = end;
                    return;
                }
                var start = _pos;
                while (_pos < _text.Length)
                {
                    var c = _text[_pos];
                    if (c == ',' || c == '}' || c == ']' || c == ':' || c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '/' ||
                        c == '"' || c == '{' || c == '[')
                        break;
                    ++_pos;
                }
                if (_pos == start) throw new FormatException();
            }

            /// <summary>Moves past the value at the current position.</summary>
            private void SkipValue()
            {
                var c = Current;
                if (c != '{' && c != '[')
                {
                    SkipScalar();
                    return;
                }
                var depth = 0;
                while (_pos < _text.Length)
                {
                    c = _text[_pos];
                    if (c == '"')
                    {
                        _pos = JsonSyntax.StringEnd(_text, _pos);
                        continue;
                    }
                    if (c == '/' && _pos + 1 < _text.Length && (_text[_pos + 1] == '/' || _text[_pos + 1] == '*'))
                    {
                        _pos = JsonSyntax.CommentEnd(_text, _pos);
                        continue;
                    }
                    ++_pos;
                    if (c == '{' || c == '[') ++depth;
                    else if ((c == '}' || c == ']') && --depth == 0) return;
                }
                throw new FormatException();
            }

            public TextSpanInfo? Find(System.Collections.Generic.IReadOnlyList<string> tokens)
            {
                SkipInsignificant();
                if (AtEnd) return null;
                int? keyStart = null;
                foreach (var token in tokens)
                {
                    var c = Current;
                    if (c == '{')
                    {
                        ++_pos;
                        int? valueStart = null;
                        while (true)
                        {
                            SkipInsignificant();
                            if (Current == '}') break;
                            if (Current != '"') throw new FormatException();
                            var start = _pos;
                            _pos = JsonSyntax.StringEnd(_text, _pos);
                            var match = Unescape(_text, start, _pos) == token;
                            Expect(':');
                            SkipInsignificant();
                            if (match)
                            {
                                valueStart = _pos;              // the last duplicate wins, as in the parser
                                keyStart = start;
                            }
                            SkipValue();
                            SkipInsignificant();
                            if (Current == ',') ++_pos;
                        }
                        if (valueStart == null) return null;
                        _pos = valueStart.Value;
                    }
                    else if (c == '[')
                    {
                        if (!IsIndex(token, out var index)) return null;
                        ++_pos;
                        for (var i = 0; ; ++i)
                        {
                            SkipInsignificant();
                            if (Current == ']') return null;
                            if (i == index) break;
                            SkipValue();
                            SkipInsignificant();
                            if (Current == ',') ++_pos;
                        }
                        keyStart = null;
                    }
                    else
                    {
                        return null;
                    }
                }
                var valueBegin = _pos;
                SkipValue();
                return new TextSpanInfo(valueBegin, _pos, keyStart);
            }

            private static bool IsIndex(string token, out int index)
            {
                index = 0;
                if (token.Length == 0 || (token.Length > 1 && token[0] == '0')) return false;
                foreach (var c in token)
                    if (c < '0' || c > '9') return false;
                return int.TryParse(token, NumberStyles.None, CultureInfo.InvariantCulture, out index);
            }
        }

        /// <summary>The value of the JSON string literal text[start, end) (quotes included).</summary>
        internal static string Unescape(string text, int start, int end)
        {
            if (end - start < 2 || text[end - 1] != '"') throw new FormatException();
            var inner = text.AsSpan(start + 1, end - start - 2);
            if (inner.IndexOf('\\') < 0) return inner.ToString();
            var sb = new StringBuilder(inner.Length);
            for (var i = 0; i < inner.Length; ++i)
            {
                var c = inner[i];
                if (c != '\\')
                {
                    sb.Append(c);
                    continue;
                }
                if (++i >= inner.Length) throw new FormatException();
                switch (inner[i])
                {
                    case '"': sb.Append('"'); break;
                    case '\\': sb.Append('\\'); break;
                    case '/': sb.Append('/'); break;
                    case 'b': sb.Append('\b'); break;
                    case 'f': sb.Append('\f'); break;
                    case 'n': sb.Append('\n'); break;
                    case 'r': sb.Append('\r'); break;
                    case 't': sb.Append('\t'); break;
                    case 'u':
                        if (i + 4 >= inner.Length) throw new FormatException();
                        if (!int.TryParse(inner.Slice(i + 1, 4), NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out var code))
                            throw new FormatException();
                        sb.Append((char)code);
                        i += 4;
                        break;
                    default: throw new FormatException();
                }
            }
            return sb.ToString();
        }
    }
}
