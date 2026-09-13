param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
if (-not $SkipBuild) {
    & $PSScriptRoot\build.ps1 -Project smaf_helper.vcxproj -Platform Win32
    & $PSScriptRoot\build.ps1 -Project foo_mmf.vcxproj -Platform Win32
    & $PSScriptRoot\build.ps1 -Project foo_mmf.vcxproj -Platform x64
}
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem

# foobar overlays x64/ on the package root for x64 and ARM64EC hosts.
$folder = Join-Path $PSScriptRoot 'out'
New-Item -ItemType Directory -Force $folder | Out-Null
$destination = Join-Path $folder 'foo_mmf.fb2k-component'
# Retire the two legacy generated packages so out/ has one installable artifact.
foreach ($legacy in 'out\Win32\foo_mmf.fb2k-component','out\x64\foo_mmf.fb2k-component') {
    $legacyPath = Join-Path $PSScriptRoot $legacy
    if (Test-Path -LiteralPath $legacyPath) { Remove-Item -LiteralPath $legacyPath }
}
# Replace only this explicit generated archive; no staging directory is deleted.
if (Test-Path -LiteralPath $destination) { Remove-Item -LiteralPath $destination }
$archive = [IO.Compression.ZipFile]::Open($destination, [IO.Compression.ZipArchiveMode]::Create)
try {
    $entries = @{
        'foo_mmf.dll' = 'build\Win32\Release\foo_mmf.dll'
        'x64/foo_mmf.dll' = 'build\x64\Release\foo_mmf.dll'
        'smaf_helper.exe' = 'build\Win32\Release\smaf_helper.exe'
        'README.md' = 'README.md'
        'LICENSE' = 'LICENSE'
        'sdk-license.txt' = 'sdk\sdk-license.txt'
        'pfc-license.txt' = 'sdk\pfc\pfc-license.txt'
    }
    foreach ($name in ($entries.Keys | Sort-Object)) {
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, (Join-Path $PSScriptRoot $entries[$name]), $name) | Out-Null
    }
} finally { $archive.Dispose() }
Get-FileHash -Algorithm SHA256 -LiteralPath $destination | Format-List Path,Hash
