param([string]$Package = (Join-Path (Split-Path $PSScriptRoot -Parent) 'out\foo_mmf.fb2k-component'))
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($Package)
try {
    $expected = @('foo_mmf.dll','x64/foo_mmf.dll','smaf_helper.exe','README.md','LICENSE','sdk-license.txt','pfc-license.txt')
    if (@(Compare-Object ($expected | Sort-Object) ($archive.Entries.FullName | Sort-Object)).Count) { throw 'Unexpected package contents' }
    $machines = @{ 'foo_mmf.dll' = 0x14c; 'x64/foo_mmf.dll' = 0x8664; 'smaf_helper.exe' = 0x14c }
    foreach ($name in $machines.Keys) {
        $stream = $archive.GetEntry($name).Open()
        $memory = New-Object IO.MemoryStream
        try {
            $stream.CopyTo($memory)
            $bytes = $memory.ToArray()
            if ([BitConverter]::ToUInt16($bytes,0) -ne 0x5a4d) { throw "Invalid DOS header: $name" }
            $pe = [BitConverter]::ToInt32($bytes,60)
            if ([BitConverter]::ToUInt32($bytes,$pe) -ne 0x4550) { throw "Invalid PE header: $name" }
            if ([BitConverter]::ToUInt16($bytes,$pe+4) -ne $machines[$name]) { throw "Wrong architecture: $name" }
            # CodeView records must not expose an absolute PDB path from the build machine.
            $optional = $pe + 24
            $directories = $optional + $(if ([BitConverter]::ToUInt16($bytes,$optional) -eq 0x20b) { 112 } else { 96 })
            $debugRva = [BitConverter]::ToUInt32($bytes,$directories+6*8)
            $debugSize = [BitConverter]::ToUInt32($bytes,$directories+6*8+4)
            if ($debugRva -and $debugSize) {
                $sections = $optional + [BitConverter]::ToUInt16($bytes,$pe+20)
                $debugOffset = $null
                for ($i=0; $i -lt [BitConverter]::ToUInt16($bytes,$pe+6); $i++) {
                    $section = $sections + $i*40
                    $rva = [BitConverter]::ToUInt32($bytes,$section+12)
                    $size = [Math]::Max([BitConverter]::ToUInt32($bytes,$section+8),[BitConverter]::ToUInt32($bytes,$section+16))
                    if ($debugRva -ge $rva -and $debugRva -lt $rva+$size) {
                        $debugOffset = [BitConverter]::ToUInt32($bytes,$section+20) + $debugRva - $rva
                        break
                    }
                }
                if ($null -eq $debugOffset -or $debugSize % 28) { throw "Invalid debug directory: $name" }
                for ($offset=$debugOffset; $offset -lt $debugOffset+$debugSize; $offset+=28) {
                    if ([BitConverter]::ToUInt32($bytes,$offset+12) -ne 2) { continue }
                    $record = [BitConverter]::ToUInt32($bytes,$offset+24)
                    $recordSize = [BitConverter]::ToUInt32($bytes,$offset+16)
                    if ($recordSize -le 24 -or $record+$recordSize -gt $bytes.Length -or [Text.Encoding]::ASCII.GetString($bytes,$record,4) -ne 'RSDS') { throw "Invalid CodeView record: $name" }
                    $symbol = [Text.Encoding]::UTF8.GetString($bytes,$record+24,$recordSize-24).TrimEnd([char]0)
                    if ($symbol -ne ([IO.Path]::GetFileNameWithoutExtension($name)+'.pdb')) { throw "Nonportable symbol path in $name" }
                }
            }
        } finally { $stream.Dispose(); $memory.Dispose() }
    }
    Write-Output 'PASS combined package contents, x86/x64 PE architectures and portable symbol paths'
} finally { $archive.Dispose() }
