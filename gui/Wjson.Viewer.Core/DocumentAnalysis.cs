// Validation and parsing for the viewer, safe to run on a background thread.
// SPDX-License-Identifier: MIT
using System;
using System.Collections;
using System.Threading;
using Wjson;

namespace Wjson.Viewer.Core
{
    public sealed class ValidationResult
    {
        private ValidationResult(JsonParseError? error, long values)
        {
            Error = error;
            ValueCount = values;
        }

        public bool IsValid => Error == null;
        public JsonParseError? Error { get; }
        /// <summary>Number of values, when a tree was built (0 otherwise).</summary>
        public long ValueCount { get; }
        public JsonTreeNode? Root { get; private set; }

        /// <summary>"Line 3, column 14: unexpected character", localized.</summary>
        public string Message
        {
            get
            {
                if (Error == null) return Strings.Get("Valid");
                var text = Strings.Error(Error);
                return Error.Line > 0 ? Strings.Format("ErrorAt", Error.Line, Error.Column, text) : text;
            }
        }

        internal static ValidationResult Valid(JsonTreeNode? root, long values) => new ValidationResult(null, values) { Root = root };
        internal static ValidationResult Invalid(JsonParseError error) => new ValidationResult(error, 0);
    }

    /// <summary>Steps of opening a large file, reported to show progress.</summary>
    public enum AnalysisStage { Validating, Reading, BuildingTree }

    public static class DocumentAnalysis
    {
        public static ParseOptions Options(bool jsonc) => jsonc ? ParseOptions.Jsonc : new ParseOptions();

        /// <summary>Validates text and, if it is valid, builds the tree.</summary>
        public static ValidationResult Analyze(string text, bool jsonc)
        {
            try
            {
                var value = WJson.Parse(text, Options(jsonc));
                return ValidationResult.Valid(JsonTree.CreateRoot(value), JsonTree.CountValues(value));
            }
            catch (JsonParseException e)
            {
                return ValidationResult.Invalid(e.Error);
            }
        }

        /// <summary>
        /// Validates a file in streaming mode (constant memory) and builds the
        /// tree only if the file is small enough. Reports each stage, and stops
        /// with OperationCanceledException between stages (and while counting
        /// values) when <paramref name="cancel"/> is signaled.
        /// </summary>
        public static ValidationResult AnalyzeFile(DocumentFile file, bool jsonc, Action<AnalysisStage>? progress = null,
                                                   CancellationToken cancel = default)
        {
            progress?.Invoke(AnalysisStage.Validating);
            if (!WJson.TryValidateFile(file.Path, out var error, Options(jsonc))) return ValidationResult.Invalid(error!);
            cancel.ThrowIfCancellationRequested();
            if (file.Size > DocumentFile.TreeSizeLimit) return ValidationResult.Valid(null, 0);

            progress?.Invoke(AnalysisStage.Reading);
            var value = WJson.ParseFile(file.Path, Options(jsonc));
            cancel.ThrowIfCancellationRequested();

            progress?.Invoke(AnalysisStage.BuildingTree);
            var count = CountValues(value, cancel);
            return ValidationResult.Valid(JsonTree.CreateRoot(value), count);
        }

        private static long CountValues(object? value, CancellationToken cancel)
        {
            long count = 1;
            if (value is JsonArray array)
            {
                foreach (var item in array) count += CountValues(item, cancel);
                cancel.ThrowIfCancellationRequested();
            }
            else if (value is JsonObject obj)
            {
                foreach (DictionaryEntry entry in obj) count += CountValues(entry.Value, cancel);
                cancel.ThrowIfCancellationRequested();
            }
            return count;
        }
    }
}
