# Pester 5 tests for the Wjson module.
#   Invoke-Pester ./tests -Output Detailed
# Set WJSON_MODULE_PATH to the built module folder (default: ../out/Wjson).
BeforeAll {
    $modulePath = if ($env:WJSON_MODULE_PATH) { $env:WJSON_MODULE_PATH } else { Join-Path $PSScriptRoot '../out/Wjson' }
    Import-Module (Join-Path $modulePath 'Wjson.psd1') -Force
    $work = Join-Path ([System.IO.Path]::GetTempPath()) ("wjson-pester-" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $work | Out-Null
}

AfterAll {
    Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
}

Describe 'Test-WJson' {
    It 'accepts valid JSON and rejects invalid JSON' {
        Test-WJson '{"a": [1, 2]}' | Should -BeTrue
        Test-WJson '{"a": [1, 2,]}' | Should -BeFalse
    }

    It 'reports the error position with -Detailed' {
        $result = Test-WJson "{`n  `"a`": [1, 2,]`n}" -Detailed
        $result.Valid | Should -BeFalse
        $result.Line | Should -Be 2
        $result.Column | Should -Be 14
        $result.Code | Should -Be 'UnexpectedCharacter'
    }

    It 'joins pipeline lines like ConvertFrom-Json' {
        "[1,", "2]" | Test-WJson | Should -BeTrue
    }

    It 'accepts JSONC with -Jsonc' {
        $jsonc = "{ // comment`n `"a`": 1, }"
        Test-WJson $jsonc | Should -BeFalse
        Test-WJson $jsonc -Jsonc | Should -BeTrue
    }

    It 'validates files, including UTF-16 and relative paths' {
        $file = Join-Path $work 'utf16.json'
        [System.IO.File]::WriteAllText($file, '{"ok": true}', [System.Text.Encoding]::Unicode)
        Test-WJson -Path $file | Should -BeTrue

        Push-Location $work
        try { Test-WJson -Path './utf16.json' | Should -BeTrue } finally { Pop-Location }

        (Test-WJson -Path (Join-Path $work 'missing.json') -Detailed).Code | Should -Be 'IoError'
    }

    It 'enforces -MaxDepth' {
        Test-WJson '[[[1]]]' -MaxDepth 2 | Should -BeFalse
    }
}

Describe 'ConvertFrom-WJson' {
    It 'produces PSCustomObject trees with typed values' {
        $o = ConvertFrom-WJson '{"name": "Iv\u00e1n", "n": 42, "d": 2.5, "ok": true, "none": null, "list": [1, {"x": 2}]}'
        $o | Should -BeOfType [pscustomobject]
        $o.name | Should -Be ([string][char]0x49 + 'v' + [char]0xE1 + 'n')
        $o.n | Should -BeOfType [long]
        $o.n | Should -Be 42
        $o.d | Should -Be 2.5
        $o.ok | Should -BeTrue
        $o.none | Should -BeNullOrEmpty
        $o.list.Count | Should -Be 2
        $o.list[1].x | Should -Be 2
    }

    It 'keeps member order' {
        $o = ConvertFrom-WJson '{"z": 1, "a": 2, "m": 3}'
        $o.PSObject.Properties.Name -join ',' | Should -Be 'z,a,m'
    }

    It 'returns ordered dictionaries with -AsHashtable' {
        $h = ConvertFrom-WJson '{"b": 1, "a": {"c": 2}}' -AsHashtable
        $h | Should -BeOfType [Wjson.JsonObject]
        $h['a']['c'] | Should -Be 2
        @($h.Keys) -join ',' | Should -Be 'b,a'
    }

    It 'enumerates top-level arrays unless -NoEnumerate' {
        @(ConvertFrom-WJson '[1, 2, 3]').Count | Should -Be 3
        $single = ConvertFrom-WJson '[1, 2, 3]' -NoEnumerate
        , $single | Should -BeOfType [object[]]
    }

    It 'throws a terminating error with the position' {
        { ConvertFrom-WJson '[1, 2,' } | Should -Throw '*1:7*unexpected end of input*'
    }

    It 'reads JSONC files' {
        $file = Join-Path $work 'settings.jsonc'
        Set-Content -LiteralPath $file -Value "{`n  // font`n  `"editor.fontSize`": 14,`n}"
        (ConvertFrom-WJson -Path $file -Jsonc).'editor.fontSize' | Should -Be 14
    }
}

Describe 'Format-WJson' {
    It 'pretty-prints and minifies' {
        Format-WJson '{"a":[1]}' | Should -Be "{`n  `"a`": [`n    1`n  ]`n}"
        Format-WJson ' { "a" : [ 1 ] } ' -Minify | Should -Be '{"a":[1]}'
        Format-WJson '{"a":1}' -Indent 4 | Should -Be "{`n    `"a`": 1`n}"
    }

    It 'escapes non-ASCII with -AsciiOnly' {
        Format-WJson ('["' + [char]0xE9 + '"]') -Minify -AsciiOnly | Should -Be '["\u00e9"]'
    }

    It 'strips comments with -Jsonc -Minify' {
        Format-WJson '[1, /* x */ 2,]' -Jsonc -Minify | Should -Be '[1,2]'
    }
}

Describe 'Get-WJsonValue' {
    BeforeAll { $doc = '{"items": [{"name": "uno"}, {"name": "dos"}], "a/b": {"c": true}}' }

    It 'returns the value at a JSON Pointer' {
        Get-WJsonValue '/items/1/name' $doc | Should -Be 'dos'
        (Get-WJsonValue '/a~1b' $doc).c | Should -BeTrue
        $doc | Get-WJsonValue '/items/0/name' | Should -Be 'uno'
    }

    It 'writes a non-terminating error when the pointer does not exist' {
        $value = Get-WJsonValue '/items/9' $doc -ErrorVariable err -ErrorAction SilentlyContinue
        $value | Should -BeNullOrEmpty
        $err[0].FullyQualifiedErrorId | Should -BeLike 'WJson.PointerNotFound*'
    }
}
