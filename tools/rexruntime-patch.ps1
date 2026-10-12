# Shared by build-patched-rexruntime.ps1 and package-release.ps1: the pinned ReXGlue baseline, the
# runtime patch, and the check that an SDK folder holds the patched runtime and the stock GPU plugin.

$RuntimePatchDir = Join-Path (Split-Path $PSScriptRoot -Parent) 'patches\rexglue-sdk'
$RuntimePatchFile = Join-Path $RuntimePatchDir '0001-physical-release-order.patch'
$RuntimeStampName = 'rexruntime-patch.txt'
$RuntimeFiles = @('bin/rexruntime.dll', 'bin/rexruntimerd.dll', 'lib/rexruntime.lib', 'lib/rexruntimerd.lib')

function Read-RuntimeBaseline {
    $values = @{}
    foreach ($line in Get-Content -LiteralPath (Join-Path $RuntimePatchDir 'baseline.env')) {
        if ($line -match '^\s*([A-Z0-9_]+)=(.*)$') { $values[$Matches[1]] = $Matches[2].Trim() }
    }
    return $values
}

function Get-FileSha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-OfficialRuntimeHashes($Baseline) {
    return @{
        'bin/rexruntime.dll'   = $Baseline.OFFICIAL_WIN_RUNTIME_SHA256
        'bin/rexruntimerd.dll' = $Baseline.OFFICIAL_WIN_RUNTIME_RD_SHA256
        'lib/rexruntime.lib'   = $Baseline.OFFICIAL_WIN_RUNTIME_LIB_SHA256
        'lib/rexruntimerd.lib' = $Baseline.OFFICIAL_WIN_RUNTIME_RD_LIB_SHA256
    }
}

function Get-OfficialGpuHashes($Baseline) {
    return @{
        'bin/rexgpu-xenos.dll'   = $Baseline.OFFICIAL_WIN_GPU_SHA256
        'bin/rexgpu-xenosrd.dll' = $Baseline.OFFICIAL_WIN_GPU_RD_SHA256
    }
}

# Returns the problems found; an empty list means $SdkDir holds the project's patched runtime and the stock GPU plugin.
function Test-PatchedRuntime([string]$SdkDir) {
    $problems = New-Object System.Collections.Generic.List[string]
    $baseline = Read-RuntimeBaseline
    $stampPath = Join-Path $SdkDir $RuntimeStampName
    if (-not (Test-Path -LiteralPath $stampPath -PathType Leaf)) {
        $problems.Add("$RuntimeStampName is missing (run tools\build-patched-rexruntime.ps1)")
        return $problems
    }
    $stamp = @{}
    $hashes = @{}
    foreach ($line in Get-Content -LiteralPath $stampPath) {
        if ($line -match '^([a-z_0-9]+)=(.*)$') { $stamp[$Matches[1]] = $Matches[2] }
        elseif ($line -match '^([0-9a-f]{64})  (.+)$') { $hashes[$Matches[2]] = $Matches[1] }
    }
    if ($stamp['sdk_commit'] -ne $baseline.SDK_COMMIT) { $problems.Add("runtime was not built from ReXGlue $($baseline.SDK_TAG) ($($baseline.SDK_COMMIT))") }
    if ($stamp['patch_sha256'] -ne (Get-FileSha256 $RuntimePatchFile)) { $problems.Add('runtime was built with a different patch than patches\rexglue-sdk (rebuild it)') }
    $official = Get-OfficialRuntimeHashes $baseline
    foreach ($leaf in $RuntimeFiles) {
        $path = Join-Path $SdkDir $leaf
        if (-not $hashes.ContainsKey($leaf)) { $problems.Add("$leaf is not in $RuntimeStampName"); continue }
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { $problems.Add("missing: $leaf"); continue }
        $hash = Get-FileSha256 $path
        if ($hash -ne $hashes[$leaf]) { $problems.Add("$leaf does not match the patched build") }
        if ($hash -eq $official[$leaf]) { $problems.Add("$leaf is the stock ReXGlue release file") }
    }
    $officialGpu = Get-OfficialGpuHashes $baseline
    foreach ($leaf in $officialGpu.Keys) {
        $path = Join-Path $SdkDir $leaf
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { $problems.Add("missing: $leaf"); continue }
        if ((Get-FileSha256 $path) -ne $officialGpu[$leaf]) { $problems.Add("$leaf is not the stock ReXGlue release file") }
    }
    return $problems
}
