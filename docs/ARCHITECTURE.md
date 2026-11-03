# Arquitectura

## Principio: un núcleo, varias interfaces

Toda la lógica JSON vive en la biblioteca `wjson`. Las interfaces de usuario (la CLI de hoy, una GUI o bindings en el futuro) son capas delgadas que solo leen la entrada, llaman a la biblioteca y muestran el resultado.

```
                ┌──────────────────────────────────────────┐
                │           Biblioteca  wjson              │
                │  parse · parse_file · serialize          │
                │  decode_text (UTF-8 / UTF-16 + BOM)      │
                │  ParseError {code, line, column, offset} │
                └───────────────┬──────────────────────────┘
          ┌─────────────────────┼──────────────────────┬───────────────────┐
          │                     │                      │                   │
   ┌──────▼──────┐    ┌─────────▼────────┐   ┌─────────▼────────┐  ┌───────▼───────┐
   │ wjson.exe   │    │ GUI (futuro)     │   │ Bindings .NET /  │  │ Tu aplicación │
   │ (cli/)      │    │ (gui/)           │   │ PowerShell       │  │ (vcpkg/NuGet) │
   └─────────────┘    └──────────────────┘   └──────────────────┘  └───────────────┘
```

## Estructura del repositorio

| Ruta | Contenido |
|---|---|
| `include/wjson/wjson.hpp` | API pública (única cabecera que incluyen los usuarios). |
| `src/` | Implementación: `parser.cpp`, `writer.cpp`, `encoding.cpp`, `value.cpp` y utilidades internas (`utf8.hpp`). |
| `cli/` | Herramienta `wjson.exe` (`validate`, `format`, `minify`). |
| `tests/` | Tests unitarios (doctest) y datos de prueba. |
| `scripts/` | Utilidades, por ejemplo el ejecutor de JSONTestSuite. |
| `cmake/` | Módulos CMake (opciones de compilador y warnings). |
| `.github/workflows/` | Integración continua. |

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
