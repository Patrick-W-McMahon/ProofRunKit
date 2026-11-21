# ProofRunKit x86: configure, build, and test with MSVC (Visual Studio 18 2026, Win32).
# Never stashes, resets, or discards local changes: aborts if the working tree is dirty.
# Does not fetch, checkout, or pull; run it from the branch you want to test
# (feat/x86-installer).
#
# Usage:
#   .\installer\build-x86.ps1
#   .\installer\build-x86.ps1 -RepoRoot C:\path\to\ProofRunKit
#
# Refs #39, Refs #60.
[CmdletBinding()]
param(
    [string]$RepoRoot = ''
)

$ErrorActionPreference = 'Stop'
$Preset = 'windows-msvc-x86-debug'

function Invoke-Checked([string]$What, [scriptblock]$Command) {
    & $Command
    if ($LASTEXITCODE -ne 0) { throw "$What failed with exit code $LASTEXITCODE" }
}

function Resolve-RepoRoot([string]$Hint) {
    if ($Hint) {
        $resolved = (Resolve-Path -LiteralPath $Hint).Path
        if (-not (Test-Path (Join-Path $resolved 'CMakeLists.txt'))) {
            throw "CMakeLists.txt was not found in -RepoRoot $resolved"
        }
        return $resolved
    }
    if ($PSScriptRoot) {
        $fromScript = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
        if (Test-Path (Join-Path $fromScript 'CMakeLists.txt')) { return $fromScript }
        if (Test-Path (Join-Path $PSScriptRoot 'CMakeLists.txt')) { return (Resolve-Path $PSScriptRoot).Path }
    }
    $defaultClone = 'C:\Users\patri\source\repos\ProofRunKit'
    if (Test-Path (Join-Path $defaultClone 'CMakeLists.txt')) { return $defaultClone }
    throw 'Could not find the ProofRunKit repository. Pass -RepoRoot <path>.'
}

$RepoRoot = Resolve-RepoRoot $RepoRoot
Write-Host "== Repo: $RepoRoot"

$dirty = git -C $RepoRoot status --porcelain
if ($LASTEXITCODE -ne 0) { throw "git status failed in $RepoRoot" }
if ($dirty) {
    Write-Host "ABORT: $RepoRoot has uncommitted changes. Nothing was touched:" -ForegroundColor Red
    $dirty
    exit 1
}

Set-Location $RepoRoot
Write-Host "== HEAD: $(git -C $RepoRoot log --oneline -1)"
Write-Host "== branch: $(git -C $RepoRoot rev-parse --abbrev-ref HEAD)"

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw "vswhere.exe was not found at $vswhere" }
$vsPath = & $vswhere -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'Visual Studio with the C++ x86/x64 tools was not found by vswhere.' }
Write-Host "== Visual Studio: $vsPath"
& (Join-Path $vsPath 'Common7\Tools\Launch-VsDevShell.ps1') -Arch x86 -HostArch amd64 -SkipAutomaticLocation
Set-Location $RepoRoot
Write-Host "== cl: $((Get-Command cl.exe).Source)"
Write-Host "== ml: $((Get-Command ml.exe).Source)"

Invoke-Checked 'configure' { cmake --preset $Preset }
Invoke-Checked 'build'     { cmake --build --preset $Preset }
Invoke-Checked 'ctest'     { ctest --preset $Preset --output-on-failure }
& ".\build\$Preset\Debug\proofrunkit.exe" --version
& ".\build\$Preset\Debug\prk.exe" version
# The C++ self-test runs inside `prk test` (CTest prk_test_suite, #96); no separate run.
Write-Host '== Done: configure, build, and CTest all succeeded.' -ForegroundColor Green
