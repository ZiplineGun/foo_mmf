param([string]$Foobar = 'C:\Program Files\foobar2000', [ValidateSet('x64','Win32')][string]$Platform = 'x64', [switch]$Package, [switch]$Recovery)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$portable = Join-Path $root "analysis\foobar-$Platform"
New-Item -ItemType Directory -Force $portable | Out-Null
Get-ChildItem -LiteralPath $Foobar -File | Copy-Item -Destination $portable -Force
Copy-Item -LiteralPath (Join-Path $Foobar 'components') -Destination $portable -Recurse -Force
foreach ($directory in 'runtime','themes') {
    if (Test-Path -LiteralPath (Join-Path $Foobar $directory)) { Copy-Item -LiteralPath (Join-Path $Foobar $directory) -Destination $portable -Recurse -Force }
}
New-Item -ItemType File -Force (Join-Path $portable 'portable_mode_enabled') | Out-Null
foreach ($component in 'foo_mmf.dll','foo_mmf_tests.dll') {
    Copy-Item -LiteralPath (Join-Path $root "build\$Platform\Release\$component") -Destination (Join-Path $portable 'components') -Force
}
Copy-Item -LiteralPath (Join-Path $root 'build\Win32\Release\smaf_helper.exe') -Destination (Join-Path $portable 'components') -Force
if ($Package) {
    # Exercise the published binaries with foobar's documented architecture overlay.
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead((Join-Path $root 'out\foo_mmf.fb2k-component'))
    try {
        $dll = if ($Platform -eq 'x64') { 'x64/foo_mmf.dll' } else { 'foo_mmf.dll' }
        foreach ($entry in $dll,'smaf_helper.exe') {
            $destination = Join-Path (Join-Path $portable 'components') ([IO.Path]::GetFileName($entry))
            [IO.Compression.ZipFileExtensions]::ExtractToFile($archive.GetEntry($entry), $destination, $true)
        }
    } finally { $archive.Dispose() }
}
# Self-authored format-0 MIDI: piano A4, five-second note.
[IO.File]::WriteAllBytes((Join-Path $root 'analysis\tone.mid'), [byte[]](0x4d,0x54,0x68,0x64,0,0,0,6,0,0,0,1,0,96,0x4d,0x54,0x72,0x6b,0,0,0,16,0,0xc0,0,0,0x90,69,100,0x87,0x40,0x80,69,0,0,0xff,0x2f,0))
$env:FOO_MMF_TEST_ROOT = $root
$resultPath = Join-Path $root 'analysis\host-results.txt'
if (Test-Path -LiteralPath $resultPath) { Remove-Item -LiteralPath $resultPath }
# Explicit chorus/reverb sends, controller reset, GM reset and running status.
$chorusTrack = [byte[]](0,0xb0,91,100,0,93,127,0,7,100,0,121,0,0,0xf0,5,0x7e,0x7f,9,1,0xf7,0,0xb0,91,100,0,93,127,0,0x90,69,100,0x87,0x40,0x80,69,0,0,0xff,0x2f,0)
$chorusHeader = [byte[]](0x4d,0x54,0x68,0x64,0,0,0,6,0,0,0,1,0,96,0x4d,0x54,0x72,0x6b,0,0,0,$chorusTrack.Length)
[IO.File]::WriteAllBytes((Join-Path $root 'analysis\chorus.mid'), [byte[]]($chorusHeader + $chorusTrack))
try {
    if ($Recovery) { $env:FOO_MMF_TEST_RECOVERY = '1' }
    $process = Start-Process -FilePath (Join-Path $portable 'foobar2000.exe') -WorkingDirectory $portable -WindowStyle Hidden -PassThru
} finally { Remove-Item Env:FOO_MMF_TEST_ROOT; if ($Recovery) { Remove-Item Env:FOO_MMF_TEST_RECOVERY } }
Write-Output "Portable test host PID: $($process.Id)"
if (-not $process.WaitForExit(45000)) { throw "Portable test host has not exited; inspect PID $($process.Id)." }
if (-not (Test-Path -LiteralPath $resultPath)) { throw "Test host exited without running the suite (exit code $($process.ExitCode))." }
$result = Get-Content -LiteralPath (Join-Path $root 'analysis\host-results.txt') -Raw
Copy-Item -LiteralPath (Join-Path $root 'analysis\host-results.txt') -Destination (Join-Path $root "analysis\host-results-$Platform.txt") -Force
Write-Output $result
if (-not $result.Contains('PASS ALL HOST TESTS')) { throw 'Host integration tests failed.' }
