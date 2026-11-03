# Arquitectura

## Principio: un núcleo, varias interfaces

Toda la lógica JSON vive en la biblioteca `wjson`. Las interfaces de usuario (la CLI de hoy, una GUI o bindings en el futuro) son capas delgadas que solo leen la entrada, llaman a la biblioteca y muestran el resultado.

```
                ┌──────────────────────────────────────────┐
                │           Biblioteca  wjson              │
                │  DOM: parse · parse_file · serialize     │
                │  SAX: Handler · parse_stream · JSONC     │
                │  JSON Pointer · límites de seguridad     │
                │  ParseError {code, line, column, offset} │
                └───────────────┬──────────────────────────┘
          ┌─────────────────────┼──────────────────────┬───────────────────┐
          │                     │                      │                   │
   ┌──────▼──────┐    ┌─────────▼────────┐   ┌─────────▼────────┐  ┌───────▼───────┐
   │ wjson.exe   │    │ GUI (futuro)     │   │ API C → .NET →   │  │ Tu aplicación │
   │ (cli/)      │    │ (gui/)           │   │ PowerShell       │  │ (vcpkg/NuGet) │
   └─────────────┘    └──────────────────┘   └──────────────────┘  └───────────────┘
```

## Estructura del repositorio

| Ruta | Contenido |
|---|---|
| `include/wjson/wjson.hpp` | API pública en C++. |
| `include/wjson/wjson.h` | API pública en C, con ABI estable (base de los bindings). |
| `src/` | Implementación (ver abajo). |
| `cli/` | Herramienta `wjson.exe` (`validate`, `format`, `minify`, `query`). |
| `tests/` | Tests unitarios (doctest), datos de prueba y `tests/package/` (proyecto que consume el paquete instalado). |
| `bench/` | Benchmark de rendimiento `wjson_bench`. |
| `fuzz/` | Objetivo de fuzzing (libFuzzer) y driver para reproducir entradas. |
| `bindings/dotnet/` | Binding .NET (P/Invoke sobre la API en C) y sus tests. |
| `bindings/powershell/` | Módulo de PowerShell `Wjson` y sus tests (Pester). |
| `ports/wjson/` | Port *overlay* de vcpkg. |
| `scripts/` | Ejecutor de JSONTestSuite y descarga de los datos de benchmark. |
| `cmake/` | Opciones de compilador y plantilla del paquete CMake. |
| `.github/workflows/` | Integración continua. |

## Cómo está organizado el parser

La gramática JSON está escrita **una sola vez** (`src/reader.hpp`) como plantilla sobre dos piezas intercambiables:

```
     Fuente (Source)                 Gramática                 Destino (Sink)
 ┌──────────────────────┐      ┌────────────────────┐      ┌──────────────────────┐
 │ MemorySource         │      │ Reader<Src, Sink>  │      │ DomBuilder → Value   │
 │   texto en memoria   │ ───► │  JSON + JSONC      │ ───► │                      │
 │ StreamSource         │      │  límites, errores  │      │ HandlerSink → tu     │
 │   std::istream por   │      │                    │      │   wjson::Handler     │
 │   bloques, UTF-16→8  │      │                    │      │                      │
 └──────────────────────┘      └────────────────────┘      └──────────────────────┘
```

- **Fuentes** (`src/sources.hpp`). `MemorySource` lee un texto completo y calcula la línea y la columna solo cuando hay un error. `StreamSource` lee bloques de tamaño fijo (`stream_buffer_size`), convierte UTF-16 a UTF-8 al vuelo y lleva la cuenta de línea y columna mientras avanza.
- **Destinos** (`src/parser.cpp`). `DomBuilder` construye el árbol `Value`; `HandlerSink` reenvía los eventos al `wjson::Handler` del usuario.

Así, cualquier cambio en la gramática (por ejemplo, JSON5) se aplica a la vez al DOM y al streaming. Los tests comprueban que ambos caminos producen exactamente los mismos eventos y errores, incluso con buffers de 4 bytes que cortan tokens a la mitad.

## Decisiones de diseño que facilitan una futura GUI

- **La biblioteca no escribe en consola ni termina el proceso.** Devuelve resultados (`ParseResult`) y nunca llama a `exit()` ni imprime nada, así que se puede usar dentro de una ventana sin efectos secundarios.
- **Errores estructurados.** `ParseError` trae un `ErrorCode` estable, la línea, la columna y el offset en bytes. Una GUI puede subrayar el error en un editor, saltar a la posición o traducir el mensaje (los textos en inglés de `message()` son solo un valor por defecto).
- **Columnas en *code points*.** Coinciden con lo que ve el usuario en un editor aunque haya acentos o emojis.
- **Objetos que conservan el orden.** Un visor en árbol muestra las claves tal y como aparecen en el archivo.
- **Sin estado global.** Se pueden parsear varios documentos en paralelo, por ejemplo en un hilo de fondo para que la ventana no se congele.

## Cómo se añadiría la GUI

1. Crear `gui/` con su propio `CMakeLists.txt` y una opción `WJSON_BUILD_GUI` (desactivada por defecto) que enlace con `wjson::wjson`, igual que la CLI.
2. Elegir la tecnología. Estas son las candidatas:

   | Opción | Ventajas | Inconvenientes |
   |---|---|---|
   | **WinUI 3** (Windows App SDK, C++/WinRT) | Aspecto nativo de Windows 11, Fluent Design, distribución MSIX. | Solo Windows; requiere Visual Studio y su toolchain. |
   | **Win32 + Direct2D** | Ejecutable mínimo, sin dependencias. | Mucho más código para una interfaz moderna. |
   | **Qt 6** | Multiplataforma, widgets de árbol y editor ya hechos. | Dependencia grande; licencia LGPL/comercial. |
   | **Dear ImGui** | Muy rápido de desarrollar; ideal para herramientas internas. | Aspecto poco nativo. |

   Para un visor/editor pensado para Windows, **WinUI 3** es la opción más natural.
3. Funcionalidades típicas de la GUI: vista en árbol, editor con resaltado y errores en vivo, formatear y minificar, búsqueda con JSON Pointer, comparación de dos archivos y apertura de archivos grandes.

La GUI se apoyará en features que ya están en la hoja de ruta ([FEATURES.md](FEATURES.md)): el **parser en streaming** e **incremental** para archivos de varios GB sin bloquear la interfaz, y callbacks de **progreso y cancelación**.

## Capas para otros lenguajes

```
 PowerShell (Wjson.psm1)  →  .NET (Imontene.Wjson.dll, P/Invoke)  →  API en C (wjson.dll)  →  núcleo C++
```

- La **API en C** (`src/c_api.cpp`) es la única frontera binaria estable. No deja pasar excepciones, todo el texto es UTF-8 y las estructuras de opciones llevan su propio tamaño (`struct_size`), así que se pueden añadir campos sin romper programas ya compilados.
- La **DLL** exporta solo los símbolos marcados con `WJSON_API`; el resto queda oculto (`-fvisibility=hidden` en GCC/Clang).
- El binding de **.NET** busca la biblioteca nativa en `runtimes/<rid>/native/` junto al ensamblado (la estructura de NuGet) o en la ruta indicada en `WJson.NativeLibraryPath`. El ensamblado se llama `Imontene.Wjson.dll` y no `Wjson.dll` porque en Windows chocaría con `wjson.dll`, al no distinguir el sistema de archivos mayúsculas de minúsculas.
- El módulo de **PowerShell** carga el ensamblado `net8.0` en PowerShell 7 y el `netstandard2.0` en Windows PowerShell 5.1.
