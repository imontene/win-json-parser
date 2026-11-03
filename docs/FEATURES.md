# Sugerencias de features — win-json-parser

Este documento recoge las funcionalidades propuestas para un parser de JSON orientado a Windows, priorizadas por fases. Sirve como hoja de ruta inicial y está abierto a discusión.

**Prioridades:** 🟢 Imprescindible (MVP) · 🟡 Importante (v1.x) · 🔵 Deseable (futuro)

---

## 1. Núcleo del parser

| Feature | Prioridad | Notas |
|---|---|---|
| Cumplimiento estricto de **RFC 8259 / ECMA-404** | 🟢 | Validado contra la suite [JSONTestSuite](https://github.com/nst/JSONTestSuite) (casos `y_`, `n_`, `i_`). |
| Árbol DOM (lectura completa a memoria) | 🟢 | API simple: `Parse(text) → JsonValue`. |
| Mensajes de error precisos | 🟢 | Línea, columna, offset en bytes y un fragmento del texto con el error señalado (`^`). |
| Manejo correcto de Unicode | 🟢 | UTF-8 y UTF-16 (LE/BE), detección y eliminación de **BOM**, pares sustitutos en `\uXXXX`, rechazo de UTF-8 inválido (o reemplazo configurable). |
| Números de alta precisión | 🟢 | Distinguir enteros de 64 bits de doubles; opción para conservar el número como texto (decimales financieros, `BigInteger`). |
| Límites configurables | 🟢 | Profundidad máxima de anidamiento, tamaño máximo de documento y de string, para evitar desbordamientos de pila y ataques DoS. |
| Parser en streaming (estilo SAX / pull reader) | 🟡 | Procesar archivos de varios GB con memoria constante. |
| Claves duplicadas | 🟡 | Política configurable: error, conservar primera, conservar última. |
| Modo tolerante / extensiones | 🟡 | Opcionales y desactivadas por defecto: comentarios `//` y `/* */`, comas finales, **JSONC** (usado por VS Code y Windows Terminal), **JSON5**. |
| NDJSON / JSON Lines | 🟡 | Leer y escribir un documento por línea (logs, exportaciones). |
| Parsing incremental / asíncrono | 🔵 | Alimentar el parser por fragmentos (red, pipes). |

## 2. Rendimiento

| Feature | Prioridad | Notas |
|---|---|---|
| Benchmarks reproducibles desde el día 1 | 🟢 | Comparar con alternativas de referencia (p. ej. simdjson, RapidJSON, System.Text.Json) con archivos estándar (`twitter.json`, `canada.json`, `citm_catalog.json`). |
| Parsing sin copias (*zero-copy*) | 🟡 | Las strings sin escapes apuntan al buffer original. |
| Aceleración SIMD (SSE4.2 / AVX2 / AVX-512, ARM64 NEON) | 🔵 | Con detección de CPU en tiempo de ejecución y *fallback* escalar. |
| Archivos mapeados en memoria | 🟡 | `CreateFileMapping` / `MapViewOfFile` para archivos grandes. |
| Asignador de memoria configurable | 🔵 | Arena / *pool allocator* para reducir fragmentación. |

## 3. Integración específica con Windows

Es lo que diferenciaría el proyecto de un parser genérico.

| Feature | Prioridad | Notas |
|---|---|---|
| Soporte de `wchar_t` / `std::wstring` / `LPCWSTR` | 🟢 | Entrada y salida nativa en UTF-16, que es la codificación de las APIs de Windows. |
| Rutas largas y Unicode | 🟢 | Abrir archivos con `CreateFileW`, soporte para rutas `\\?\` de más de 260 caracteres. |
| Compatibilidad con MSVC, clang-cl y MinGW | 🟢 | Arquitecturas x64, x86 y **ARM64** (Windows on ARM). |
| Distribución en **vcpkg** y **NuGet** | 🟡 | Biblioteca estática y DLL, con símbolos (`.pdb`). |
| Bindings para **.NET** (C#) | 🟡 | P/Invoke o C++/CLI. |
| Módulo de **PowerShell** | 🟡 | Cmdlets tipo `ConvertFrom-FastJson`, más rápido y con mejores errores que `ConvertFrom-Json`. |
| Componente **WinRT / COM** | 🔵 | Uso desde VBA, VBScript y apps UWP/WinUI. |
| Conversión JSON ↔ Registro de Windows | 🔵 | Exportar/importar claves del registro como JSON. |
| Eventos ETW / integración con logs | 🔵 | Diagnóstico de rendimiento con Windows Performance Analyzer. |

## 4. API y usabilidad

| Feature | Prioridad | Notas |
|---|---|---|
| API en **C** con ABI estable | 🟢 | Facilita bindings desde cualquier lenguaje. |
| *Wrapper* moderno en **C++17/20** | 🟢 | Iteradores, `std::string_view`, `std::optional`, `operator[]`. |
| Serialización (*writer*) | 🟢 | Salida compacta y formateada (*pretty-print*), indentación configurable, opción de escapar no-ASCII. |
| Consultas **JSON Pointer** (RFC 6901) | 🟡 | `doc.at("/users/0/name")`. |
| **JSONPath** | 🔵 | Consultas tipo `$.store.book[*].author`. |
| Mapeo a structs / clases | 🟡 | Macros o reflexión para (de)serializar tipos de usuario. |
| **JSON Patch** (RFC 6902) y **Merge Patch** (RFC 7396) | 🔵 | Aplicar y generar diferencias. |
| Validación con **JSON Schema** (draft 2020-12) | 🔵 | Muy útil para archivos de configuración. |
| Orden de claves preservado | 🟡 | Opción para mantener el orden de inserción. |

## 5. Herramienta de línea de comandos (`wjson.exe`)

| Feature | Prioridad | Notas |
|---|---|---|
| `wjson validate <archivo>` | 🟢 | Código de salida distinto de 0 si el JSON no es válido; errores claros. |
| `wjson format` / `wjson minify` | 🟢 | Formatear o compactar, en sitio o hacia stdout. |
| Lectura desde stdin y pipes | 🟢 | `type data.json \| wjson query /items/0`. |
| `wjson query <pointer>` | 🟡 | Extraer valores (alternativa ligera a `jq`). |
| `wjson diff a.json b.json` | 🔵 | Diferencias semánticas, ignorando formato y orden de claves. |
| `wjson convert` | 🔵 | JSON ↔ YAML / CSV / XML. |
| Salida con colores | 🟡 | Usando secuencias VT de la consola de Windows (Windows Terminal). |
| Ejecutable único sin dependencias | 🟢 | Fácil de distribuir con **winget** y **Scoop**. |

## 6. Calidad, seguridad y mantenimiento

| Feature | Prioridad | Notas |
|---|---|---|
| Tests unitarios + JSONTestSuite en CI | 🟢 | GitHub Actions con runners `windows-latest` (y Linux para portabilidad). |
| *Fuzzing* continuo | 🟡 | libFuzzer con clang-cl; candidato a OSS-Fuzz. |
| AddressSanitizer / análisis estático | 🟡 | `/fsanitize=address` de MSVC, `/analyze`, clang-tidy. |
| Cobertura de código | 🟡 | Informe publicado en cada PR. |
| Documentación | 🟢 | README con ejemplos, referencia de la API generada (Doxygen) y guía de migración desde otras bibliotecas. |
| Versionado semántico y CHANGELOG | 🟢 | Releases con binarios firmados (*code signing*) para evitar avisos de SmartScreen. |
| Licencia permisiva | 🟢 | MIT o Apache-2.0 para facilitar la adopción. |

---

## Hoja de ruta propuesta

1. **v0.1 – MVP:** parser DOM conforme a RFC 8259, errores con línea/columna, UTF-8/UTF-16 con BOM, writer con pretty-print, API C + C++, CLI `validate`/`format`/`minify`, CI en Windows con JSONTestSuite.
2. **v0.5:** parser en streaming, JSONC, JSON Pointer, límites de seguridad, benchmarks publicados, paquete vcpkg.
3. **v1.0:** API estable, NuGet + bindings .NET, módulo de PowerShell, fuzzing continuo, distribución por winget.
4. **Futuro:** SIMD, JSON Schema, JSONPath, JSON Patch, COM/WinRT, conversión de formatos.

## Decisiones tomadas

- **Lenguaje de implementación:** C++17, compilado con CMake (MSVC, clang-cl y MinGW).
- **Objetivo principal:** una biblioteca reutilizable, con la CLI como primera interfaz y una GUI posible más adelante (ver [ARCHITECTURE.md](ARCHITECTURE.md)).

## Decisiones abiertas

- **Tecnología de la GUI:** WinUI 3, Win32, Qt o Dear ImGui.
- **Licencia:** MIT o Apache-2.0.
- **Portabilidad:** ¿exclusivamente Windows o multiplataforma con extras específicos de Windows? Lo segundo amplía la base de usuarios sin perder el enfoque.
