# Changelog

Formato basado en [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/). El proyecto sigue [versionado semántico](https://semver.org/lang/es/): a partir de 1.0.0, la API pública (C++ y C) solo cambia de forma incompatible en una nueva versión mayor.

## [Sin publicar]

### Añadido
- **wjson Viewer**: aplicación de escritorio para Windows (WinUI 3, sin instalación) con editor, validación en vivo, ir al error, árbol coloreado y perezoso, búsqueda con JSON Pointer, formatear y minificar, JSONC (automático con `.jsonc`), conservación de codificación y finales de línea, modo streaming para archivos grandes e interfaz en español e inglés (`--lang`).
- Las releases incluyen `wjson-viewer-<versión>-windows-{x64,x86,arm64}.zip`.

## [1.0.0] - 2026-11-03

### Añadido
- **API en C** (`wjson/wjson.h`) con ABI estable: documentos opacos, validación en streaming, formateo, JSON Pointer y estructuras de opciones versionadas.
- **Biblioteca dinámica** (`-DBUILD_SHARED_LIBS=ON`): `wjson.dll` / `libwjson.so` exportando solo la API pública.
- **Bindings para .NET** (paquete NuGet `Imontene.Wjson`, `netstandard2.0` y `net8.0`) con binarios nativos para win-x64, win-x86, win-arm64 y linux-x64.
- **Módulo de PowerShell** `Wjson` (5.1 y 7) con `Test-WJson`, `ConvertFrom-WJson`, `Format-WJson` y `Get-WJsonValue`.
- **Fuzzing continuo** con libFuzzer y comprobaciones diferenciales (DOM/SAX, memoria/stream, ida y vuelta, API C).
- Flujo de **release**: zips de `wjson.exe` (x64, x86, ARM64), NuGet, módulo de PowerShell, manifiestos de **winget** y sumas SHA-256.
- El port de vcpkg admite también tripletas dinámicas.

### Cambiado
- El paquete CMake declara compatibilidad `SameMajorVersion`.

## [0.5.0]

### Añadido
- Parser en streaming (SAX) con memoria constante y decodificación UTF-16 al vuelo.
- JSONC (comentarios y comas finales), JSON Pointer (RFC 6901) y límites de seguridad.
- CLI: `query`, `--jsonc`, `--raw` y `--max-depth`; `validate` en streaming.
- Benchmarks, paquete CMake (`find_package(wjson)`) y port *overlay* de vcpkg.

## [0.1.0]

### Añadido
- Parser DOM estricto (RFC 8259) con errores por línea y columna, validación UTF-8 y lectura de UTF-16 con BOM.
- Serialización compacta y formateada.
- CLI `wjson.exe` con `validate`, `format` y `minify`.
- CI en Windows (MSVC, clang-cl, MinGW, ARM64) y Linux, con JSONTestSuite.
