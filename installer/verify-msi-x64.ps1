# ProofRunKit x64 MSI verification (READ-ONLY): inspects the package and optionally
# extracts it with msiexec /a. Never runs msiexec /i and never changes the
# working tree.
#
# Usage:
#   .\installer\verify-msi-x64.ps1
#   .\installer\verify-msi-x64.ps1 -MsiPath path\to\ProofRunKit-0.1.0-win-x64.msi
#
# Refs #37, Refs #40.
[CmdletBinding()]
param(
    [string]$MsiPath = '',
    [string]$RepoRoot = ''
)

$ErrorActionPreference = 'Stop'
$ExpectedUpgradeCode = '{FB358981-835D-421D-8634-BA130FE100A2}'
$ExpectedPublisher = 'ProofRunKit'
$ExpectedProduct = 'ProofRunKit'
$ExpectedUrl = 'https://github.com/Patrick-W-McMahon/ProofRunKit'
$failures = New-Object System.Collections.Generic.List[string]

function Resolve-RepoRoot([string]$Hint) {
    if ($Hint) { return (Resolve-Path -LiteralPath $Hint).Path }
    if ($PSScriptRoot) {
        $fromScript = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
        if (Test-Path (Join-Path $fromScript 'CMakeLists.txt')) { return $fromScript }
        if (Test-Path (Join-Path $PSScriptRoot 'CMakeLists.txt')) { return (Resolve-Path $PSScriptRoot).Path }
    }
    $defaultClone = 'C:\Users\patri\source\repos\ProofRunKit'
    if (Test-Path (Join-Path $defaultClone 'CMakeLists.txt')) { return $defaultClone }
    return $null
}

function Resolve-MsiPath([string]$Hint, [string]$Root) {
    if ($Hint) { return (Resolve-Path -LiteralPath $Hint).Path }
    if (-not $Root) { throw 'Pass -MsiPath <file.msi> (the repo root could not be found).' }
    $pattern = Join-Path $Root 'build\windows-msvc-x64-release\installer\ProofRunKit-*-win-x64.msi'
    $msiFiles = @(Get-Item $pattern -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending)
    if ($msiFiles.Count -eq 0) {
        throw "No MSI found at $pattern. Build it with .\installer\build-msi-x64.ps1 or pass -MsiPath."
    }
    return $msiFiles[0].FullName
}

function Get-MsiProperty([object]$Database, [string]$Name) {
    $literal = "'" + $Name.Replace("'", "''") + "'"
    $sql = 'SELECT `Value` FROM `Property` WHERE `Property` = ' + $literal
    $view = $Database.OpenView($sql)
    $view.Execute() | Out-Null
    $record = $view.Fetch()
    $value = $null
    if ($record) { $value = $record.StringData(1) }
    $view.Close()
    return $value
}

function Get-MsiTableRows([object]$Database, [string]$Sql, [int]$Columns) {
    # Use a List so a single-row result is not unwrapped by PowerShell.
    $view = $Database.OpenView($Sql)
    $view.Execute() | Out-Null
    $rows = [System.Collections.Generic.List[object]]::new()
    while ($true) {
        $record = $view.Fetch()
        if (-not $record) { break }
        $row = @()
        for ($i = 1; $i -le $Columns; $i++) { $row += $record.StringData($i) }
        $rows.Add($row)
    }
    $view.Close()
    return $rows
}

function Normalize-Guid([string]$Value) {
    if ([string]::IsNullOrWhiteSpace($Value)) { return '' }
    try { return ([Guid]$Value).ToString('B').ToUpperInvariant() }
    catch { return $Value.Trim().ToUpperInvariant() }
}

function Assert-Equal([string]$Label, [string]$Actual, [string]$Expected) {
    if ($Actual -ne $Expected) {
        $script:failures.Add("${Label}: expected '$Expected', got '$Actual'")
        Write-Host "FAIL  $Label" -ForegroundColor Red
        Write-Host "      expected: $Expected"
        Write-Host "      actual:   $Actual"
    } else {
        Write-Host "OK    $Label = $Actual" -ForegroundColor Green
    }
}

function Assert-True([string]$Label, [bool]$Condition, [string]$Detail) {
    if (-not $Condition) {
        $script:failures.Add("${Label}: $Detail")
        Write-Host "FAIL  $Label  ($Detail)" -ForegroundColor Red
    } else {
        Write-Host "OK    $Label  ($Detail)" -ForegroundColor Green
    }
}

$RepoRoot = Resolve-RepoRoot $RepoRoot
$MsiPath = Resolve-MsiPath $MsiPath $RepoRoot
Write-Host "== MSI: $MsiPath"
Write-Host "== size: $((Get-Item -LiteralPath $MsiPath).Length) bytes"
Write-Host '== This script does not install the product (no msiexec /i).'

$installer = New-Object -ComObject WindowsInstaller.Installer
$database = $installer.OpenDatabase($MsiPath, 0)

# Summary information: template (PID_TEMPLATE = 7) carries the platform.
$summary = $installer.SummaryInformation($MsiPath, 0)
$template = $summary.Property(7)
$author = $summary.Property(4)
$title = $summary.Property(2)
Write-Host "== Summary Title: $title"
Write-Host "== Summary Author (publisher): $author"
Write-Host "== Summary Template: $template"
Assert-True 'Template is x64' ($template -match '(?i)(^|;|\s)x64') "Template='$template' must include x64 (not Intel / x86)"
if ($author) { Assert-Equal 'Summary Author' $author $ExpectedPublisher }

$productName = Get-MsiProperty $database 'ProductName'
$manufacturer = Get-MsiProperty $database 'Manufacturer'
$productVersion = Get-MsiProperty $database 'ProductVersion'
$upgradeCode = Get-MsiProperty $database 'UpgradeCode'
$allUsers = Get-MsiProperty $database 'ALLUSERS'
$urlAbout = Get-MsiProperty $database 'ARPURLINFOABOUT'
$helpLink = Get-MsiProperty $database 'ARPHELPLINK'
$productCode = Get-MsiProperty $database 'ProductCode'

Write-Host "== ProductCode: $productCode  (generated per build; used with MajorUpgrade)"
Assert-Equal 'ProductName' $productName $ExpectedProduct
Assert-Equal 'Manufacturer (ARP publisher)' $manufacturer $ExpectedPublisher
Assert-True 'ProductVersion is set' (-not [string]::IsNullOrWhiteSpace($productVersion)) "ProductVersion='$productVersion'"
Assert-Equal 'UpgradeCode' (Normalize-Guid $upgradeCode) $ExpectedUpgradeCode
Assert-True 'ALLUSERS (per-machine)' ($allUsers -in @('1','2')) "ALLUSERS='$allUsers' (1 or 2 means per-machine)"
Assert-Equal 'ARPURLINFOABOUT' $urlAbout $ExpectedUrl
Assert-Equal 'ARPHELPLINK' $helpLink $ExpectedUrl

# Directory table: INSTALLFOLDER must be ProofRunKit under ProgramFiles64Folder.
$directories = Get-MsiTableRows $database 'SELECT `Directory`, `Directory_Parent`, `DefaultDir` FROM `Directory`' 3
$dirMap = @{}
foreach ($row in $directories) { $dirMap[$row[0]] = @{ Parent = $row[1]; DefaultDir = $row[2] } }
Write-Host '== Directory table:'
foreach ($row in $directories) { Write-Host "   $($row[0])  parent=$($row[1])  DefaultDir=$($row[2])" }

Assert-True 'ProgramFiles64Folder present' ($dirMap.ContainsKey('ProgramFiles64Folder')) 'ProgramFiles64Folder row exists'
Assert-True 'INSTALLFOLDER present' ($dirMap.ContainsKey('INSTALLFOLDER')) 'INSTALLFOLDER row exists'
if ($dirMap.ContainsKey('INSTALLFOLDER')) {
    $install = $dirMap['INSTALLFOLDER']
    Assert-Equal 'INSTALLFOLDER parent' $install.Parent 'ProgramFiles64Folder'
    $leaf = $install.DefaultDir
    if ($leaf -match '\|') { $leaf = ($leaf -split '\|', 2)[1] }
    Assert-Equal 'INSTALLFOLDER name' $leaf 'ProofRunKit'
}

# Payload: proofrunkit.exe and LICENSE.txt, and no self-test or packer leftovers.
$files = Get-MsiTableRows $database 'SELECT `File`, `Component_`, `FileName` FROM `File`' 3
$fileNames = @()
foreach ($row in $files) {
    $name = $row[2]
    if ($name -match '\|') { $name = ($name -split '\|', 2)[1] }
    $fileNames += $name
    Write-Host "   File: $name  (component $($row[1]))"
}
Assert-True 'proofrunkit.exe is packaged' ($fileNames -contains 'proofrunkit.exe') "files=$($fileNames -join ', ')"
Assert-True 'LICENSE.txt is packaged' ($fileNames -contains 'LICENSE.txt') "files=$($fileNames -join ', ')"
$unexpected = @($fileNames | Where-Object { $_ -match 'selftest|\.pdb$|\.lib$' })
Assert-True 'Self-test / PDB / .lib not packaged' ($unexpected.Count -eq 0) "unexpected=$($unexpected -join ', ')"

# Optional PATH feature (default on: Level 1).
$features = Get-MsiTableRows $database 'SELECT `Feature`, `Feature_Parent`, `Title`, `Level` FROM `Feature`' 4
$addToPath = $features | Where-Object { $_[0] -eq 'AddToPath' } | Select-Object -First 1
$productFeature = $features | Where-Object { $_[0] -eq 'ProductFeature' } | Select-Object -First 1
Assert-True 'ProductFeature exists' ($null -ne $productFeature) 'ProductFeature row'
Assert-True 'AddToPath feature exists' ($null -ne $addToPath) 'AddToPath row'
if ($addToPath) {
    Assert-Equal 'AddToPath default Level (1 = included)' $addToPath[3] '1'
}

# System PATH environment value points at INSTALLFOLDER.
$envRows = @()
try { $envRows = Get-MsiTableRows $database 'SELECT `Name`, `Value`, `Component_` FROM `Environment`' 3 } catch { $envRows = @() }
$pathRow = $envRows | Where-Object { $_[0] -match 'PATH' } | Select-Object -First 1
Assert-True 'PATH environment entry exists' ($null -ne $pathRow) 'Environment table has a PATH row'
if ($pathRow) {
    Write-Host "   Environment: Name='$($pathRow[0])' Value='$($pathRow[1])'"
    Assert-True 'PATH value is the install folder' ($pathRow[1] -match 'INSTALLFOLDER') "Value='$($pathRow[1])'"
    Assert-True 'PATH is a system variable' ($pathRow[0] -match '\*') "Name='$($pathRow[0])' should include * (system)"
}

# Launch condition requires 64-bit Windows.
$launches = @()
try { $launches = Get-MsiTableRows $database 'SELECT `Condition`, `Description` FROM `LaunchCondition`' 2 } catch { $launches = @() }
$nt64 = $launches | Where-Object { $_[0] -match 'VersionNT64' } | Select-Object -First 1
Assert-True 'Launch condition VersionNT64' ($null -ne $nt64) "conditions=$($launches | ForEach-Object { $_[0] })"

# Administrative extraction (still not an install): confirms the payload files.
$extractRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("prk-msi-admin-" + [Guid]::NewGuid().ToString('n'))
New-Item -ItemType Directory -Path $extractRoot | Out-Null
Write-Host "== Administrative extract (msiexec /a, TARGETDIR=$extractRoot)"
try {
    $proc = Start-Process -FilePath 'msiexec.exe' -ArgumentList @('/a', $MsiPath, '/qn', "TARGETDIR=$extractRoot") -Wait -PassThru -WindowStyle Hidden
    if ($proc.ExitCode -ne 0) {
        $failures.Add("msiexec /a exited $($proc.ExitCode). COM checks above still apply.")
        Write-Host "FAIL  msiexec /a exit code $($proc.ExitCode)" -ForegroundColor Red
    } else {
        $extractedExe = Get-ChildItem -LiteralPath $extractRoot -Recurse -Filter 'proofrunkit.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
        $extractedLic = Get-ChildItem -LiteralPath $extractRoot -Recurse -Filter 'LICENSE.txt' -ErrorAction SilentlyContinue | Select-Object -First 1
        Assert-True 'Extracted proofrunkit.exe' ($null -ne $extractedExe) $(if ($extractedExe) { $extractedExe.FullName } else { "not found under $extractRoot" })
        Assert-True 'Extracted LICENSE.txt' ($null -ne $extractedLic) $(if ($extractedLic) { $extractedLic.FullName } else { "not found under $extractRoot" })
        Write-Host '   (Administrative TARGETDIR is a temporary folder; the real install destination is ProgramFiles64Folder\ProofRunKit, verified from the Directory table above.)'
    }
} finally {
    if (Test-Path $extractRoot) { Remove-Item -LiteralPath $extractRoot -Recurse -Force -ErrorAction SilentlyContinue }
}

[System.Runtime.InteropServices.Marshal]::ReleaseComObject($database) | Out-Null
[System.Runtime.InteropServices.Marshal]::ReleaseComObject($installer) | Out-Null

if ($failures.Count -gt 0) {
    Write-Host "FAILED: $($failures.Count) check(s) did not pass:" -ForegroundColor Red
    foreach ($item in $failures) { Write-Host " - $item" }
    exit 1
}

Write-Host '== All checks passed. Destination is ProgramFiles64Folder\ProofRunKit; MSI was not installed.' -ForegroundColor Green
