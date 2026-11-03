@{
    RootModule           = 'Wjson.psm1'
    ModuleVersion        = '1.1.0'
    GUID                 = '2ad89558-8b93-4eb0-b990-0c9eebed6a36'
    Author               = 'imontene'
    Copyright            = '(c) 2026 imontene and win-json-parser contributors. MIT License.'
    Description          = 'Fast, strict JSON for PowerShell, backed by the native wjson parser: validation with line/column errors (streaming for large files), conversion, formatting, JSONC and JSON Pointer queries.'
    PowerShellVersion    = '5.1'
    CompatiblePSEditions = @('Desktop', 'Core')
    FunctionsToExport    = @('Test-WJson', 'ConvertFrom-WJson', 'Format-WJson', 'Get-WJsonValue')
    CmdletsToExport      = @()
    VariablesToExport    = @()
    AliasesToExport      = @()
    PrivateData          = @{
        PSData = @{
            Tags         = @('JSON', 'JSONC', 'Parser', 'Validation', 'JSONPointer', 'Windows')
            LicenseUri   = 'https://github.com/imontene/win-json-parser/blob/main/LICENSE'
            ProjectUri   = 'https://github.com/imontene/win-json-parser'
            ReleaseNotes = 'https://github.com/imontene/win-json-parser/blob/main/CHANGELOG.md'
        }
    }
}
