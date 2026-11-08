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
   │ wjson.exe   │    │ wjson Viewer     │   │ API C → .NET →   │  │ Tu aplicación │
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
| `gui/` | wjson Viewer, la aplicación de escritorio (WinUI 3), su lógica y sus tests. |
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

## Dos representaciones del documento

| | `Value` (`parse`) | `Document` (`parse_document`) |
|---|---|---|
| Uso | Modificable: se puede editar y construir desde código | Solo lectura |
| Memoria | ~7 veces el JSON | ~3 veces el JSON |
| Estructura | Árbol de `std::variant` con `std::string` y `std::vector` | Un array de nodos de 16 bytes, un array de enlaces por tipo de contenedor y un único buffer de texto |
| Acceso | Referencias a `std::string`, `Array` y `Object` | `ValueRef` (puntero al documento + índice): `as_string()` devuelve un `string_view` dentro del buffer |

En `Document` los nodos están en orden de documento, así que la raíz es el nodo 0. Cada contenedor guarda cuántos hijos tiene y dónde empieza su bloque de enlaces, que es contiguo, de modo que el elemento *i* se encuentra en tiempo constante. Lo construye `src/document.cpp` como un *sink* más de la misma gramática (`Reader`), igual que `DomBuilder`, así que los errores y sus posiciones son idénticos a los de `parse()`. Los tests y el fuzzer comprueban que ambos dan exactamente el mismo resultado.

## Decisiones de diseño que facilitan la GUI

- **La biblioteca no escribe en consola ni termina el proceso.** Devuelve resultados (`ParseResult`) y nunca llama a `exit()` ni imprime nada, así que se puede usar dentro de una ventana sin efectos secundarios.
- **Errores estructurados.** `ParseError` trae un `ErrorCode` estable, la línea, la columna y el offset en bytes. Una GUI puede subrayar el error en un editor, saltar a la posición o traducir el mensaje (los textos en inglés de `message()` son solo un valor por defecto).
- **Columnas en *code points*.** Coinciden con lo que ve el usuario en un editor aunque haya acentos o emojis.
- **Objetos que conservan el orden.** Un visor en árbol muestra las claves tal y como aparecen en el archivo.
- **Sin estado global.** Se pueden parsear varios documentos en paralelo, por ejemplo en un hilo de fondo para que la ventana no se congele.

## La GUI: wjson Viewer

La GUI es una aplicación **WinUI 3** (Windows App SDK 1.8) escrita en C#, que usa la biblioteca a través del binding de .NET:

```
 WjsonViewer.exe (WinUI 3)  →  Wjson.Viewer.Core  →  Imontene.Wjson (.NET)  →  wjson.dll
```

| Proyecto | Contenido |
|---|---|
| `gui/Wjson.Viewer.Core` | Lógica sin interfaz, que se prueba en cualquier sistema: árbol perezoso (los hijos se crean al expandir, y los contenedores de más de 1000 elementos se dividen en rangos), navegación por JSON Pointer, conversión de línea y columna a posición en el texto, carga y guardado que respetan codificación y finales de línea, modo streaming para archivos grandes, textos en español e inglés (incluidos los errores del parser, traducidos por su código) y un serializador para copiar nodos. |
| `gui/Wjson.Viewer.Core.Tests` | Tests de xUnit de la lógica. |
| `gui/Wjson.Viewer` | La ventana (XAML y code-behind): editor, árbol, barra de estado, abrir, guardar y arrastrar archivos. |
| `gui/tests/smoke.ps1` | Prueba de humo del CI: abre la app con archivos de ejemplo, comprueba que la ventana aparece y captura pantallas. |

Algunos detalles:

- **Aplicación sin empaquetar y autocontenida**: no necesita instalar .NET ni el runtime de Windows App SDK; basta con descomprimir el zip. `EnableMsixTooling` debe seguir activado, porque genera el `resources.pri` del que dependen los controles de WinUI.
- **Validación en vivo**: el texto se valida en segundo plano 350 ms después de la última pulsación. Cuando hay un error, el árbol conserva la última versión válida, atenuada.
- **Archivos grandes** (más de 8 MB): se abren en solo lectura y se validan en streaming, sin cargar el texto en el editor. A partir de 256 MB tampoco se construye el árbol.
- **Finales de línea**: el `TextBox` de WinUI guarda los saltos de línea como `\r`. Al guardar se restauran los del archivo original (LF o CRLF).
- **Idioma**: el de Windows, o el que se indique con `--lang es` / `--lang en`.

- **Comparación** (`JsonDiff` en `Wjson.Viewer.Core`): objetos por clave, arrays alineados con la subsecuencia común más larga (sobre identificadores de una forma canónica de cada elemento, para comparar enteros y no subárboles) y números por valor. A partir de 4 millones de celdas de alineación, los arrays se comparan por posición; los resultados se limitan a 10 000.

Pendiente: resaltado de sintaxis en el editor, enlace entre la selección del árbol y el texto, y progreso y cancelación al abrir archivos muy grandes.

## Capas para otros lenguajes

```
 PowerShell (Wjson.psm1)  →  .NET (Imontene.Wjson.dll, P/Invoke)  →  API en C (wjson.dll)  →  núcleo C++
```

- La **API en C** (`src/c_api.cpp`) es la única frontera binaria estable. No deja pasar excepciones, todo el texto es UTF-8 y las estructuras de opciones llevan su propio tamaño (`struct_size`), así que se pueden añadir campos sin romper programas ya compilados.
- La **DLL** exporta solo los símbolos marcados con `WJSON_API`; el resto queda oculto (`-fvisibility=hidden` en GCC/Clang).
- El binding de **.NET** busca la biblioteca nativa en `runtimes/<rid>/native/` junto al ensamblado (la estructura de NuGet) o en la ruta indicada en `WJson.NativeLibraryPath`. El ensamblado se llama `Imontene.Wjson.dll` y no `Wjson.dll` porque en Windows chocaría con `wjson.dll`, al no distinguir el sistema de archivos mayúsculas de minúsculas.
- El módulo de **PowerShell** carga el ensamblado `net8.0` en PowerShell 7 y el `netstandard2.0` en Windows PowerShell 5.1.
