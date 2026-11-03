// Tree model shown by the viewer. Children are created only when a node is
// expanded, and big arrays/objects are split into ranges of ChunkSize items so
// that expanding a node with a million elements stays instant.
// SPDX-License-Identifier: MIT
using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.Text;
using Wjson;

namespace Wjson.Viewer.Core
{
    public enum NodeKind { Null, Boolean, Number, String, Array, Object, Range }

    public sealed class JsonTreeNode
    {
        private readonly int _rangeStart;
        private readonly int _rangeEnd;   // exclusive
        private IReadOnlyList<JsonTreeNode>? _children;

        internal JsonTreeNode(string label, object? value, string pointer, int rangeStart = 0, int rangeEnd = -1)
        {
            Label = label;
            Value = value;
            Pointer = pointer;
            _rangeStart = rangeStart;
            _rangeEnd = rangeEnd;
            Kind = rangeEnd >= 0 ? NodeKind.Range : KindOf(value);
        }

        /// <summary>Object key, "[index]", the root label or a range such as "[0 … 999]".</summary>
        public string Label { get; }
        /// <summary>The parsed value (for a range: the array or object that contains it).</summary>
        public object? Value { get; }
        public NodeKind Kind { get; }
        /// <summary>JSON Pointer of this value; for a range, of its container.</summary>
        public string Pointer { get; }

        /// <summary>Short text for the value column: "\"text\"", "42", "{3}", "[10]"...</summary>
        public string ValueText => Kind switch
        {
            NodeKind.Null => "null",
            NodeKind.Boolean => (bool)Value! ? "true" : "false",
            NodeKind.Number => JsonTree.NumberText(Value),
            NodeKind.String => JsonTree.Quote((string)Value!, 200),
            NodeKind.Array => Strings.Format("Items", ((JsonArray)Value!).Count),
            NodeKind.Object => Strings.Format("Members", ((JsonObject)Value!).Count),
            _ => "",
        };

        public int ChildCount => Kind switch
        {
            NodeKind.Array => ((JsonArray)Value!).Count,
            NodeKind.Object => ((JsonObject)Value!).Count,
            NodeKind.Range => _rangeEnd - _rangeStart,
            _ => 0,
        };

        public bool HasChildren => ChildCount > 0;

        public IReadOnlyList<JsonTreeNode> GetChildren() => _children ??= CreateChildren();

        private IReadOnlyList<JsonTreeNode> CreateChildren()
        {
            if (Kind == NodeKind.Range) return CreateItems(_rangeStart, _rangeEnd);
            var count = ChildCount;
            if (count <= JsonTree.ChunkSize) return CreateItems(0, count);

            var ranges = new List<JsonTreeNode>((count + JsonTree.ChunkSize - 1) / JsonTree.ChunkSize);
            for (var start = 0; start < count; start += JsonTree.ChunkSize)
            {
                var end = Math.Min(count, start + JsonTree.ChunkSize);
                ranges.Add(new JsonTreeNode($"[{start} … {end - 1}]", Value, Pointer, start, end));
            }
            return ranges;
        }

        private IReadOnlyList<JsonTreeNode> CreateItems(int start, int end)
        {
            var items = new List<JsonTreeNode>(end - start);
            if (Value is JsonArray array)
            {
                for (var i = start; i < end; ++i)
                    items.Add(new JsonTreeNode($"[{i}]", array[i], Pointer + "/" + i.ToString(CultureInfo.InvariantCulture)));
            }
            else if (Value is JsonObject obj)
            {
                var i = 0;
                foreach (DictionaryEntry entry in obj)
                {
                    if (i >= end) break;
                    if (i++ < start) continue;
                    var key = (string)entry.Key;
                    items.Add(new JsonTreeNode(key, entry.Value, Pointer + "/" + JsonTree.EscapeToken(key)));
                }
            }
            return items;
        }

        private static NodeKind KindOf(object? value) => value switch
        {
            null => NodeKind.Null,
            bool => NodeKind.Boolean,
            long or double => NodeKind.Number,
            string => NodeKind.String,
            JsonArray => NodeKind.Array,
            JsonObject => NodeKind.Object,
            _ => throw new ArgumentException("unsupported value type: " + value.GetType()),
        };

        public override string ToString() => Kind == NodeKind.Range ? Label : $"{Label}: {ValueText}";
    }

    public static class JsonTree
    {
        /// <summary>Containers with more children than this are shown in ranges.</summary>
        public const int ChunkSize = 1000;

        public static JsonTreeNode CreateRoot(object? value) => new JsonTreeNode(Strings.Get("Root"), value, "");

        /// <summary>Number of values in the tree (each container counts as one value).</summary>
        public static long CountValues(object? value)
        {
            long count = 1;
            if (value is JsonArray array)
                foreach (var item in array) count += CountValues(item);
            else if (value is JsonObject obj)
                foreach (DictionaryEntry entry in obj) count += CountValues(entry.Value);
            return count;
        }

        /// <summary>Escapes one JSON Pointer reference token ("~" → "~0", "/" → "~1").</summary>
        public static string EscapeToken(string token) => token.Replace("~", "~0").Replace("/", "~1");

        /// <summary>Splits a JSON Pointer into unescaped tokens, or returns null if it is malformed.</summary>
        public static IReadOnlyList<string>? ParsePointer(string pointer)
        {
            if (pointer.Length == 0) return Array.Empty<string>();
            if (pointer[0] != '/') return null;
            var tokens = new List<string>();
            foreach (var raw in pointer.Substring(1).Split('/'))
            {
                var token = new StringBuilder(raw.Length);
                for (var i = 0; i < raw.Length; ++i)
                {
                    if (raw[i] != '~')
                    {
                        token.Append(raw[i]);
                        continue;
                    }
                    if (++i == raw.Length) return null;
                    if (raw[i] == '0') token.Append('~');
                    else if (raw[i] == '1') token.Append('/');
                    else return null;
                }
                tokens.Add(token.ToString());
            }
            return tokens;
        }

        /// <summary>
        /// Nodes from the root to the value at the pointer, including the range
        /// nodes in between, so that the UI can expand each one in turn. Returns
        /// null if there is no value at that pointer.
        /// </summary>
        public static IReadOnlyList<JsonTreeNode>? FindPath(JsonTreeNode root, string pointer)
        {
            var tokens = ParsePointer(pointer);
            if (tokens == null) return null;
            var path = new List<JsonTreeNode> { root };
            var current = root;
            foreach (var token in tokens)
            {
                var next = FindChild(current, token, path);
                if (next == null) return null;
                current = next;
                path.Add(current);
            }
            return path;
        }

        // Finds the child for a token, descending through range nodes (which are added to the path).
        private static JsonTreeNode? FindChild(JsonTreeNode node, string token, List<JsonTreeNode> path)
        {
            int index;
            if (node.Value is JsonArray array)
            {
                if (!IsIndex(token, out index) || index >= array.Count) return null;
            }
            else if (node.Value is JsonObject obj)
            {
                if (!obj.Contains(token)) return null;
                index = 0;
                foreach (DictionaryEntry entry in obj)
                {
                    if ((string)entry.Key == token) break;
                    ++index;
                }
            }
            else
            {
                return null;
            }

            var children = node.GetChildren();
            if (node.ChildCount > ChunkSize)
            {
                var range = children[index / ChunkSize];
                path.Add(range);
                return range.GetChildren()[index % ChunkSize];
            }
            return children[index];
        }

        private static bool IsIndex(string token, out int index)
        {
            index = 0;
            if (token.Length == 0 || (token.Length > 1 && token[0] == '0')) return false;
            foreach (var c in token)
                if (c < '0' || c > '9') return false;
            return int.TryParse(token, NumberStyles.None, CultureInfo.InvariantCulture, out index);
        }

        internal static string NumberText(object? value) => value switch
        {
            long l => l.ToString(CultureInfo.InvariantCulture),
            double d => d.ToString("R", CultureInfo.InvariantCulture),
            _ => "",
        };

        /// <summary>The string as a JSON literal for display, cut to maxLength characters.</summary>
        public static string Quote(string s, int maxLength)
        {
            var sb = new StringBuilder(Math.Min(s.Length, maxLength) + 2);
            sb.Append('"');
            foreach (var c in s)
            {
                if (sb.Length > maxLength)
                {
                    sb.Append('…');
                    break;
                }
                switch (c)
                {
                    case '"': sb.Append("\\\""); break;
                    case '\\': sb.Append("\\\\"); break;
                    case '\n': sb.Append("\\n"); break;
                    case '\r': sb.Append("\\r"); break;
                    case '\t': sb.Append("\\t"); break;
                    default:
                        if (c < 0x20) sb.Append("\\u").Append(((int)c).ToString("x4", CultureInfo.InvariantCulture));
                        else sb.Append(c);
                        break;
                }
            }
            return sb.Append('"').ToString();
        }
    }
}
