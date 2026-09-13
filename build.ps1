param([string]$Project = 'smaf_probe.vcxproj', [string]$Configuration = 'Release', [string]$Platform = 'Win32')
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force (Join-Path $PSScriptRoot 'build') | Out-Null
# Some launcher environments contain both Path and PATH. MSBuild rejects that.
$buildPath = [Environment]::GetEnvironmentVariable('PATH', 'Process')
[Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
[Environment]::SetEnvironmentVariable('Path', $null, 'Process')
[Environment]::SetEnvironmentVariable('Path', $buildPath, 'Process')
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'Visual Studio C++ tools not found.' }
$msbuild = Join-Path $installation 'MSBuild\Current\Bin\MSBuild.exe'
& $msbuild (Join-Path $PSScriptRoot $Project) /m:1 /nologo /verbosity:minimal /fl "/flp:logfile=$PSScriptRoot\build\last-build.log;verbosity=normal" "/p:Configuration=$Configuration" "/p:Platform=$Platform" /p:PlatformToolset=v145 /p:PreferredToolArchitecture=x64 /p:CL_MPCount=4
if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }
