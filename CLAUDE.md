# CLAUDE.md

Contexto para trabajar en este repositorio. El usuario escribe en español: respóndele en español. El código, los comentarios y los mensajes de commit van en inglés; la documentación de `docs/`, el README y el CHANGELOG, en español.

## Qué es

**wjson**: parser de JSON en C++17 pensado para Windows (MIT). Publicado en GitHub Releases, NuGet (`Imontene.Wjson`) y PowerShell Gallery (`Wjson`). Versión actual: ver `CMakeLists.txt`.

| Ruta | Contenido |
|---|---|
| `include/wjson/wjson.hpp` | API pública C++ (DOM `Value`, documento compacto `Document`/`ValueRef`, `parse`, `Handler` SAX, `serialize`, JSON Pointer). |
| `include/wjson/wjson.h` | API pública C con ABI estable; base de todos los bindings. |
| `src/reader.hpp` | **La gramática, escrita una sola vez**: plantilla `Reader<Source, Sink>`. |
| `src/sources.hpp` | `MemorySource` (texto completo) y `StreamSource` (por bloques, UTF-16→UTF-8). |
| `src/parser.cpp` | `DomBuilder`, `HandlerSink` y las funciones públicas de parseo. |
| `src/document.cpp` | `Document` compacto: `DocumentBuilder` (otro *sink* de `Reader`) y `ValueRef`. |
| `src/c_api.cpp` | API C sobre la C++. Ninguna excepción cruza la frontera. |
| `src/third_party/fast_float/` | fast_float v8.3.2 *vendorizado* (conversión de doubles). |
| `cli/` | `wjson.exe`: `validate`, `format`, `minify`, `query`, `diff` (`cli/diff.cpp`, probado en `tests/test_diff.cpp`). |
| `bindings/dotnet/` | Binding .NET (P/Invoke). Ensamblado `Imontene.Wjson.dll`. |
| `bindings/powershell/` | Módulo `Wjson` (5.1 y 7) y tests Pester. |
| `gui/` | **wjson Viewer** (WinUI 3): `Wjson.Viewer.Core` (lógica, testeable en cualquier SO), `Wjson.Viewer` (ventana). |
| `tests/` | doctest (C++), `test_c_api.c` (C99), datos, y `tests/package` (consume el paquete instalado). |
| `fuzz/`, `bench/` | Objetivo de libFuzzer y `wjson_bench`. |
| `ports/wjson/` | Port *overlay* de vcpkg. |
| `docs/` | `FEATURES.md` (hoja de ruta y estado), `ARCHITECTURE.md`, `BENCHMARKS.md`, `RELEASING.md`. |

## Compilar y probar

Windows (presets: `msvc-x64`, `msvc-x86`, `msvc-arm64`, `clang-cl-x64`):

```powershell
cmake --preset msvc-x64
cmake --build build/msvc-x64 --config Release
ctest --test-dir build/msvc-x64 -C Release --output-on-failure
```

Linux: presets `linux-gcc` y `linux-clang-sanitize` (ASan + UBSan).

- Opciones CMake: `BUILD_SHARED_LIBS` (DLL), `WJSON_BUILD_TESTS`, `WJSON_BUILD_CLI`, `WJSON_WARNINGS_AS_ERRORS` (los presets la activan), `WJSON_SANITIZE`, `WJSON_BUILD_FUZZERS`, `WJSON_BUILD_BENCHMARKS`, `WJSON_INSTALL`.
- Los tests descargan doctest al configurar (hash fijado). Sin conexión: `-DWJSON_DOCTEST_INCLUDE_DIR=<carpeta>`.
- Conformidad: `python scripts/run_jsontestsuite.py <wjson.exe> <JSONTestSuite>/test_parsing` (todos los `y_` y `n_` deben pasar).
- .NET: `dotnet test bindings/dotnet/tests/Wjson.Tests -p:WjsonNativeLibrary=<ruta a wjson.dll>` (DLL compilada con `-DBUILD_SHARED_LIBS=ON`).
- PowerShell: `bindings/powershell/build.ps1 -NativeRoot <dir con win-x64/native/wjson.dll> -OutputPath out/Wjson`, luego `Invoke-Pester bindings/powershell/tests` con `$env:WJSON_MODULE_PATH` apuntando al módulo.
- GUI: abrir `gui/Wjson.Viewer/Wjson.Viewer.csproj` en Visual Studio (plataforma x64) o
  `dotnet publish gui/Wjson.Viewer -c Release -r win-x64 -p:Platform=x64`.
  Por defecto usa el NuGet publicado; con `-p:WjsonLocalNative=<wjson.dll>` usa el binding y la DLL del repositorio.
  Tests de la lógica: `dotnet test gui/Wjson.Viewer.Core.Tests` (funcionan también en Linux).
  Prueba de humo con capturas: `gui/tests/smoke.ps1 -Exe <WjsonViewer.exe> -Samples gui/samples`.
  Incluye `WjsonViewer.exe gui/samples/sample.json --self-test log.txt` (`MainWindow.SelfTest.cs`): comprobaciones del editor dentro de la app.

## Convenciones

- C++17, compatible también con C++20 (MinGW compila en C++20 por defecto): nada de `std::filesystem::u8path` ni `u8""` donde importe `char8_t`.
- Todo debe compilar sin warnings con `-Werror` / `/WX` en MSVC, clang-cl, MinGW, GCC y Clang (`-Wconversion -Wsign-conversion -Wold-style-cast`; `-Wshadow` solo en Clang).
- La API pública sigue semver desde 1.0: no romper `wjson.hpp` ni `wjson.h` sin versión mayor. Los `ErrorCode` y su espejo en C (`wjson_error_code`) son estables; `c_api.cpp` comprueba con `static_assert` que coinciden. Los códigos nuevos van al final.
- Las estructuras de opciones de la API C llevan `struct_size`; los campos nuevos se añaden al final y se leen solo si caben en `struct_size`.
- Cualquier cambio en la gramática va en `reader.hpp` y vale para DOM y streaming a la vez. Los tests comparan ambos caminos (también con buffers de 4 bytes) y el fuzzer lo comprueba de forma continua.
- Las rutas rápidas (`available()`, `skip_plain`, `skip_bytes`) nunca generan errores: ante cualquier cosa rara se vuelve al camino byte a byte, que es el único que reporta errores. Así códigos y posiciones no cambian.
- Las posiciones de error son 1-based y las columnas cuentan *code points*.
- Cambios visibles para el usuario: entrada en `CHANGELOG.md` (sección "Sin publicar"), y actualizar `docs/FEATURES.md` si cambia el estado de una feature.

## Versiones y releases

- La versión vive en **4 sitios**: `CMakeLists.txt`, `ports/wjson/vcpkg.json`, `bindings/dotnet/src/Wjson/Wjson.csproj` y `bindings/powershell/Wjson/Wjson.psd1`. `python scripts/check_versions.py` comprueba que coinciden (el flujo de release lo exige). La GUI toma la versión de la etiqueta.
- Proceso (detalle en `docs/RELEASING.md`): subir versión en los 4 sitios, fechar el CHANGELOG, commit, esperar CI verde, opcionalmente ensayo con *Actions → Release → Run workflow* (no publica), y `git tag vX.Y.Z && git push origin vX.Y.Z`.
- La etiqueta dispara `release.yml`: GitHub Release (zips del CLI y del visor x64/x86/ARM64, NuGet, módulo, manifiestos de winget, `SHA256SUMS.txt`) y publicación en NuGet y PowerShell Gallery con los secretos `NUGET_API_KEY` y `PSGALLERY_API_KEY`.
- `publish.yml` republica en NuGet/Gallery los paquetes de una release ya existente.
- winget es manual: `wingetcreate submit` con los manifiestos de la release (todavía no se ha enviado ninguna versión).
- Publicar es irreversible: confirma con el usuario antes de etiquetar o publicar.

## CI (GitHub Actions)

- `ci.yml`: MSVC x64/x86/ARM64 (ARM64 solo compila), DLL, clang-cl, MinGW, Linux GCC (+ biblioteca compartida) y Clang con ASan/UBSan, JSONTestSuite, paquete CMake, port vcpkg (dinámico y estático), .NET + PowerShell (Pester en 7 y prueba en 5.1) y benchmark de Windows (resultados en el resumen del run).
- `gui.yml`: compila el visor, tests de la lógica y prueba de humo que abre la app y saca capturas (impresas en base64 en el log, líneas `SHOT:`).
- `fuzz.yml`: libFuzzer 2 min por push y 30 min cada noche, con corpus persistente en la caché.

## Problemas ya resueltos (no repetir)

- **GUI sin empaquetar**: necesita `EnableMsixTooling=true` en el `.csproj` para generar `resources.pri`; sin él los controles WinUI (p. ej. `TreeView`) fallan al cargar con `XamlParseException` genérica.
- **Fuentes en XAML**: se ponen directamente (`FontFamily="Cascadia Mono, Consolas"`); un `StaticResource` propio de `FontFamily` en `App.xaml` hacía fallar la carga.
- **Diagnóstico de la GUI**: las excepciones no controladas se escriben en `%LOCALAPPDATA%\wjson\viewer.log` (o `WJSON_VIEWER_LOG`). Si falla la carga de `MainWindow.xaml`, `App.ProbeXaml` registra qué elemento falla.
- **Editor del visor** (`RichEditBox`): guarda los saltos de línea como `\r` (`DocumentFile.Save` restaura LF/CRLF y la codificación); en solo lectura ignora `SetText`; tiene `UndoLimit = 0` porque colorear ensuciaría su historial (el historial es `EditHistory`); los colores se cargan como RTF y se repintan por líneas (`JsonSyntax`).
- **Ensamblado .NET**: se llama `Imontene.Wjson.dll`, no `Wjson.dll`, porque en Windows chocaría con el nativo `wjson.dll` (mayúsculas indistintas).
- **Runtime de MSVC**: la biblioteca usa `/MD` (lo que esperan los consumidores); solo `wjson.exe` usa `/MT`, compilando dentro las fuentes de la biblioteca. La DLL para NuGet se compila con `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`.
- **PDB**: la biblioteca usa `COMPILE_PDB_NAME wjson_lib` para no pisar `wjson.pdb` del CLI en Debug.
- **GCC 15**: falsos positivos de `-Wmaybe-uninitialized` dentro de `std::variant`; están como warning, no error.
- **Escapes `\u` en tests de C++**: en strings crudos `R"(...)"` no se procesan, y algunas herramientas convierten texto con `\u` seguido de 4 dígitos hexadecimales en el carácter literal. En los tests, escribe los escapes JSON como `"\\u00XX"` en strings normales y revisa el archivo con `grep` tras editarlo.
- **`dotnet nuget push`** no expande `dist/*.nupkg` en Windows: se le pasa la ruta completa del paquete.
- **Secretos en Actions**: deben ir en el `env` del job; el `env` de un paso no es visible en el `if` de ese mismo paso.
- **fast_float**: vendorizado en `src/third_party` como directorio de include *SYSTEM* (no le aplican nuestros warnings). Para actualizarlo: nueva versión de al menos dos semanas, verificar hashes, actualizar `src/third_party/fast_float/README.md` y `THIRD_PARTY_NOTICES.md` (que viaja en todos los paquetes por la licencia MIT).

## Rendimiento

- Medir con `wjson_bench` (`-DWJSON_BUILD_BENCHMARKS=ON`, datos con `scripts/fetch_benchmark_data.py`); el ruido es de ±15 %, compara el mejor de varias ejecuciones. Para perfilar, `valgrind --tool=callgrind` en Linux.
- Las comparaciones entre versiones en Windows deben hacerse en la misma imagen del runner de CI; la columna *Serialize* sirve de control.
- Cambios en conversión de números: verificar bit a bit contra `std::from_chars` (GCC) con millones de números aleatorios, subnormales y límites de desbordamiento.

## Pendiente

Ver `docs/FEATURES.md`. Lo más inmediato: enviar winget, firma de código de los ejecutables, porcentaje de progreso al abrir archivos enormes (necesita un callback de progreso en la API C), `--in-place` en `format`/`minify` y salida de `diff` como JSON Patch.
