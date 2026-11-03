// P/Invoke declarations for the wjson C API (include/wjson/wjson.h).
// SPDX-License-Identifier: MIT
using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;

namespace Wjson
{
    internal static unsafe class Native
    {
        private const string Library = "wjson";

        static Native()
        {
            NativeLoader.Initialize();
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct Error
        {
            public int Code;
            public UIntPtr Offset;
            public UIntPtr Line;
            public UIntPtr Column;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct ParseOptions
        {
            public UIntPtr StructSize;
            public UIntPtr MaxDepth;
            public UIntPtr MaxStringLength;
            public UIntPtr MaxDocumentSize;
            public int AllowComments;
            public int AllowTrailingCommas;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct WriteOptions
        {
            public UIntPtr StructSize;
            public int Indent;
            public int AsciiOnly;
        }

        internal const int TypeNull = 0, TypeBool = 1, TypeInteger = 2, TypeDouble = 3, TypeString = 4, TypeArray = 5, TypeObject = 6;

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr wjson_version();

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr wjson_error_message(int code);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void wjson_parse_options_init(ref ParseOptions options);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void wjson_write_options_init(ref WriteOptions options);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr wjson_parse(byte* text, UIntPtr length, ref ParseOptions options, out Error error);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr wjson_parse_file(byte* path, ref ParseOptions options, out Error error);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void wjson_document_free(IntPtr document);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr wjson_document_root(IntPtr document);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int wjson_validate(byte* text, UIntPtr length, ref ParseOptions options, out Error error);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int wjson_validate_file(byte* path, ref ParseOptions options, out Error error);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int wjson_value_type(IntPtr value);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int wjson_value_get_bool(IntPtr value);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern long wjson_value_get_integer(IntPtr value);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern double wjson_value_get_double(IntPtr value);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr wjson_value_get_string(IntPtr value, out UIntPtr length);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern UIntPtr wjson_value_size(IntPtr value);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr wjson_array_at(IntPtr array, UIntPtr index);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr wjson_object_key_at(IntPtr obj, UIntPtr index, out UIntPtr length);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr wjson_object_value_at(IntPtr obj, UIntPtr index);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr wjson_value_at_pointer(IntPtr value, byte* pointer, UIntPtr length);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr wjson_format(byte* text, UIntPtr length, ref ParseOptions parseOptions,
                                                   ref WriteOptions writeOptions, out UIntPtr outLength, out Error error);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void wjson_free(IntPtr pointer);
    }

    /// <summary>
    /// Finds the native wjson library next to the managed assembly, in either
    /// the NuGet layout (runtimes/&lt;rid&gt;/native) or the assembly folder.
    /// </summary>
    internal static class NativeLoader
    {
        private static bool _initialized;

        internal static void Initialize()
        {
            if (_initialized) return;
            _initialized = true;
#if NET5_0_OR_GREATER
            NativeLibrary.SetDllImportResolver(typeof(NativeLoader).Assembly, (name, assembly, searchPath) =>
            {
                if (name != "wjson") return IntPtr.Zero;
                foreach (var candidate in Candidates())
                    if (File.Exists(candidate) && NativeLibrary.TryLoad(candidate, out var handle)) return handle;
                return IntPtr.Zero; // fall back to the default search
            });
#else
            // .NET Framework / Windows PowerShell: preload the DLL so that the
            // DllImport("wjson") calls bind to the already loaded module.
            if (!RuntimeInformation.IsOSPlatform(OSPlatform.Windows)) return;
            foreach (var candidate in Candidates())
                if (File.Exists(candidate) && LoadLibraryW(candidate) != IntPtr.Zero) return;
#endif
        }

        private static System.Collections.Generic.IEnumerable<string> Candidates()
        {
            if (!string.IsNullOrEmpty(WJson.NativeLibraryPath)) yield return WJson.NativeLibraryPath!;

            var dir = Path.GetDirectoryName(typeof(NativeLoader).Assembly.Location) ?? "";
            string os, file;
            if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows)) { os = "win"; file = "wjson.dll"; }
            else if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX)) { os = "osx"; file = "libwjson.dylib"; }
            else { os = "linux"; file = "libwjson.so"; }

            string arch;
            switch (RuntimeInformation.ProcessArchitecture)
            {
                case Architecture.X86: arch = "x86"; break;
                case Architecture.Arm64: arch = "arm64"; break;
                case Architecture.Arm: arch = "arm"; break;
                default: arch = "x64"; break;
            }
            yield return Path.Combine(dir, "runtimes", os + "-" + arch, "native", file);
            yield return Path.Combine(dir, arch, file);
            yield return Path.Combine(dir, file);
        }

#if !NET5_0_OR_GREATER
        [DllImport("kernel32", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr LoadLibraryW(string path);
#endif
    }
}
