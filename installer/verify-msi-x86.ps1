# ProofRunKit x86 MSI verification (READ-ONLY): inspects the package and optionally
# extracts it with msiexec /a. Never runs msiexec /i and never changes the
# working tree.
#
# Usage:
#   .\installer\verify-msi-x86.ps1
#   .\installer\verify-msi-x86.ps1 -MsiPath path\to\ProofRunKit-0.1.0-win-x86.msi
#
# Compatible with Windows PowerShell 5.1 and PowerShell 7.
# Refs #38, Refs #40, Refs #51, Refs #52, Refs #53.
[CmdletBinding()]
param(
    [string]$MsiPath = '',
    [string]$RepoRoot = ''
)

$ErrorActionPreference = 'Stop'
$ExpectedUpgradeCode = '{A31020BF-969A-4139-9FA1-82E044AC31F5}'
$ExpectedPublisher = 'ProofRunKit'
$ExpectedProduct = 'ProofRunKit (32-bit)'
$ExpectedUrl = 'https://github.com/Patrick-W-McMahon/ProofRunKit'
$failures = New-Object 'System.Collections.Generic.List[string]'

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
    $pattern = Join-Path $Root 'build\windows-msvc-x86-release\installer\ProofRunKit-*-win-x86.msi'
    $msiFiles = @(Get-Item $pattern -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending)
    if ($msiFiles.Count -eq 0) {
        throw "No MSI found at $pattern. Build it with .\installer\build-msi-x86.ps1 or pass -MsiPath."
    }
    return $msiFiles[0].FullName
}

# Windows Installer COM parameterized methods/properties are unreliable when
# called with $obj.Name(args) in PowerShell. Route them through InvokeMember.
# Callers must assign or [void] the result: a VT_EMPTY/$null return would
# otherwise leak into a function's output and stringify as a leading space
# (Refs #51).
function Invoke-MsiComMethod {
    param(
        [Parameter(Mandatory = $true)][object]$Object,
        [Parameter(Mandatory = $true)][string]$Name,
        [object[]]$ArgumentList = @()
    )
    return [System.__ComObject].InvokeMember(
        $Name,
        [System.Reflection.BindingFlags]::InvokeMethod,
        $null,
        $Object,
        $ArgumentList)
}

function Get-MsiComProperty {
    param(
        [Parameter(Mandatory = $true)][object]$Object,
        [Parameter(Mandatory = $true)][string]$Name,
        [object[]]$ArgumentList = @()
    )
    return [System.__ComObject].InvokeMember(
        $Name,
        [System.Reflection.BindingFlags]::GetProperty,
        $null,
        $Object,
        $ArgumentList)
}

function Close-MsiView([object]$View) {
    if ($null -eq $View) { return }
    [void](Invoke-MsiComMethod -Object $View -Name 'Close')
    [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($View)
}

# Returns one [string] (empty if the property is absent). Any COM $null is
# discarded inside the subexpression so it cannot become a leading space.
function Get-MsiProperty([object]$Database, [string]$Name) {
    $value = [string]::Empty
    $null = $(
        $literal = "'" + $Name.Replace("'", "''") + "'"
        $sql = 'SELECT `Value` FROM `Property` WHERE `Property` = ' + $literal
        $view = Invoke-MsiComMethod -Object $Database -Name 'OpenView' -ArgumentList @($sql)
        [void](Invoke-MsiComMethod -Object $view -Name 'Execute')
        $record = Invoke-MsiComMethod -Object $view -Name 'Fetch'
        if ($null -ne $record) {
            $raw = Get-MsiComProperty -Object $record -Name 'StringData' -ArgumentList @(1)
            if ($null -ne $raw) { $value = ([string]$raw).Trim() }
            [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($record)
        }
        Close-MsiView $view
    )
    return [string]$value
}

# Returns a List of string[]; never $null. COM output is discarded so a leaked
# empty value cannot appear as a $null row (which would crash $row[0]).
function Get-MsiTableRows([object]$Database, [string]$Sql, [int]$Columns) {
    $rows = New-Object 'System.Collections.Generic.List[object]'
    $null = $(
        $view = Invoke-MsiComMethod -Object $Database -Name 'OpenView' -ArgumentList @($Sql)
        [void](Invoke-MsiComMethod -Object $view -Name 'Execute')
        while ($true) {
            $record = Invoke-MsiComMethod -Object $view -Name 'Fetch'
            if ($null -eq $record) { break }
            $row = New-Object 'System.Collections.Generic.List[string]'
            for ($i = 1; $i -le $Columns; $i++) {
                $cell = Get-MsiComProperty -Object $record -Name 'StringData' -ArgumentList @($i)
                if ($null -eq $cell) { [void]$row.Add([string]::Empty) }
                else { [void]$row.Add(([string]$cell).Trim()) }
            }
            [void]$rows.Add($row.ToArray())
            [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($record)
        }
        Close-MsiView $view
    )
    # Comma operator: keep a one-row result from being unrolled into its cells.
    return ,$rows
}

function Normalize-Guid([string]$Value) {
    if ([string]::IsNullOrWhiteSpace($Value)) { return [string]::Empty }
    $trimmed = $Value.Trim()
    try { return ([Guid]$trimmed).ToString('B').ToUpperInvariant() }
    catch { return $trimmed.ToUpperInvariant() }
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
$database = Invoke-MsiComMethod -Object $installer -Name 'OpenDatabase' -ArgumentList @($MsiPath, 0)

# Summary information: template (PID_TEMPLATE = 7) carries the platform.
$summary = Get-MsiComProperty -Object $installer -Name 'SummaryInformation' -ArgumentList @($MsiPath, 0)
$template = [string](Get-MsiComProperty -Object $summary -Name 'Property' -ArgumentList @(7))
$author = [string](Get-MsiComProperty -Object $summary -Name 'Property' -ArgumentList @(4))
$title = [string](Get-MsiComProperty -Object $summary -Name 'Property' -ArgumentList @(2))
if ($null -ne $template) { $template = $template.Trim() }
if ($null -ne $author) { $author = $author.Trim() }
if ($null -ne $title) { $title = $title.Trim() }
Write-Host "== Summary Title: $title"
Write-Host "== Summary Author (publisher): $author"
Write-Host "== Summary Template: $template"
Assert-True 'Template is Intel (x86)' ($template -match '(?i)^Intel(;|$)') "Template='$template' must be Intel;1033 (32-bit), not x64"
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
Assert-True 'ALLUSERS (per-machine)' (@('1', '2') -contains $allUsers) "ALLUSERS='$allUsers' (1 or 2 means per-machine)"
Assert-Equal 'ARPURLINFOABOUT' $urlAbout $ExpectedUrl
Assert-Equal 'ARPHELPLINK' $helpLink $ExpectedUrl

# Directory table: INSTALLFOLDER must be ProofRunKit under ProgramFilesFolder.
$directories = Get-MsiTableRows $database 'SELECT `Directory`, `Directory_Parent`, `DefaultDir` FROM `Directory`' 3
$dirMap = @{}
foreach ($row in $directories) {
    if ($null -eq $row -or $row.Length -lt 3) { continue }
    $dirMap[$row[0]] = @{ Parent = $row[1]; DefaultDir = $row[2] }
}
Write-Host '== Directory table:'
foreach ($row in $directories) {
    if ($null -eq $row -or $row.Length -lt 3) { continue }
    Write-Host "   $($row[0])  parent=$($row[1])  DefaultDir=$($row[2])"
}

Assert-True 'ProgramFilesFolder present' ($dirMap.ContainsKey('ProgramFilesFolder')) 'ProgramFilesFolder row exists'
Assert-True 'ProgramFiles64Folder is not used' (-not $dirMap.ContainsKey('ProgramFiles64Folder')) '32-bit MSI must not use ProgramFiles64Folder'
Assert-True 'INSTALLFOLDER present' ($dirMap.ContainsKey('INSTALLFOLDER')) 'INSTALLFOLDER row exists'
if ($dirMap.ContainsKey('INSTALLFOLDER')) {
    $install = $dirMap['INSTALLFOLDER']
    Assert-Equal 'INSTALLFOLDER parent' $install.Parent 'ProgramFilesFolder'
    $leaf = $install.DefaultDir
    if ($leaf -match '\|') { $leaf = ($leaf -split '\|', 2)[1] }
    Assert-Equal 'INSTALLFOLDER name' $leaf 'ProofRunKit'
}

# Payload: proofrunkit.exe and LICENSE.txt, and no self-test or packer leftovers.
$files = Get-MsiTableRows $database 'SELECT `File`, `Component_`, `FileName` FROM `File`' 3
$fileNames = New-Object 'System.Collections.Generic.List[string]'
foreach ($row in $files) {
    if ($null -eq $row -or $row.Length -lt 3) { continue }
    $name = $row[2]
    if ($name -match '\|') { $name = ($name -split '\|', 2)[1] }
    [void]$fileNames.Add($name)
    Write-Host "   File: $name  (component $($row[1]))"
}
Assert-True 'proofrunkit.exe is packaged' ($fileNames.Contains('proofrunkit.exe')) "files=$($fileNames -join ', ')"
Assert-True 'LICENSE.txt is packaged' ($fileNames.Contains('LICENSE.txt')) "files=$($fileNames -join ', ')"
$unexpected = @($fileNames | Where-Object { $_ -match 'selftest|\.pdb$|\.lib$' })
Assert-True 'Self-test / PDB / .lib not packaged' ($unexpected.Count -eq 0) "unexpected=$($unexpected -join ', ')"

# Optional PATH feature (default on: Level 1).
$features = Get-MsiTableRows $database 'SELECT `Feature`, `Feature_Parent`, `Title`, `Level` FROM `Feature`' 4
$addToPath = $null
$productFeature = $null
foreach ($row in $features) {
    if ($null -eq $row -or $row.Length -lt 4) { continue }
    if ($row[0] -eq 'AddToPath') { $addToPath = $row }
    if ($row[0] -eq 'ProductFeature') { $productFeature = $row }
}
Assert-True 'ProductFeature exists' ($null -ne $productFeature) 'ProductFeature row'
Assert-True 'AddToPath feature exists' ($null -ne $addToPath) 'AddToPath row'
if ($null -ne $addToPath) {
    Assert-Equal 'AddToPath default Level (1 = included)' $addToPath[3] '1'
}

# System PATH environment value points at INSTALLFOLDER.
$envRows = New-Object 'System.Collections.Generic.List[object]'
try { $envRows = Get-MsiTableRows $database 'SELECT `Name`, `Value`, `Component_` FROM `Environment`' 3 } catch { $envRows = New-Object 'System.Collections.Generic.List[object]' }
$pathRow = $null
foreach ($row in $envRows) {
    if ($null -eq $row -or $row.Length -lt 2) { continue }
    if ($row[0] -match 'PATH') { $pathRow = $row; break }
}
Assert-True 'PATH environment entry exists' ($null -ne $pathRow) 'Environment table has a PATH row'
if ($null -ne $pathRow) {
    Write-Host "   Environment: Name='$($pathRow[0])' Value='$($pathRow[1])'"
    Assert-True 'PATH value is the install folder' ($pathRow[1] -match 'INSTALLFOLDER') "Value='$($pathRow[1])'"
    Assert-True 'PATH is a system variable' ($pathRow[0] -match '\*') "Name='$($pathRow[0])' should include * (system)"
}

# Launch condition requires 64-bit Windows.
$launches = New-Object 'System.Collections.Generic.List[object]'
try { $launches = Get-MsiTableRows $database 'SELECT `Condition`, `Description` FROM `LaunchCondition`' 2 } catch { $launches = New-Object 'System.Collections.Generic.List[object]' }
$nt64 = $null
$launchConditions = New-Object 'System.Collections.Generic.List[string]'
foreach ($row in $launches) {
    if ($null -eq $row -or $row.Length -lt 1) { continue }
    [void]$launchConditions.Add([string]$row[0])
    if ($row[0] -match 'VersionNT64') { $nt64 = $row }
}
Assert-True 'No VersionNT64 launch condition' ($null -eq $nt64) "conditions=$($launchConditions -join ', ') (must be installable on 32-bit Windows)"

# Administrative extraction (still not an install): confirms the payload files.
$extractRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("prk-msi-admin-" + [Guid]::NewGuid().ToString('n'))
[void](New-Item -ItemType Directory -Path $extractRoot)
Write-Host "== Administrative extract (msiexec /a, TARGETDIR=$extractRoot)"
try {
    $proc = Start-Process -FilePath 'msiexec.exe' -ArgumentList @('/a', $MsiPath, '/qn', "TARGETDIR=$extractRoot") -Wait -PassThru -WindowStyle Hidden
    if ($proc.ExitCode -ne 0) {
        $script:failures.Add("msiexec /a exited $($proc.ExitCode). COM checks above still apply.")
        Write-Host "FAIL  msiexec /a exit code $($proc.ExitCode)" -ForegroundColor Red
    } else {
        $extractedExe = Get-ChildItem -LiteralPath $extractRoot -Recurse -Filter 'proofrunkit.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
        $extractedLic = Get-ChildItem -LiteralPath $extractRoot -Recurse -Filter 'LICENSE.txt' -ErrorAction SilentlyContinue | Select-Object -First 1
        $exeDetail = "not found under $extractRoot"
        $licDetail = "not found under $extractRoot"
        if ($null -ne $extractedExe) { $exeDetail = $extractedExe.FullName }
        if ($null -ne $extractedLic) { $licDetail = $extractedLic.FullName }
        Assert-True 'Extracted proofrunkit.exe' ($null -ne $extractedExe) $exeDetail
        Assert-True 'Extracted LICENSE.txt' ($null -ne $extractedLic) $licDetail
        Write-Host '   (Administrative TARGETDIR is a temporary folder; the real install destination is ProgramFilesFolder\ProofRunKit (%ProgramFiles(x86)%\ProofRunKit on 64-bit Windows), verified from the Directory table above.)'
    }
} finally {
    if (Test-Path $extractRoot) { Remove-Item -LiteralPath $extractRoot -Recurse -Force -ErrorAction SilentlyContinue }
}

if ($null -ne $summary) { [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($summary) }
if ($null -ne $database) { [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($database) }
if ($null -ne $installer) { [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($installer) }

if ($failures.Count -gt 0) {
    Write-Host "FAILED: $($failures.Count) check(s) did not pass:" -ForegroundColor Red
    foreach ($item in $failures) { Write-Host " - $item" }
    exit 1
}

Write-Host '== All checks passed. Destination is ProgramFilesFolder\ProofRunKit; MSI was not installed.' -ForegroundColor Green
