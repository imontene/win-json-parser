# Cómo publicar una versión

El flujo [`release.yml`](../.github/workflows/release.yml) se encarga de compilar y empaquetar todo; publicar requiere tres pasos manuales.

## 1. Preparar

1. Actualiza la versión en los cuatro sitios y comprueba que coinciden:
   ```bash
   python scripts/check_versions.py
   ```
   Los sitios son `CMakeLists.txt`, `ports/wjson/vcpkg.json`, `bindings/dotnet/src/Wjson/Wjson.csproj` y `bindings/powershell/Wjson/Wjson.psd1`.
2. Mueve las entradas de `CHANGELOG.md` de "sin publicar" a la nueva versión, con la fecha.
3. Opcional: ejecuta el flujo **Release** a mano (*Actions → Release → Run workflow*). Es un ensayo: genera todos los artefactos sin publicar nada.

## 2. Etiquetar

```bash
git tag v1.0.0
git push origin v1.0.0
```

El flujo comprueba que la etiqueta coincide con la versión y crea una **GitHub Release** con:

| Archivo | Contenido |
|---|---|
| `wjson-<v>-windows-{x64,x86,arm64}.zip` | `wjson.exe` sin dependencias, `LICENSE`, `README.md` |
| `Imontene.Wjson.<v>.nupkg` / `.snupkg` | Paquete NuGet con binarios nativos y símbolos |
| `Wjson-powershell-<v>.zip` | Módulo de PowerShell |
| `winget-manifests-<v>.zip` | Manifiestos de winget con las URLs y hashes de la release |
| `SHA256SUMS.txt` | Sumas de verificación |

## 3. Publicar en los registros

| Registro | Cómo |
|---|---|
| **NuGet** | Automático si existe el secreto `NUGET_API_KEY` (*Settings → Secrets and variables → Actions*). Clave en [nuget.org](https://www.nuget.org/account/apikeys). |
| **PowerShell Gallery** | Automático si existe el secreto `PSGALLERY_API_KEY`. Clave en [powershellgallery.com](https://www.powershellgallery.com/account/apikeys). |
| **winget** | Manual. Descomprime `winget-manifests-<v>.zip` y envíalo con `wingetcreate submit <carpeta>` o con un pull request a [microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs). |
| **vcpkg** | Manual. Copia `ports/wjson` al registro de vcpkg sustituyendo `SOURCE_PATH` por `vcpkg_from_github` (ver el comentario del `portfile.cmake`). |

## Firma de código

Los ejecutables no están firmados todavía, así que SmartScreen puede avisar al descargarlos. Para firmarlos hace falta un certificado de firma de código (o [Azure Trusted Signing](https://learn.microsoft.com/azure/trusted-signing/)) y un paso de firma en el job `windows` antes de empaquetar.
