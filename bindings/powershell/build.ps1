<#
.SYNOPSIS
Assembles the Wjson PowerShell module into an output folder.
.PARAMETER NativeRoot
Folder with the native libraries laid out as <rid>/native/<file>, for example
win-x64/native/wjson.dll. Every runtime found there is included.
.PARAMETER OutputPath
Destination folder (the module folder itself, e.g. out/Wjson).
.EXAMPLE
./build.ps1 -NativeRoot ../../native -OutputPath ../../out/Wjson
#>
param(
    [Parameter(Mandatory)] [string] $NativeRoot,
    [Parameter(Mandatory)] [string] $OutputPath,
    [string] $Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'

$project = Join-Path $PSScriptRoot '../dotnet/src/Wjson/Wjson.csproj'
dotnet build $project -c $Configuration --nologo -v quiet
if ($LASTEXITCODE -ne 0) { throw 'dotnet build failed' }

if (Test-Path $OutputPath) { Remove-Item $OutputPath -Recurse -Force }
New-Item -ItemType Directory -Path $OutputPath | Out-Null

Copy-Item (Join-Path $PSScriptRoot 'Wjson/Wjson.psd1'), (Join-Path $PSScriptRoot 'Wjson/Wjson.psm1') $OutputPath
Copy-Item (Join-Path $PSScriptRoot '../../LICENSE'), (Join-Path $PSScriptRoot '../../THIRD_PARTY_NOTICES.md') $OutputPath

foreach ($framework in 'netstandard2.0', 'net8.0') {
    $source = Join-Path $PSScriptRoot "../dotnet/src/Wjson/bin/$Configuration/$framework/Imontene.Wjson.dll"
    $target = Join-Path $OutputPath "lib/$framework"
    New-Item -ItemType Directory -Path $target -Force | Out-Null
    Copy-Item $source $target
}

$runtimes = Join-Path $OutputPath 'runtimes'
Copy-Item $NativeRoot $runtimes -Recurse
$found = Get-ChildItem $runtimes -Recurse -File | ForEach-Object { $_.FullName.Substring($runtimes.Length + 1) }
if (-not $found) { throw "No native libraries found in $NativeRoot" }
Write-Host "Module written to $OutputPath with native runtimes:"
$found | ForEach-Object { Write-Host "  $_" }
