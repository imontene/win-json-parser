// User-facing text in Spanish and English. Parser errors are translated from
// their error code, so the native library's English messages are only a
// fallback.
// SPDX-License-Identifier: MIT
using System.Collections.Generic;
using System.Globalization;
using Wjson;

namespace Wjson.Viewer.Core
{
    public static class Strings
    {
        /// <summary>Language used for all text. Defaults to the Windows display language.</summary>
        public static CultureInfo Culture { get; set; } = CultureInfo.CurrentUICulture;

        private static bool Spanish => Culture.TwoLetterISOLanguageName == "es";

        private static readonly Dictionary<string, (string En, string Es)> Text = new()
        {
            ["AppName"] = ("wjson Viewer", "wjson Viewer"),
            ["Untitled"] = ("Untitled", "Sin título"),
            ["Open"] = ("Open", "Abrir"),
            ["Save"] = ("Save", "Guardar"),
            ["SaveAs"] = ("Save as", "Guardar como"),
            ["Format"] = ("Format", "Formatear"),
            ["Minify"] = ("Minify", "Minificar"),
            ["Jsonc"] = ("JSONC", "JSONC"),
            ["JsoncTooltip"] = ("Allow comments and trailing commas", "Permitir comentarios y comas finales"),
            ["PointerPlaceholder"] = ("JSON Pointer, e.g. /items/0/name", "JSON Pointer, p. ej. /items/0/name"),
            ["Go"] = ("Go", "Ir"),
            ["Valid"] = ("Valid JSON", "JSON válido"),
            ["Empty"] = ("Empty document", "Documento vacío"),
            ["GoToError"] = ("Go to error", "Ir al error"),
            ["ErrorAt"] = ("Line {0}, column {1}: {2}", "Línea {0}, columna {1}: {2}"),
            ["Nodes"] = ("{0:N0} values", "{0:N0} valores"),
            ["Characters"] = ("{0:N0} characters", "{0:N0} caracteres"),
            ["Root"] = ("root", "raíz"),
            ["Items"] = ("{0:N0} items", "{0:N0} elementos"),
            ["Members"] = ("{0:N0} members", "{0:N0} miembros"),
            ["CopyPointer"] = ("Copy pointer", "Copiar puntero"),
            ["CopyValue"] = ("Copy value", "Copiar valor"),
            ["PointerNotFound"] = ("No value at {0}", "No hay ningún valor en {0}"),
            ["PointerInvalid"] = ("A JSON Pointer starts with / (or is empty for the whole document)",
                                  "Un JSON Pointer empieza por / (o está vacío para todo el documento)"),
            ["FixErrorsFirst"] = ("Fix the errors first", "Corrige primero los errores"),
            ["UnsavedTitle"] = ("Save changes?", "¿Guardar los cambios?"),
            ["UnsavedMessage"] = ("Do you want to save the changes to {0}?", "¿Quieres guardar los cambios en {0}?"),
            ["DontSave"] = ("Don't save", "No guardar"),
            ["Cancel"] = ("Cancel", "Cancelar"),
            ["LargeFile"] = ("Large file ({0}): opened read-only, validated in streaming mode",
                             "Archivo grande ({0}): abierto en solo lectura y validado en streaming"),
            ["TooLargeForTree"] = ("Too large to show as a tree", "Demasiado grande para mostrarlo como árbol"),
            ["OpenFailed"] = ("Could not open the file", "No se pudo abrir el archivo"),
            ["SaveFailed"] = ("Could not save the file", "No se pudo guardar el archivo"),
            ["Ok"] = ("OK", "Aceptar"),
            ["DropHint"] = ("Open a file or drop it here", "Abre un archivo o suéltalo aquí"),
            ["Compare"] = ("Compare", "Comparar"),
            ["CompareTooltip"] = ("Compare with another file", "Comparar con otro archivo"),
            ["CompareTitle"] = ("Differences with {0}", "Diferencias con {0}"),
            ["CompareEqual"] = ("The documents are equal.", "Los documentos son iguales."),
            ["DiffSummary"] = ("{0:N0} added, {1:N0} removed, {2:N0} changed", "{0:N0} añadidos, {1:N0} eliminados, {2:N0} cambiados"),
            ["DiffTruncated"] = ("Showing the first {0:N0} differences.", "Se muestran las primeras {0:N0} diferencias."),
            ["DiffHint"] = ("Select a difference to show it in the tree.", "Selecciona una diferencia para verla en el árbol."),
            ["CompareNeedsValid"] = ("Fix the errors in the current document before comparing.",
                                     "Corrige los errores del documento actual antes de comparar."),
            ["OtherInvalid"] = ("{0} is not valid JSON: {1}", "{0} no es JSON válido: {1}"),
            ["Close"] = ("Close", "Cerrar"),
        };

        private static readonly Dictionary<JsonErrorCode, string> SpanishErrors = new()
        {
            [JsonErrorCode.UnexpectedEnd] = "fin inesperado del documento",
            [JsonErrorCode.UnexpectedCharacter] = "carácter inesperado",
            [JsonErrorCode.TrailingCharacters] = "contenido inesperado después del valor JSON",
            [JsonErrorCode.ExpectedKey] = "se esperaba una clave entre comillas",
            [JsonErrorCode.ExpectedColon] = "se esperaba ':' después de la clave",
            [JsonErrorCode.ExpectedCommaOrBrace] = "se esperaba ',' o '}'",
            [JsonErrorCode.ExpectedCommaOrBracket] = "se esperaba ',' o ']'",
            [JsonErrorCode.InvalidLiteral] = "literal no válido (se esperaba true, false o null)",
            [JsonErrorCode.InvalidNumber] = "número no válido",
            [JsonErrorCode.NumberOutOfRange] = "número demasiado grande",
            [JsonErrorCode.UnterminatedString] = "string sin cerrar",
            [JsonErrorCode.ControlCharacterInString] = "carácter de control sin escapar dentro de un string",
            [JsonErrorCode.InvalidEscape] = "secuencia de escape no válida",
            [JsonErrorCode.InvalidUnicodeEscape] = "escape \\u no válido (se esperaban 4 dígitos hexadecimales)",
            [JsonErrorCode.InvalidSurrogate] = "par sustituto UTF-16 no válido o incompleto en \\u",
            [JsonErrorCode.InvalidUtf8] = "secuencia UTF-8 no válida",
            [JsonErrorCode.InvalidEncoding] = "codificación de texto no válida",
            [JsonErrorCode.DepthLimitExceeded] = "se superó la profundidad máxima de anidamiento",
            [JsonErrorCode.IoError] = "no se pudo leer el archivo",
            [JsonErrorCode.InvalidComment] = "comentario no válido (se esperaba // o /*)",
            [JsonErrorCode.UnterminatedComment] = "comentario /* sin cerrar",
            [JsonErrorCode.StringTooLong] = "el string supera la longitud máxima",
            [JsonErrorCode.DocumentTooLarge] = "el documento supera el tamaño máximo",
            [JsonErrorCode.Cancelled] = "análisis cancelado",
            [JsonErrorCode.OutOfMemory] = "memoria insuficiente",
            [JsonErrorCode.InvalidArgument] = "argumento no válido",
        };

        public static string Get(string key)
        {
            if (!Text.TryGetValue(key, out var t)) return key;
            return Spanish ? t.Es : t.En;
        }

        public static string Format(string key, params object[] args) => string.Format(Culture, Get(key), args);

        /// <summary>Localized description of a parse error.</summary>
        public static string Error(JsonParseError error)
        {
            if (Spanish && SpanishErrors.TryGetValue(error.Code, out var es)) return es;
            return error.Message;
        }

        /// <summary>Human-readable size, e.g. "12.5 MB".</summary>
        public static string Size(long bytes)
        {
            string[] units = { "B", "KB", "MB", "GB", "TB" };
            double value = bytes;
            var unit = 0;
            while (value >= 1024 && unit < units.Length - 1)
            {
                value /= 1024;
                ++unit;
            }
            return unit == 0 ? $"{bytes} B" : value.ToString("0.#", Culture) + " " + units[unit];
        }
    }
}
