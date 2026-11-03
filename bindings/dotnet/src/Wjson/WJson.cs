// Public .NET API for wjson.
// SPDX-License-Identifier: MIT
using System;
using System.Collections.Generic;
using System.Collections.Specialized;
using System.IO;
using System.Text;

namespace Wjson
{
    /// <summary>Error codes reported by the parser (same values as the C API).</summary>
#pragma warning disable CS1591 // the member names are self-explanatory
    public enum JsonErrorCode
    {
        None = 0,
        UnexpectedEnd = 1,
        UnexpectedCharacter = 2,
        TrailingCharacters = 3,
        ExpectedKey = 4,
        ExpectedColon = 5,
        ExpectedCommaOrBrace = 6,
        ExpectedCommaOrBracket = 7,
        InvalidLiteral = 8,
        InvalidNumber = 9,
        NumberOutOfRange = 10,
        UnterminatedString = 11,
        ControlCharacterInString = 12,
        InvalidEscape = 13,
        InvalidUnicodeEscape = 14,
        InvalidSurrogate = 15,
        InvalidUtf8 = 16,
        InvalidEncoding = 17,
        DepthLimitExceeded = 18,
        IoError = 19,
        InvalidComment = 20,
        UnterminatedComment = 21,
        StringTooLong = 22,
        DocumentTooLarge = 23,
        Cancelled = 24,
        OutOfMemory = 100,
        InvalidArgument = 101,
    }
#pragma warning restore CS1591

    /// <summary>Where and why parsing failed.</summary>
    public sealed class JsonParseError
    {
        internal JsonParseError(JsonErrorCode code, long offset, long line, long column, string message)
        {
            Code = code;
            Offset = offset;
            Line = line;
            Column = column;
            Message = message;
        }

        /// <summary>Error code.</summary>
        public JsonErrorCode Code { get; }
        /// <summary>Byte offset in the UTF-8 text.</summary>
        public long Offset { get; }
        /// <summary>1-based line (0 when not applicable, e.g. I/O errors).</summary>
        public long Line { get; }
        /// <summary>1-based column, in Unicode code points.</summary>
        public long Column { get; }
        /// <summary>English description of the error.</summary>
        public string Message { get; }

        /// <inheritdoc />
        public override string ToString() => Line > 0 ? $"{Line}:{Column}: {Message}" : Message;
    }

    /// <summary>Thrown when a document cannot be parsed.</summary>
    public sealed class JsonParseException : FormatException
    {
        internal JsonParseException(JsonParseError error) : base(error.ToString())
        {
            Error = error;
        }

        /// <summary>Details of the error.</summary>
        public JsonParseError Error { get; }
    }

    /// <summary>Parser options. Defaults follow RFC 8259 strictly.</summary>
    public sealed class ParseOptions
    {
        /// <summary>Maximum nesting of arrays and objects.</summary>
        public int MaxDepth { get; set; } = 512;
        /// <summary>Maximum length in UTF-8 bytes of a string or key (null = unlimited).</summary>
        public long? MaxStringLength { get; set; }
        /// <summary>Maximum document size in UTF-8 bytes (null = unlimited).</summary>
        public long? MaxDocumentSize { get; set; }
        /// <summary>Allow // and /* */ comments.</summary>
        public bool AllowComments { get; set; }
        /// <summary>Allow a comma after the last element of an array or object.</summary>
        public bool AllowTrailingCommas { get; set; }

        /// <summary>JSON with Comments (VS Code, Windows Terminal settings).</summary>
        public static ParseOptions Jsonc => new ParseOptions { AllowComments = true, AllowTrailingCommas = true };

        internal Native.ParseOptions ToNative()
        {
            var o = new Native.ParseOptions();
            Native.wjson_parse_options_init(ref o);
            o.MaxDepth = (UIntPtr)(ulong)Math.Max(1, MaxDepth);
            if (MaxStringLength.HasValue) o.MaxStringLength = (UIntPtr)(ulong)Math.Max(0, MaxStringLength.Value);
            if (MaxDocumentSize.HasValue) o.MaxDocumentSize = (UIntPtr)(ulong)Math.Max(0, MaxDocumentSize.Value);
            o.AllowComments = AllowComments ? 1 : 0;
            o.AllowTrailingCommas = AllowTrailingCommas ? 1 : 0;
            return o;
        }
    }

    /// <summary>
    /// A JSON object. Keeps members in document order; keys are case-sensitive.
    /// With duplicate keys the last value wins. Works as an ordered hashtable in PowerShell.
    /// </summary>
    public sealed class JsonObject : OrderedDictionary
    {
        /// <summary>Creates an empty object.</summary>
        public JsonObject() : base(StringComparer.Ordinal) { }

        /// <summary>Gets or sets a member by key (null if missing).</summary>
        public object? this[string key]
        {
            get => base[key];
            set => base[key] = value;
        }

        /// <summary>Gets a member if present.</summary>
        public bool TryGetValue(string key, out object? value)
        {
            if (Contains(key))
            {
                value = base[key];
                return true;
            }
            value = null;
            return false;
        }
    }

    /// <summary>A JSON array.</summary>
    public sealed class JsonArray : List<object?>
    {
    }

    /// <summary>
    /// Entry point. Parsed values are mapped to .NET types: null, bool, long
    /// (integers that fit in 64 bits), double, string, <see cref="JsonArray"/>
    /// and <see cref="JsonObject"/>.
    /// </summary>
    public static unsafe class WJson
    {
        /// <summary>
        /// Full path of the native library (wjson.dll / libwjson.so) to load. Optional:
        /// by default it is looked up next to this assembly, also in runtimes/&lt;rid&gt;/native.
        /// Must be set before the first call into the library.
        /// </summary>
        public static string? NativeLibraryPath { get; set; }

        /// <summary>Version of the native wjson library.</summary>
        public static string Version => Utf8(Native.wjson_version(), null) ?? "";

        /// <summary>Parses JSON text. Throws <see cref="JsonParseException"/> on invalid input.</summary>
        public static object? Parse(string json, ParseOptions? options = null)
        {
            using (var doc = Document.FromText(json, options)) return Convert(doc.Root);
        }

        /// <summary>Parses a file (UTF-8 with or without BOM, or UTF-16 with BOM).</summary>
        public static object? ParseFile(string path, ParseOptions? options = null)
        {
            using (var doc = Document.FromFile(path, options)) return Convert(doc.Root);
        }

        /// <summary>Checks whether text is valid JSON without building a tree.</summary>
        public static bool TryValidate(string json, out JsonParseError? error, ParseOptions? options = null)
        {
            var o = (options ?? new ParseOptions()).ToNative();
            var bytes = ToUtf8(json);
            Native.Error e;
            int ok;
            fixed (byte* p = bytes) ok = Native.wjson_validate(p, (UIntPtr)(ulong)bytes.Length, ref o, out e);
            error = ok == 1 ? null : ToError(e);
            return ok == 1;
        }

        /// <summary>Validates a file of any size with constant memory (streaming).</summary>
        public static bool TryValidateFile(string path, out JsonParseError? error, ParseOptions? options = null)
        {
            var o = (options ?? new ParseOptions()).ToNative();
            var bytes = ToUtf8Z(Path.GetFullPath(path));
            Native.Error e;
            int ok;
            fixed (byte* p = bytes) ok = Native.wjson_validate_file(p, ref o, out e);
            error = ok == 1 ? null : ToError(e);
            return ok == 1;
        }

        /// <summary>Pretty-prints JSON with the given indentation (spaces).</summary>
        public static string Format(string json, int indent = 2, bool asciiOnly = false, ParseOptions? options = null)
            => Reformat(json, Math.Max(1, indent), asciiOnly, options);

        /// <summary>Removes all insignificant whitespace (and comments, with JSONC options).</summary>
        public static string Minify(string json, bool asciiOnly = false, ParseOptions? options = null)
            => Reformat(json, 0, asciiOnly, options);

        /// <summary>
        /// Gets the value at a JSON Pointer (RFC 6901), e.g. "/items/0/name".
        /// Returns false if there is no value at that path.
        /// </summary>
        public static bool TryQuery(string json, string pointer, out object? value, ParseOptions? options = null)
        {
            if (pointer == null) throw new ArgumentNullException(nameof(pointer));
            using (var doc = Document.FromText(json, options))
            {
                var bytes = ToUtf8(pointer);
                IntPtr found;
                fixed (byte* p = bytes) found = Native.wjson_value_at_pointer(doc.Root, p, (UIntPtr)(ulong)bytes.Length);
                value = found == IntPtr.Zero ? null : Convert(found);
                return found != IntPtr.Zero;
            }
        }

        private static string Reformat(string json, int indent, bool asciiOnly, ParseOptions? options)
        {
            var po = (options ?? new ParseOptions()).ToNative();
            var wo = new Native.WriteOptions();
            Native.wjson_write_options_init(ref wo);
            wo.Indent = indent;
            wo.AsciiOnly = asciiOnly ? 1 : 0;
            var bytes = ToUtf8(json);
            IntPtr result;
            UIntPtr length;
            Native.Error e;
            fixed (byte* p = bytes) result = Native.wjson_format(p, (UIntPtr)(ulong)bytes.Length, ref po, ref wo, out length, out e);
            if (result == IntPtr.Zero) throw new JsonParseException(ToError(e));
            try
            {
                return Encoding.UTF8.GetString((byte*)result, checked((int)(ulong)length));
            }
            finally
            {
                Native.wjson_free(result);
            }
        }

        // Owns a native document.
        private sealed class Document : IDisposable
        {
            private IntPtr _handle;

            private Document(IntPtr handle) { _handle = handle; }

            internal IntPtr Root => Native.wjson_document_root(_handle);

            internal static Document FromText(string json, ParseOptions? options)
            {
                var o = (options ?? new ParseOptions()).ToNative();
                var bytes = ToUtf8(json);
                IntPtr handle;
                Native.Error e;
                fixed (byte* p = bytes) handle = Native.wjson_parse(p, (UIntPtr)(ulong)bytes.Length, ref o, out e);
                if (handle == IntPtr.Zero) throw new JsonParseException(ToError(e));
                return new Document(handle);
            }

            internal static Document FromFile(string path, ParseOptions? options)
            {
                if (path == null) throw new ArgumentNullException(nameof(path));
                var o = (options ?? new ParseOptions()).ToNative();
                var bytes = ToUtf8Z(Path.GetFullPath(path));
                IntPtr handle;
                Native.Error e;
                fixed (byte* p = bytes) handle = Native.wjson_parse_file(p, ref o, out e);
                if (handle == IntPtr.Zero)
                {
                    var error = ToError(e);
                    if (error.Code == JsonErrorCode.IoError) throw new FileNotFoundException(error.Message, path);
                    throw new JsonParseException(error);
                }
                return new Document(handle);
            }

            public void Dispose()
            {
                if (_handle != IntPtr.Zero) Native.wjson_document_free(_handle);
                _handle = IntPtr.Zero;
            }
        }

        // Copies a native value tree into .NET objects.
        private static object? Convert(IntPtr value)
        {
            switch (Native.wjson_value_type(value))
            {
                case Native.TypeBool: return Native.wjson_value_get_bool(value) != 0;
                case Native.TypeInteger: return Native.wjson_value_get_integer(value);
                case Native.TypeDouble: return Native.wjson_value_get_double(value);
                case Native.TypeString:
                {
                    var p = Native.wjson_value_get_string(value, out var length);
                    return Utf8(p, length) ?? "";
                }
                case Native.TypeArray:
                {
                    var count = (ulong)Native.wjson_value_size(value);
                    var array = new JsonArray();
                    array.Capacity = checked((int)count);
                    for (ulong i = 0; i < count; ++i) array.Add(Convert(Native.wjson_array_at(value, (UIntPtr)i)));
                    return array;
                }
                case Native.TypeObject:
                {
                    var count = (ulong)Native.wjson_value_size(value);
                    var obj = new JsonObject();
                    for (ulong i = 0; i < count; ++i)
                    {
                        var key = Utf8(Native.wjson_object_key_at(value, (UIntPtr)i, out var keyLength), keyLength) ?? "";
                        obj[key] = Convert(Native.wjson_object_value_at(value, (UIntPtr)i)); // last duplicate wins
                    }
                    return obj;
                }
                default: return null;
            }
        }

        private static JsonParseError ToError(Native.Error e)
        {
            var message = Utf8(Native.wjson_error_message(e.Code), null) ?? "unknown error";
            return new JsonParseError((JsonErrorCode)e.Code, (long)(ulong)e.Offset, (long)(ulong)e.Line,
                                      (long)(ulong)e.Column, message);
        }

        private static byte[] ToUtf8(string s)
        {
            if (s == null) throw new ArgumentNullException(nameof(s));
            return Encoding.UTF8.GetBytes(s);
        }

        // NUL-terminated UTF-8, for paths.
        private static byte[] ToUtf8Z(string s)
        {
            var bytes = new byte[Encoding.UTF8.GetByteCount(s) + 1];
            Encoding.UTF8.GetBytes(s, 0, s.Length, bytes, 0);
            return bytes;
        }

        private static string? Utf8(IntPtr p, UIntPtr? length)
        {
            if (p == IntPtr.Zero) return null;
            var bytes = (byte*)p;
            int n;
            if (length.HasValue)
            {
                n = checked((int)(ulong)length.Value);
            }
            else
            {
                n = 0;
                while (bytes[n] != 0) ++n;
            }
            return Encoding.UTF8.GetString(bytes, n);
        }
    }
}
