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

### Linux, GCC 13, `-O3` (contenedor de desarrollo, v0.5.0)

| File | Size (MB) | DOM parse (MB/s) | SAX parse (MB/s) | SAX stream (MB/s) | Serialize (MB/s) |
|---|---:|---:|---:|---:|---:|
| canada.json | 2.25 | 184 | 348 | 283 | 351 |
| citm_catalog.json | 1.73 | 376 | 737 | 339 | 1586 |
| twitter.json | 0.63 | 254 | 532 | 329 | 648 |

### Memoria en archivos grandes

Con un archivo sintético de 137 MB (2 millones de objetos):

| Operación | Tiempo | Memoria máxima |
|---|---:|---:|
| `wjson validate` (streaming) | 0.64 s | 13 MB |
| `wjson minify` (árbol completo) | 6.7 s | 1.2 GB |

El streaming mantiene la memoria constante sea cual sea el tamaño del archivo. El árbol ocupa unas 9 veces el tamaño del JSON, porque cada valor y cada clave es un objeto independiente en el *heap*. En ese contenedor, la mayor parte del tiempo de la primera pasada se va en fallos de página del sistema operativo al pedir memoria nueva. Reducir ese consumo con un asignador tipo *arena* está en la hoja de ruta.

## Próximos pasos

- Comparar con otras bibliotecas (RapidJSON, nlohmann/json, simdjson) en el mismo job.
- Optimizaciones previstas: strings sin copias (*zero-copy*), asignador *arena* para el DOM y SIMD para saltar espacios y buscar comillas.
