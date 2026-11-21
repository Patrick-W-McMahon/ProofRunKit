# ProofRunKit x64 MSI: configure and build the release preset, then package the MSI.
# Never stashes, resets, or discards local changes: aborts if the working tree is dirty.
# Does not install WiX or the .NET SDK; if they are missing, prints the exact install
# commands and exits.
#
# Usage (from a Developer PowerShell or any PowerShell):
#   .\installer\build-msi-x64.ps1
#   .\installer\build-msi-x64.ps1 -RepoRoot C:\path\to\ProofRunKit
#
# Refs #37, Refs #40.
[CmdletBinding()]
param(
    [string]$RepoRoot = ''
)

$ErrorActionPreference = 'Stop'
$Preset = 'windows-msvc-x64-release'

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

function Find-WixExecutable {
    $cmd = Get-Command wix -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $candidates = @(
        (Join-Path $env:USERPROFILE '.dotnet\tools\wix.exe'),
        (Join-Path $env:HOME '.dotnet\tools\wix.exe')
    )
    foreach ($path in $candidates) {
        if ($path -and (Test-Path $path)) { return $path }
    }
    return $null
}

function Get-WixInstallHelp {
    return @"
The WiX Toolset v4 command-line tool is required to build the MSI, and the .NET SDK
(6 or later; 8 recommended) is required to install that tool.

1. If ``dotnet`` is not available, install the .NET SDK from https://dot.net
   or with:

     winget install Microsoft.DotNet.SDK.8

2. Then install the pinned WiX v4 tool and its UI extension (this script will
   not install them for you):

     dotnet tool install --global wix --version 4.0.6
     wix extension add -g WixToolset.UI.wixext --version 4.0.6

3. Close and reopen the terminal so %USERPROFILE%\.dotnet\tools is on PATH,
   then re-run this script.
"@
}

function Assert-WixV4([string]$WixPath) {
    $raw = & $WixPath --version
    if ($LASTEXITCODE -ne 0) { throw "``$WixPath --version`` failed with exit code $LASTEXITCODE" }
    $text = ($raw | Out-String).Trim()
    $token = ($text -split '\s+')[0].TrimStart('v', 'V')
    Write-Host "== WiX: $text  ($WixPath)"
    if ($token -notmatch '^4\.') {
        throw @"
WiX $text is not v4. This project pins WiX Toolset v4 (4.0.6), which is still
published on NuGet. Install the pinned version:

  dotnet tool uninstall --global wix
  dotnet tool install --global wix --version 4.0.6
  wix extension add -g WixToolset.UI.wixext --version 4.0.6
"@
    }
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

$wix = Find-WixExecutable
if (-not $wix) {
    Write-Host 'ABORT: WiX Toolset v4 was not found. Nothing was built.' -ForegroundColor Red
    $dotnet = Get-Command dotnet -ErrorAction SilentlyContinue
    if ($dotnet) {
        Write-Host "== dotnet: $((& $dotnet.Source --version) 2>$null)"
        Write-Host 'The .NET SDK is present. Install WiX v4 with the commands below.'
    } else {
        Write-Host '== dotnet: NOT FOUND  (the .NET SDK is required to install the WiX tool)'
    }
    Write-Host (Get-WixInstallHelp)
    exit 1
}
Assert-WixV4 $wix

Set-Location $RepoRoot
Write-Host "== HEAD: $(git -C $RepoRoot log --oneline -1)"
Write-Host "== branch: $(git -C $RepoRoot rev-parse --abbrev-ref HEAD)"

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw "vswhere.exe was not found at $vswhere" }
$vsPath = & $vswhere -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'Visual Studio with the C++ x64 tools was not found by vswhere.' }
Write-Host "== Visual Studio: $vsPath"
& (Join-Path $vsPath 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
Set-Location $RepoRoot
Write-Host "== cl: $((Get-Command cl.exe).Source)"

# Launch-VsDevShell may reset PATH; rediscover wix and put the dotnet tools dir on PATH.
$wix = Find-WixExecutable
if (-not $wix) {
    $dotnetTools = Join-Path $env:USERPROFILE '.dotnet\tools'
    if (Test-Path $dotnetTools) { $env:PATH = "$dotnetTools;$env:PATH" }
    $wix = Find-WixExecutable
}
if (-not $wix) {
    Write-Host 'ABORT: WiX was found before the Visual Studio shell started, but not after.' -ForegroundColor Red
    Write-Host (Get-WixInstallHelp)
    exit 1
}
$env:PATH = "$(Split-Path -Parent $wix);$env:PATH"
Assert-WixV4 $wix

Invoke-Checked 'configure' { cmake --preset $Preset }
Invoke-Checked 'build'     { cmake --build --preset $Preset }
Invoke-Checked 'package-msi' { cmake --build --preset $Preset --target package-msi }

$msi = Join-Path $RepoRoot "build\$Preset\installer\ProofRunKit-*-win-x64.msi"
$built = Get-Item $msi -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $built) { throw "The MSI was not found at $msi" }
Write-Host "== MSI: $($built.FullName)"
Write-Host "== size: $($built.Length) bytes"

# Checksum for `prk -update` (#75): "<sha256>  <file name>" next to the MSI, ASCII, no BOM. Attach both files
# to the GitHub release; prk refuses to install an MSI without its .sha256.
$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $built.FullName).Hash.ToLowerInvariant()
$checksumPath = "$($built.FullName).sha256"
[System.IO.File]::WriteAllText($checksumPath, "$hash  $($built.Name)`n", [System.Text.Encoding]::ASCII)
Write-Host "== SHA-256: $hash"
Write-Host "== checksum: $checksumPath"
Write-Host '== Done: release configure, build, and package-msi succeeded.' -ForegroundColor Green
Write-Host "   Verify without installing:  .\installer\verify-msi-x64.ps1 -MsiPath `"$($built.FullName)`""
