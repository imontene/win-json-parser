<#
.SYNOPSIS
Smoke test for wjson Viewer: opens files, checks that the window appears and
stays up, and captures screenshots.
.DESCRIPTION
Each screenshot is saved as PNG and, with -PrintScreenshots, also written to
the output as base64 lines ("SHOT:<name>:<n>:<data>") so it can be recovered
from a CI log.
#>
param(
    [Parameter(Mandatory)] [string] $Exe,
    [Parameter(Mandatory)] [string] $Samples,
    [string] $OutputDir = 'screenshots',
    [switch] $PrintScreenshots
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type -Namespace Native -Name Win32 -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
'@
New-Item -ItemType Directory -Force $OutputDir | Out-Null

function Save-Screenshot([string] $Name) {
    $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bitmap = [System.Drawing.Bitmap]::new($bounds.Width, $bounds.Height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
    $path = Join-Path $OutputDir "$Name.png"
    $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose(); $bitmap.Dispose()
    Write-Host "screenshot $path ($($bounds.Width)x$($bounds.Height), $((Get-Item $path).Length) bytes)"
    if ($PrintScreenshots) {
        $data = [Convert]::ToBase64String([IO.File]::ReadAllBytes($path))
        $n = 0
        for ($i = 0; $i -lt $data.Length; $i += 2000) {
            Write-Host ("SHOT:{0}:{1}:{2}" -f $Name, $n++, $data.Substring($i, [Math]::Min(2000, $data.Length - $i)))
        }
    }
}

$env:WJSON_VIEWER_LOG = Join-Path $OutputDir 'viewer.log'

$priFiles = Get-ChildItem (Split-Path $Exe) -Filter *.pri
Write-Host "PRI files next to the viewer: $(if ($priFiles) { ($priFiles.Name -join ', ') } else { 'none' })"

function Write-Diagnostics {
    if (Test-Path $env:WJSON_VIEWER_LOG) {
        Write-Host '--- viewer.log'
        Get-Content $env:WJSON_VIEWER_LOG | Write-Host
    }
    Write-Host '--- recent Application event log errors'
    Get-WinEvent -FilterHashtable @{ LogName = 'Application'; Level = 2; StartTime = (Get-Date).AddMinutes(-10) } -MaxEvents 5 -ErrorAction SilentlyContinue |
        ForEach-Object { Write-Host "[$($_.ProviderName)] $($_.Message)" }
}

function Test-Scenario([string] $Name, [string[]] $Arguments, [string] $ExpectedTitle, [double] $DelaySeconds = 4) {
    Write-Host "== $Name"
    $process = Start-Process -FilePath $Exe -ArgumentList $Arguments -PassThru
    try {
        $deadline = (Get-Date).AddSeconds(60)
        while ((Get-Date) -lt $deadline) {
            $process.Refresh()
            if ($process.HasExited) { throw "${Name}: the viewer exited with code $($process.ExitCode)" }
            if ($process.MainWindowHandle -ne 0 -and $process.MainWindowTitle -like "*$ExpectedTitle*") { break }
            Start-Sleep -Milliseconds 500
        }
        if ($process.MainWindowTitle -notlike "*$ExpectedTitle*") {
            throw "${Name}: window title is '$($process.MainWindowTitle)', expected it to contain '$ExpectedTitle'"
        }
        [Native.Win32]::ShowWindow($process.MainWindowHandle, 3) | Out-Null   # maximize
        [Native.Win32]::SetForegroundWindow($process.MainWindowHandle) | Out-Null
        Start-Sleep -Milliseconds ([int]($DelaySeconds * 1000))                # validation + rendering
        $process.Refresh()
        if ($process.HasExited) { throw "${Name}: the viewer exited with code $($process.ExitCode)" }
        Write-Host "window: '$($process.MainWindowTitle)'"
        Save-Screenshot $Name
    } catch {
        Write-Diagnostics
        throw
    } finally {
        if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    }
}

# Scripted checks inside the app (colors, typing, undo, text <-> tree): see MainWindow.SelfTest.cs.
function Test-SelfTest([string] $File) {
    Write-Host "== self-test"
    $log = Join-Path $OutputDir 'self-test.txt'
    $process = Start-Process -FilePath $Exe -ArgumentList @($File, '--lang', 'en', '--self-test', $log) -PassThru
    $null = $process.Handle   # keeps the exit code available after the process ends
    if (-not $process.WaitForExit(180000)) {
        $process.Kill()
        Write-Diagnostics
        throw 'self-test: the viewer did not finish in 3 minutes'
    }
    if (Test-Path $log) { Get-Content $log | Write-Host } else { Write-Host 'no self-test log' }
    if ($process.ExitCode -ne 0) {
        Write-Diagnostics
        throw "self-test: exit code $($process.ExitCode)"
    }
}

# A file big enough to see the progress panel while it opens (about 110 MB).
function New-LargeSample([string] $Path) {
    $item = '{"id":1234,"nombre":"elemento de prueba","valores":[1.5,2,3],"activo":true,"extra":null},'
    $writer = [IO.StreamWriter]::new($Path, $false, [Text.UTF8Encoding]::new($false))
    try {
        $writer.Write('[')
        for ($i = 0; $i -lt 600; $i++) { $writer.Write($item * 2000) }
        $writer.Write('{}]')
    } finally { $writer.Dispose() }
    Write-Host "large sample: $([Math]::Round((Get-Item $Path).Length / 1MB)) MB"
}

Test-SelfTest (Join-Path $Samples 'sample.json')

Test-Scenario 'valid' @((Join-Path $Samples 'sample.json'), '--pointer', '/tiendas/2/ciudad') 'sample.json'
Test-Scenario 'invalid' @((Join-Path $Samples 'invalid.json')) 'invalid.json'
Test-Scenario 'spanish-jsonc' @((Join-Path $Samples 'settings.jsonc'), '--lang', 'es') 'settings.jsonc'
Test-Scenario 'compare' @((Join-Path $Samples 'sample.json'), '--compare', (Join-Path $Samples 'sample-v2.json'), '--lang', 'es') 'sample.json'
$large = Join-Path ([IO.Path]::GetTempPath()) 'wjson-large-sample.json'
New-LargeSample $large
Test-Scenario 'large-file-progress' @($large, '--lang', 'es') 'wjson-large-sample.json' 2.5
Remove-Item $large -ErrorAction SilentlyContinue
Write-Host 'Smoke test passed'
