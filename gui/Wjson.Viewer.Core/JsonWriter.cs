// Serializes parsed values (JsonObject, JsonArray, string, long, double,
// bool, null) back to JSON, e.g. to copy a node from the tree.
// SPDX-License-Identifier: MIT
using System;
using System.Collections;
using System.Globalization;
using System.Text;
using Wjson;

namespace Wjson.Viewer.Core
{
    public static class JsonWriter
    {
        public static string Write(object? value, int indent = 2)
        {
            var sb = new StringBuilder();
            WriteValue(sb, value, indent, 0);
            return sb.ToString();
        }

        private static void WriteValue(StringBuilder sb, object? value, int indent, int level)
        {
            switch (value)
            {
                case null: sb.Append("null"); break;
                case bool b: sb.Append(b ? "true" : "false"); break;
                case long l: sb.Append(l.ToString(CultureInfo.InvariantCulture)); break;
                case double d: WriteDouble(sb, d); break;
                case string s: WriteString(sb, s); break;
                case JsonArray array:
                    sb.Append('[');
                    for (var i = 0; i < array.Count; ++i)
                    {
                        if (i > 0) sb.Append(',');
                        NewLine(sb, indent, level + 1);
                        WriteValue(sb, array[i], indent, level + 1);
                    }
                    if (array.Count > 0) NewLine(sb, indent, level);
                    sb.Append(']');
                    break;
                case JsonObject obj:
                    sb.Append('{');
                    var first = true;
                    foreach (DictionaryEntry entry in obj)
                    {
                        if (!first) sb.Append(',');
                        first = false;
                        NewLine(sb, indent, level + 1);
                        WriteString(sb, (string)entry.Key);
                        sb.Append(indent > 0 ? ": " : ":");
                        WriteValue(sb, entry.Value, indent, level + 1);
                    }
                    if (!first) NewLine(sb, indent, level);
                    sb.Append('}');
                    break;
                default:
                    throw new ArgumentException("unsupported value type: " + value.GetType());
            }
        }

        private static void NewLine(StringBuilder sb, int indent, int level)
        {
            if (indent <= 0) return;
            sb.Append('\n').Append(' ', indent * level);
        }

        private static void WriteDouble(StringBuilder sb, double d)
        {
            if (double.IsNaN(d) || double.IsInfinity(d))
            {
                sb.Append("null");
                return;
            }
            var text = d.ToString("R", CultureInfo.InvariantCulture).Replace('E', 'e'); // 1e+300, like wjson
            sb.Append(text);
            if (text.IndexOfAny(new[] { '.', 'E', 'e' }) < 0) sb.Append(".0"); // keep it a double
        }

        private static void WriteString(StringBuilder sb, string s)
        {
            sb.Append('"');
            foreach (var c in s)
            {
                switch (c)
                {
                    case '"': sb.Append("\\\""); break;
                    case '\\': sb.Append("\\\\"); break;
                    case '\b': sb.Append("\\b"); break;
                    case '\f': sb.Append("\\f"); break;
                    case '\n': sb.Append("\\n"); break;
                    case '\r': sb.Append("\\r"); break;
                    case '\t': sb.Append("\\t"); break;
                    default:
                        if (c < 0x20) sb.Append("\\u").Append(((int)c).ToString("x4", CultureInfo.InvariantCulture));
                        else sb.Append(c);
                        break;
                }
            }
            sb.Append('"');
        }
    }
}
