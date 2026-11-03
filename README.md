# win-json-parser

[![CI](https://github.com/imontene/win-json-parser/actions/workflows/ci.yml/badge.svg)](https://github.com/imontene/win-json-parser/actions/workflows/ci.yml)

Parser de JSON rápido, robusto y conforme a estándares, escrito en C++17 y diseñado para Windows.

> 🚧 Proyecto en fase inicial (v0.1.0).

## Estado actual

- Parser estricto conforme a **RFC 8259**: pasa todos los casos `y_` y `n_` de [JSONTestSuite](https://github.com/nst/JSONTestSuite).
- Errores con **línea, columna y fragmento** del texto donde falla.
- Validación de **UTF-8** y lectura de archivos **UTF-8 con o sin BOM y UTF-16 LE/BE** (los que genera el Bloc de notas o PowerShell).
- Límite de profundidad configurable, para evitar desbordamientos de pila con entradas maliciosas.
- Serialización compacta o formateada, con opción de escapar todo lo que no sea ASCII.
- Herramienta de línea de comandos `wjson.exe`.

Consulta la hoja de ruta en [docs/FEATURES.md](docs/FEATURES.md) y el diseño en [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Compilar

Requisitos: CMake ≥ 3.21 y uno de estos compiladores: Visual Studio 2022 o posterior (MSVC o clang-cl), MinGW-w64 GCC ≥ 11, o GCC/Clang en Linux.

```powershell
cmake --preset msvc-x64
cmake --build build/msvc-x64 --config Release
ctest --test-dir build/msvc-x64 -C Release --output-on-failure
```

Otros presets: `msvc-x86`, `msvc-arm64`, `clang-cl-x64`, `linux-gcc` y `linux-clang-sanitize`. También puedes abrir la carpeta directamente con Visual Studio, que detecta `CMakePresets.json`.

Los tests descargan [doctest](https://github.com/doctest/doctest) (una sola cabecera, con hash verificado) al configurar. Para compilar sin conexión, pasa `-DWJSON_DOCTEST_INCLUDE_DIR=<carpeta>` o desactiva los tests con `-DWJSON_BUILD_TESTS=OFF`.

## Uso de la CLI

```text
wjson validate config.json otro.json      # código de salida 0 = válido, 1 = inválido
wjson format --indent 4 data.json         # formatear
wjson minify data.json > data.min.json    # compactar
type data.json | wjson format             # leer desde stdin
```

Ejemplo de error:

```text
config.json:3:14: error: unexpected character
    "b": [1, 2,]
               ^
```

## Uso de la biblioteca

```cpp
#include <wjson/wjson.hpp>

auto result = wjson::parse_file(L"C:\\datos\\config.json");
if (!result) {
    const auto& e = result.error;
    std::printf("Error en %zu:%zu: %s\n", e.line, e.column, e.message());
    return;
}
if (const wjson::Value* name = result.value->find("nombre"))
    std::puts(name->as_string().c_str());

std::string pretty = wjson::serialize(*result.value, {/*indent=*/2});
```

Para integrarla en tu proyecto CMake:

```cmake
add_subdirectory(win-json-parser)
target_link_libraries(mi_app PRIVATE wjson::wjson)
```

## Conformidad

```bash
git clone https://github.com/nst/JSONTestSuite
python scripts/run_jsontestsuite.py build/msvc-x64/Release/wjson.exe JSONTestSuite/test_parsing
```

## Licencia

[MIT](LICENSE).
