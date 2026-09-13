$ErrorActionPreference = 'Stop'

$sourcePath = Join-Path $PSScriptRoot 'SMAA.reference.hlsl'
$outputPath = Join-Path $PSScriptRoot 'SMAAReference.generated.h'
$source = [System.IO.File]::ReadAllText($sourcePath)
if ($source.Contains(')SMAALIB"')) {
    throw 'SMAA source collides with the generated raw-string delimiter.'
}
$chunks = [System.Collections.Generic.List[string]]::new()
for ($offset = 0; $offset -lt $source.Length; $offset += 12000) {
    $length = [Math]::Min(12000, $source.Length - $offset)
    $chunks.Add("R`"SMAALIB(" + $source.Substring($offset, $length) + ")SMAALIB`"")
}
$header = "#pragma once`r`n`r`nstatic const char kSmaaReferenceSource[] =`r`n" +
    ($chunks -join "`r`n") + ";`r`n"
[System.IO.File]::WriteAllText($outputPath, $header, [System.Text.UTF8Encoding]::new($false))
