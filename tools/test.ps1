<#
.SYNOPSIS
  Builds and runs tests\live_tests.cpp (the local stats store and the guest-buffer helpers behind the offline Live
  services) and tests\achievements_overlay_rules_tests.cpp (the achievements overlay's controller and window-size
  rules), with Visual Studio's clang++. No game files and no ReXGlue are needed.

.EXAMPLE
  .\tools\test.ps1
#>
$ErrorActionPreference = 'Stop'

$Root = Split-Path $PSScriptRoot -Parent
$OutDir = Join-Path $Root 'build\tests'
$Exe = Join-Path $OutDir 'live_tests.exe'
$RulesExe = Join-Path $OutDir 'achievements_overlay_rules_tests.exe'

$Vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $Vswhere)) { throw 'Visual Studio Installer (vswhere.exe) not found' }
$VsRoot = & $Vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 Microsoft.VisualStudio.Component.VC.Llvm.Clang -property installationPath
if (-not $VsRoot) { throw 'Visual Studio with the C++ x64 tools and C++ Clang tools is required' }
$VsDevCmd = Join-Path $VsRoot 'Common7\Tools\VsDevCmd.bat'
$ClangXX = Join-Path $VsRoot 'VC\Tools\Llvm\x64\bin\clang++.exe'
foreach ($tool in @($VsDevCmd, $ClangXX)) {
    if (-not (Test-Path $tool)) { throw "Required tool not found: $tool" }
}

$SavedEnv = [Environment]::GetEnvironmentVariables()
try {
    # Keep MSYS2's gcc out of the build.
    $env:PATH = (($env:PATH -split ';') | Where-Object { $_ -and $_ -notmatch '\\msys64\\' -and $_ -notmatch '^C:\\msys64' }) -join ';'
    $vsEnv = cmd.exe /c "`"$VsDevCmd`" -arch=x64 -host_arch=x64 -no_logo >nul && set"
    foreach ($line in $vsEnv) {
        if ($line -match '^([^=]+)=(.*)$') { Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2] }
    }

    New-Item -ItemType Directory -Force $OutDir | Out-Null
    & $ClangXX -std=c++23 -O1 -Wall -Wextra -I (Join-Path $Root 'src') `
        (Join-Path $Root 'tests\live_tests.cpp') (Join-Path $Root 'src\local_stats.cpp') (Join-Path $Root 'src\live_stats.cpp') `
        -o $Exe
    if ($LASTEXITCODE) { throw "Test build failed ($LASTEXITCODE)" }
    & $ClangXX -std=c++23 -O1 -Wall -Wextra -I (Join-Path $Root 'src') `
        (Join-Path $Root 'tests\achievements_overlay_rules_tests.cpp') -o $RulesExe
    if ($LASTEXITCODE) { throw "Test build failed ($LASTEXITCODE)" }
} finally {
    foreach ($name in @([Environment]::GetEnvironmentVariables().Keys)) {
        if (-not $SavedEnv.Contains($name)) { Remove-Item "Env:$name" }
    }
    foreach ($name in $SavedEnv.Keys) { Set-Item "Env:$name" $SavedEnv[$name] }
}

& $Exe
if ($LASTEXITCODE) { throw "Tests failed ($LASTEXITCODE)" }
& $RulesExe
if ($LASTEXITCODE) { throw "Achievements overlay rule tests failed ($LASTEXITCODE)" }
