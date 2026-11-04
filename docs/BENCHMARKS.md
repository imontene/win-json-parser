# Benchmarks

## Qué se mide

`wjson_bench` mide el rendimiento en MB/s de cuatro operaciones sobre cada archivo:

| Columna | Operación |
|---|---|
| DOM parse | `wjson::parse(texto)`: construye el árbol `Value` completo. |
| SAX parse | `wjson::parse(texto, handler)` con un `Handler` vacío (validación sin árbol). |
| SAX stream | `wjson::parse_stream()` desde un `std::istream`, en bloques de 64 KiB. |
| Serialize | `wjson::serialize()` del árbol, en formato compacto. |

Cada operación se repite durante al menos un tiempo mínimo y se publica la **ejecución más rápida**, que es la cifra menos ruidosa en máquinas compartidas.

Archivos (conjunto estándar de [nativejson-benchmark](https://github.com/miloyip/nativejson-benchmark), descargados con hash verificado):

- `twitter.json` (0.6 MB): datos de una red social, con mucho texto Unicode.
- `canada.json` (2.3 MB): GeoJSON, dominado por números en coma flotante.
- `citm_catalog.json` (1.7 MB): objetos anidados con muchas claves y enteros.

## Ejecutarlo

```powershell
cmake --preset msvc-x64 -DWJSON_BUILD_BENCHMARKS=ON
cmake --build build/msvc-x64 --config Release --target wjson_bench
python scripts/fetch_benchmark_data.py bench-data
build/msvc-x64/Release/wjson_bench.exe bench-data/twitter.json bench-data/canada.json bench-data/citm_catalog.json
```

Añade `--markdown` para obtener una tabla y `--min-time N` para cambiar los segundos mínimos por operación.

## Resultados

El job `benchmark` del CI publica la tabla en el resumen de cada ejecución en `main` (pestaña *Actions* → ejecución → *Summary*). Al ser máquinas compartidas, conviene comparar tendencias, no cifras sueltas.

### Windows, MSVC x64 Release (runner `windows-2025-vs2026` de GitHub)

Las dos filas de cada archivo se midieron en el mismo tipo de máquina: 1.1.0 y la versión actual.

| File | Versión | DOM parse (MB/s) | SAX parse (MB/s) | SAX stream (MB/s) | Serialize (MB/s) |
|---|---|---:|---:|---:|---:|
| canada.json | 1.1.0 | 51 | 76 | 60 | 213 |
| canada.json | actual | **179** (×3,5) | **379** (×5,0) | **261** (×4,4) | 229 |
| citm_catalog.json | 1.1.0 | 243 | 440 | 226 | 1031 |
| citm_catalog.json | actual | **451** (×1,9) | **900** (×2,0) | **338** (×1,5) | 1035 |
| twitter.json | 1.1.0 | 149 | 268 | 173 | 303 |
| twitter.json | actual | **248** (×1,7) | **451** (×1,7) | **269** (×1,6) | 302 |

### Linux, GCC 13, `-O3` (contenedor de desarrollo, mejor de tres ejecuciones)

| File | Versión | DOM parse (MB/s) | SAX parse (MB/s) | SAX stream (MB/s) | Serialize (MB/s) |
|---|---|---:|---:|---:|---:|
| canada.json | 1.1.0 | 227 | 427 | 342 | 414 |
| canada.json | actual | **268** | **650** | **540** | 426 |
| citm_catalog.json | 1.1.0 | 427 | 926 | 370 | 1842 |
| citm_catalog.json | actual | **607** | **1131** | **578** | 1849 |
| twitter.json | 1.1.0 | 308 | 588 | 421 | 738 |
| twitter.json | actual | **429** | **845** | **523** | 782 |

### Qué cambió

Un perfil con `callgrind` mostró dónde se iba el tiempo; cada cambio ataca uno de esos puntos:

- **Lectura por bloques.** Las dos fuentes de entrada exponen los bytes ya disponibles en un bloque contiguo. Los strings se copian por tramos, buscando su final de 8 en 8 bytes (SWAR, C++ portable). Los espacios se saltan en un bucle cerrado, de 8 en 8 cuando son indentación. Los números se convierten directamente donde están, sin copiarlos byte a byte.
- **Números en coma flotante con [fast_float](https://github.com/fastfloat/fast_float)** (algoritmo Eisel-Lemire), en lugar de `std::from_chars`. Es la causa de la gran mejora en Windows: la implementación de MSVC era el cuello de botella. En Linux la diferencia es menor porque la biblioteca estándar de GCC 12+ ya usa ese algoritmo. Se verificó bit a bit contra `std::from_chars` con 3 millones de números, incluidos subnormales y los límites de desbordamiento.
- **DOM sin realocaciones.** Cada contenedor se reserva una sola vez con su tamaño final, en vez de crecer duplicando su capacidad.

La serialización no cambió; la columna *Serialize* sirve de control de que las máquinas son comparables.

### Memoria en archivos grandes

Con un archivo sintético de 137 MB (2 millones de objetos):

| Operación | Tiempo | Memoria máxima |
|---|---:|---:|
| `wjson validate` (streaming) | 0.64 s | 13 MB |
| `wjson minify` (árbol completo) | 6.7 s | 1.2 GB |

El streaming mantiene la memoria constante sea cual sea el tamaño del archivo. El árbol ocupa unas 9 veces el tamaño del JSON, porque cada valor y cada clave es un objeto independiente en el *heap*. En ese contenedor, la mayor parte del tiempo de la primera pasada se va en fallos de página del sistema operativo al pedir memoria nueva. Reducir ese consumo con un asignador tipo *arena* está en la hoja de ruta.

## Próximos pasos

- Comparar con otras bibliotecas (RapidJSON, nlohmann/json, simdjson) en el mismo job.
- Reducir la memoria del DOM (hoy ~7 veces el tamaño del JSON): un `Value` más compacto o un asignador *arena*.
- SIMD explícito (SSE2/AVX2/NEON) para buscar comillas y escapes en bloques de 16 a 32 bytes.
- Serialización: escribir números y strings sin pasar por buffers intermedios.
