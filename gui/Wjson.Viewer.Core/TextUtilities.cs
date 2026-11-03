// Positions, line endings and file I/O for the editor.
// SPDX-License-Identifier: MIT
using System;
using System.IO;
using System.Text;

namespace Wjson.Viewer.Core
{
    public enum NewlineStyle { Lf, CrLf }

    public static class TextPositions
    {
        /// <summary>
        /// Converts a wjson line/column (1-based; columns in Unicode code points)
        /// into an index in a .NET string. Line breaks are counted like wjson
        /// does: "\r\n" once, and a lone "\r" or "\n" as one break each.
        /// </summary>
        public static int IndexOf(string text, long line, long column)
        {
            var index = 0;
            for (long current = 1; current < line && index < text.Length; ++index)
            {
                var c = text[index];
                if (c == '\r' && index + 1 < text.Length && text[index + 1] == '\n') continue; // CRLF: count at '\n'
                if (c == '\n' || c == '\r') ++current;
            }
            for (long col = 1; col < column && index < text.Length; ++col)
            {
                if (char.IsHighSurrogate(text[index]) && index + 1 < text.Length && char.IsLowSurrogate(text[index + 1]))
                    index += 2;
                else
                    index += 1;
            }
            return Math.Min(index, text.Length);
        }
    }

    public static class Newlines
    {
        /// <summary>CRLF if the first line break is "\r\n", LF otherwise.</summary>
        public static NewlineStyle Detect(string text)
        {
            var i = text.IndexOfAny(new[] { '\r', '\n' });
            return i >= 0 && text[i] == '\r' && i + 1 < text.Length && text[i + 1] == '\n' ? NewlineStyle.CrLf : NewlineStyle.Lf;
        }

        /// <summary>
        /// Converts every line break to the given style. Needed because the
        /// WinUI TextBox stores line breaks as a single "\r".
        /// </summary>
        public static string Normalize(string text, NewlineStyle style)
        {
            var lf = text.Replace("\r\n", "\n").Replace('\r', '\n');
            return style == NewlineStyle.CrLf ? lf.Replace("\n", "\r\n") : lf;
        }
    }

    /// <summary>A JSON file opened in the viewer.</summary>
    public sealed class DocumentFile
    {
        /// <summary>Files above this size open read-only and are validated in streaming mode.</summary>
        public const long LargeFileThreshold = 8L * 1024 * 1024;
        /// <summary>Files above this size are not shown as a tree (the tree needs ~10x the file size in memory).</summary>
        public const long TreeSizeLimit = 256L * 1024 * 1024;

        private DocumentFile(string path, string? text, Encoding encoding, NewlineStyle newlines, long size)
        {
            Path = path;
            Text = text;
            Encoding = encoding;
            NewlineStyle = newlines;
            Size = size;
        }

        public string Path { get; }
        public string FileName => System.IO.Path.GetFileName(Path);
        /// <summary>The content, or null for large files.</summary>
        public string? Text { get; }
        public Encoding Encoding { get; }
        public NewlineStyle NewlineStyle { get; }
        public long Size { get; }
        public bool IsLarge => Text == null;

        public string EncodingName
        {
            get
            {
                var bom = Encoding.GetPreamble().Length > 0;
                return Encoding switch
                {
                    UnicodeEncoding u when u.CodePage == 1201 => "UTF-16 BE",
                    UnicodeEncoding => "UTF-16 LE",
                    UTF8Encoding when bom => "UTF-8 BOM",
                    _ => "UTF-8",
                };
            }
        }

        public static DocumentFile Load(string path)
        {
            var size = new FileInfo(path).Length;
            if (size > LargeFileThreshold)
                return new DocumentFile(path, null, new UTF8Encoding(false), NewlineStyle.Lf, size);

            using var reader = new StreamReader(path, new UTF8Encoding(false), detectEncodingFromByteOrderMarks: true);
            var text = reader.ReadToEnd();
            return new DocumentFile(path, text, reader.CurrentEncoding, Newlines.Detect(text), size);
        }

        /// <summary>Writes editor text keeping the file's encoding (and BOM) and line endings.</summary>
        public static void Save(string path, string editorText, Encoding encoding, NewlineStyle newlines)
        {
            var text = Newlines.Normalize(editorText, newlines);
            var temp = path + ".wjson-tmp";
            File.WriteAllText(temp, text, encoding);
            if (File.Exists(path)) File.Replace(temp, path, null);
            else File.Move(temp, path);
        }
    }
}
