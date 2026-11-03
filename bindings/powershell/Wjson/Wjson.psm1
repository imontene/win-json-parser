# Wjson PowerShell module: fast, strict JSON validation, conversion,
# formatting and JSON Pointer queries backed by the native wjson library.
# SPDX-License-Identifier: MIT
Set-StrictMode -Version 3.0

# --- Load the .NET binding and point it at the native library ---------------

$useNet8 = $PSVersionTable.PSEdition -eq 'Core' -and [Environment]::Version.Major -ge 8
$framework = if ($useNet8) { 'net8.0' } else { 'netstandard2.0' }
$assemblyPath = Join-Path $PSScriptRoot "lib/$framework/Imontene.Wjson.dll"
Add-Type -Path $assemblyPath

$isWindowsOS = $PSVersionTable.PSEdition -eq 'Desktop' -or $IsWindows
$arch = switch ([System.Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture) {
    'X86' { 'x86' }
    'Arm64' { 'arm64' }
    default { 'x64' }
}
$rid, $file = if ($isWindowsOS) { "win-$arch", 'wjson.dll' }
              elseif ($IsMacOS) { "osx-$arch", 'libwjson.dylib' }
              else { "linux-$arch", 'libwjson.so' }
$nativePath = Join-Path $PSScriptRoot "runtimes/$rid/native/$file"
if (Test-Path -LiteralPath $nativePath) {
    [Wjson.WJson]::NativeLibraryPath = $nativePath
}

# --- Helpers ----------------------------------------------------------------

function New-WJsonOptions([bool] $Jsonc, [int] $MaxDepth) {
    $options = if ($Jsonc) { [Wjson.ParseOptions]::Jsonc } else { [Wjson.ParseOptions]::new() }
    if ($MaxDepth -gt 0) { $options.MaxDepth = $MaxDepth }
    $options
}

# Converts JsonObject/JsonArray trees into PSCustomObject/object[] trees, the
# same shapes that ConvertFrom-Json produces.
function ConvertTo-PSValue($Value) {
    if ($Value -is [Wjson.JsonObject]) {
        $properties = [ordered]@{}
        foreach ($entry in $Value.GetEnumerator()) {
            $properties[$entry.Key] = ConvertTo-PSValue $entry.Value
        }
        return [pscustomobject]$properties
    }
    if ($Value -is [Wjson.JsonArray]) {
        $items = [object[]]::new($Value.Count)
        for ($i = 0; $i -lt $Value.Count; $i++) { $items[$i] = ConvertTo-PSValue $Value[$i] }
        return , $items
    }
    $Value
}

function New-WJsonErrorRecord([Wjson.JsonParseError] $ParseError, [string] $Source, $Target) {
    $where = if ($ParseError.Line -gt 0) { "${Source}:$($ParseError.Line):$($ParseError.Column)" } else { $Source }
    $exception = [System.FormatException]::new("${where}: $($ParseError.Message)")
    [System.Management.Automation.ErrorRecord]::new(
        $exception, "WJson.$($ParseError.Code)", [System.Management.Automation.ErrorCategory]::InvalidData, $Target)
}

# Collects pipeline input the way ConvertFrom-Json does: several strings (for
# example lines from Get-Content without -Raw) are joined into one document.
function Join-WJsonInput([System.Collections.Generic.List[string]] $Lines) {
    if ($Lines.Count -eq 1) { return $Lines[0] }
    $Lines -join "`n"
}

# --- Public functions -------------------------------------------------------

function Test-WJson {
    <#
    .SYNOPSIS
    Checks whether text or a file is valid JSON.
    .DESCRIPTION
    Files are validated in streaming mode, so files of any size use constant
    memory. UTF-8 (with or without BOM) and UTF-16 (with BOM) are supported.
    .PARAMETER Json
    JSON text. Accepts pipeline input; several strings are joined with newlines.
    .PARAMETER Path
    Path of a JSON file.
    .PARAMETER Jsonc
    Accept comments and trailing commas (JSON with Comments).
    .PARAMETER MaxDepth
    Maximum nesting depth (default 512).
    .PARAMETER Detailed
    Return an object with the result and the error position instead of a boolean.
    .EXAMPLE
    Test-WJson -Path .\settings.json -Jsonc
    .EXAMPLE
    Get-Content data.json -Raw | Test-WJson -Detailed
    #>
    [CmdletBinding(DefaultParameterSetName = 'Json')]
    [OutputType([bool], [pscustomobject])]
    param(
        [Parameter(Mandatory, Position = 0, ValueFromPipeline, ParameterSetName = 'Json')]
        [AllowEmptyString()]
        [string] $Json,

        [Parameter(Mandatory, ParameterSetName = 'Path')]
        [Alias('FullName')]
        [string] $Path,

        [switch] $Jsonc,
        [int] $MaxDepth,
        [switch] $Detailed
    )
    begin { $lines = [System.Collections.Generic.List[string]]::new() }
    process { if ($PSCmdlet.ParameterSetName -eq 'Json') { $lines.Add($Json) } }
    end {
        $options = New-WJsonOptions $Jsonc $MaxDepth
        $parseError = $null
        if ($PSCmdlet.ParameterSetName -eq 'Path') {
            $fullPath = $PSCmdlet.GetUnresolvedProviderPathFromPSPath($Path)
            $valid = [Wjson.WJson]::TryValidateFile($fullPath, [ref] $parseError, $options)
        } else {
            $valid = [Wjson.WJson]::TryValidate((Join-WJsonInput $lines), [ref] $parseError, $options)
        }
        if (-not $Detailed) { return $valid }
        [pscustomobject]@{
            Valid   = $valid
            Line    = if ($parseError) { $parseError.Line } else { $null }
            Column  = if ($parseError) { $parseError.Column } else { $null }
            Offset  = if ($parseError) { $parseError.Offset } else { $null }
            Code    = if ($parseError) { $parseError.Code } else { $null }
            Message = if ($parseError) { $parseError.Message } else { $null }
        }
    }
}

function ConvertFrom-WJson {
    <#
    .SYNOPSIS
    Converts JSON text or a file into PowerShell objects.
    .DESCRIPTION
    Like ConvertFrom-Json, but with strict RFC 8259 parsing, precise error
    positions, JSONC support and UTF-16 file support. Objects keep their member
    order. Integers that fit in 64 bits become [long]; other numbers [double].
    .PARAMETER AsHashtable
    Return ordered dictionaries ([Wjson.JsonObject]) instead of PSCustomObject.
    .PARAMETER NoEnumerate
    Output a top-level array as a single object instead of enumerating it.
    .EXAMPLE
    $settings = ConvertFrom-WJson -Path "$env:APPDATA\Code\User\settings.json" -Jsonc
    #>
    [CmdletBinding(DefaultParameterSetName = 'Json')]
    param(
        [Parameter(Mandatory, Position = 0, ValueFromPipeline, ParameterSetName = 'Json')]
        [AllowEmptyString()]
        [string] $Json,

        [Parameter(Mandatory, ParameterSetName = 'Path')]
        [Alias('FullName')]
        [string] $Path,

        [switch] $Jsonc,
        [int] $MaxDepth,
        [switch] $AsHashtable,
        [switch] $NoEnumerate
    )
    begin { $lines = [System.Collections.Generic.List[string]]::new() }
    process { if ($PSCmdlet.ParameterSetName -eq 'Json') { $lines.Add($Json) } }
    end {
        $options = New-WJsonOptions $Jsonc $MaxDepth
        try {
            if ($PSCmdlet.ParameterSetName -eq 'Path') {
                $source = $Path
                $value = [Wjson.WJson]::ParseFile($PSCmdlet.GetUnresolvedProviderPathFromPSPath($Path), $options)
            } else {
                $source = '<input>'
                $value = [Wjson.WJson]::Parse((Join-WJsonInput $lines), $options)
            }
        } catch [Wjson.JsonParseException] {
            $PSCmdlet.ThrowTerminatingError((New-WJsonErrorRecord $_.Exception.Error $source $null))
        }
        $result = if ($AsHashtable) { $value } else { ConvertTo-PSValue $value }
        Write-Output -InputObject $result -NoEnumerate:$NoEnumerate
    }
}

function Format-WJson {
    <#
    .SYNOPSIS
    Pretty-prints or minifies JSON.
    .PARAMETER Indent
    Spaces per level (default 2).
    .PARAMETER Minify
    Remove all insignificant whitespace (and comments, with -Jsonc).
    .PARAMETER AsciiOnly
    Escape every non-ASCII character as \uXXXX.
    .EXAMPLE
    Format-WJson -Path .\data.json -Indent 4
    .EXAMPLE
    '{"a":[1,2]}' | Format-WJson
    #>
    [CmdletBinding(DefaultParameterSetName = 'Json')]
    [OutputType([string])]
    param(
        [Parameter(Mandatory, Position = 0, ValueFromPipeline, ParameterSetName = 'Json')]
        [AllowEmptyString()]
        [string] $Json,

        [Parameter(Mandatory, ParameterSetName = 'Path')]
        [Alias('FullName')]
        [string] $Path,

        [ValidateRange(1, 16)]
        [int] $Indent = 2,
        [switch] $Minify,
        [switch] $AsciiOnly,
        [switch] $Jsonc
    )
    begin { $lines = [System.Collections.Generic.List[string]]::new() }
    process { if ($PSCmdlet.ParameterSetName -eq 'Json') { $lines.Add($Json) } }
    end {
        $options = New-WJsonOptions $Jsonc 0
        if ($PSCmdlet.ParameterSetName -eq 'Path') {
            $source = $Path
            # ReadAllText detects UTF-8 and UTF-16 byte order marks.
            $text = [System.IO.File]::ReadAllText($PSCmdlet.GetUnresolvedProviderPathFromPSPath($Path))
        } else {
            $source = '<input>'
            $text = Join-WJsonInput $lines
        }
        try {
            if ($Minify) { [Wjson.WJson]::Minify($text, [bool] $AsciiOnly, $options) }
            else { [Wjson.WJson]::Format($text, $Indent, [bool] $AsciiOnly, $options) }
        } catch [Wjson.JsonParseException] {
            $PSCmdlet.ThrowTerminatingError((New-WJsonErrorRecord $_.Exception.Error $source $null))
        }
    }
}

function Get-WJsonValue {
    <#
    .SYNOPSIS
    Gets the value at a JSON Pointer (RFC 6901), e.g. /items/0/name.
    .DESCRIPTION
    Writes a non-terminating error if there is no value at that path.
    .EXAMPLE
    Get-WJsonValue -Pointer /version -Path .\package.json
    .EXAMPLE
    $json | Get-WJsonValue /users/0/email
    #>
    [CmdletBinding(DefaultParameterSetName = 'Json')]
    param(
        [Parameter(Mandatory, Position = 0)]
        [AllowEmptyString()]
        [string] $Pointer,

        [Parameter(Mandatory, Position = 1, ValueFromPipeline, ParameterSetName = 'Json')]
        [AllowEmptyString()]
        [string] $Json,

        [Parameter(Mandatory, ParameterSetName = 'Path')]
        [Alias('FullName')]
        [string] $Path,

        [switch] $Jsonc,
        [switch] $AsHashtable
    )
    begin { $lines = [System.Collections.Generic.List[string]]::new() }
    process { if ($PSCmdlet.ParameterSetName -eq 'Json') { $lines.Add($Json) } }
    end {
        $options = New-WJsonOptions $Jsonc 0
        if ($PSCmdlet.ParameterSetName -eq 'Path') {
            $source = $Path
            $text = [System.IO.File]::ReadAllText($PSCmdlet.GetUnresolvedProviderPathFromPSPath($Path))
        } else {
            $source = '<input>'
            $text = Join-WJsonInput $lines
        }
        $value = $null
        try {
            $found = [Wjson.WJson]::TryQuery($text, $Pointer, [ref] $value, $options)
        } catch [Wjson.JsonParseException] {
            $PSCmdlet.ThrowTerminatingError((New-WJsonErrorRecord $_.Exception.Error $source $null))
        }
        if (-not $found) {
            $exception = [System.Management.Automation.ItemNotFoundException]::new("${source}: no value at '$Pointer'")
            $PSCmdlet.WriteError([System.Management.Automation.ErrorRecord]::new(
                $exception, 'WJson.PointerNotFound', [System.Management.Automation.ErrorCategory]::ObjectNotFound, $Pointer))
            return
        }
        $result = if ($AsHashtable) { $value } else { ConvertTo-PSValue $value }
        Write-Output -InputObject $result -NoEnumerate
    }
}

Export-ModuleMember -Function Test-WJson, ConvertFrom-WJson, Format-WJson, Get-WJsonValue
