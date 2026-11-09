<#
.SYNOPSIS
  Install or roll back the pinned Aurie + YYToolkit runtime for ChroniconSkillMeter.

.DESCRIPTION
  ChroniconSkillMeter.dll is built against the YYToolkit v3 headers shipped in
  atty303/chronicon-dps (Feb 2024). Loading it with YYToolkit v4.x crashes the
  game with 0xc0000005 inside YYToolkit.dll, because v4 changed the interface
  vtable and the RValue API (AsReal/AsBool/AsString -> ToDouble/ToBoolean/ToString,
  CInstance::at / operator[] removed, GetMember / GetRefMember added, etc).

  This script pins the exact runtime those headers were written for (x64 only):

    Aurie      v1.0.5   AurieLoader.exe + AurieCore.dll   (2024-01-15)
    YYToolkit  v3.2.3   YYToolkit.dll                     (2024-01-21)

  Nothing is deleted. The current stack is copied to
  <GameDir>\_ChroniconSkillMeter_backup before anything is replaced.

.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File install_runtime.ps1
  powershell -NoProfile -ExecutionPolicy Bypass -File install_runtime.ps1 -GameDir "D:\SteamLibrary\steamapps\common\Chronicon" -ModDll "C:\path\ChroniconSkillMeter.dll"
  powershell -NoProfile -ExecutionPolicy Bypass -File install_runtime.ps1 -UseControl
  powershell -NoProfile -ExecutionPolicy Bypass -File install_runtime.ps1 -Rollback
#>
[CmdletBinding()]
param(
    [string]$GameDir = 'D:\SteamLibrary\steamapps\common\Chronicon',
    [string]$ModDll = '',
    [switch]$UseControl,
    [switch]$Rollback,
    [switch]$NoDownload
)

$ErrorActionPreference = 'Stop'
try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch { }

$Root    = Split-Path -Parent $MyInvocation.MyCommand.Definition
$Cache   = Join-Path $Root 'dist\runtime'
$Mods    = Join-Path $GameDir 'mods'
$Native  = Join-Path $Mods 'native'
$Aurie   = Join-Path $Mods 'aurie'
$Backup  = Join-Path $GameDir '_ChroniconSkillMeter_backup'

if (-not (Test-Path -LiteralPath (Join-Path $GameDir 'Chronicon.exe'))) {
    throw "Chronicon.exe not found under '$GameDir'. Pass -GameDir with the real game folder."
}

# name, pinned release url, sha256, byte size
$Pinned = @(
    [pscustomobject]@{
        Name = 'AurieLoader.exe'
        Url  = 'https://github.com/AurieFramework/Aurie/releases/download/v1.0.5/AurieLoader.exe'
        Sha  = '8CE89A0B518F65E4D0FEE21ECE39072D94B35C04EE13F8A887F54FFCD31A63F9'
        Size = 272896
    },
    [pscustomobject]@{
        Name = 'AurieCore.dll'
        Url  = 'https://github.com/AurieFramework/Aurie/releases/download/v1.0.5/AurieCore.dll'
        Sha  = '4B26CCF828BED538E10CAA250B812217B4F92EED37F7A01A081EB418869860ED'
        Size = 606720
    },
    [pscustomobject]@{
        Name = 'YYToolkit.dll'
        Url  = 'https://github.com/AurieFramework/YYToolkit/releases/download/v3.2.3/YYToolkit.dll'
        Sha  = '1AB7BB9413228B1C9AAC43928266443F4756FA59CA0CCA725077DB5CF1855B71'
        Size = 699904
    }
)

$Control = [pscustomobject]@{
    Name = 'ChroniconDPS.dll'
    Url  = 'https://github.com/atty303/chronicon-dps/releases/download/v1.0.0/ChroniconDPS.dll'
    Sha  = '53E66BDC68E1EB566181315FB64D58EBCDEC4304E0BD30F0FCBB3B85E5418788'
    Size = 209408
}

function Test-PinnedFile([string]$Path, [string]$ExpectedSha, [int]$ExpectedSize) {
    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    $item = Get-Item -LiteralPath $Path
    if ($item.Length -ne $ExpectedSize) { return $false }
    return ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -eq $ExpectedSha)
}

function Get-PinnedFile($Spec) {
    $target = Join-Path $Cache $Spec.Name
    if (Test-PinnedFile $target $Spec.Sha $Spec.Size) {
        Write-Host "  [cache ] $($Spec.Name)" -ForegroundColor DarkGray
        return $target
    }
    if ($NoDownload) { throw "$($Spec.Name) missing or hash mismatch, and -NoDownload was specified." }
    if (-not (Test-Path -LiteralPath $Cache)) { New-Item -ItemType Directory -Path $Cache -Force | Out-Null }

    for ($i = 1; $i -le 6; $i++) {
        Write-Host "  [download $i/6] $($Spec.Name)"
        try {
            Invoke-WebRequest -UseBasicParsing -Uri $Spec.Url -OutFile $target -TimeoutSec 90 | Out-Null
        } catch {
            Write-Host "    $($_.Exception.Message)" -ForegroundColor DarkYellow
        }
        if (Test-PinnedFile $target $Spec.Sha $Spec.Size) { return $target }
    }
    throw "Could not download a valid $($Spec.Name). Network may be blocked; download manually from $($Spec.Url) into '$Cache' and re-run."
}

function Assert-NoGameProcess {
    $busy = @(Get-Process -Name 'Chronicon', 'AurieLoader' -ErrorAction SilentlyContinue)
    if ($busy.Count -gt 0) {
        throw "Close the game first (running: $($busy.Name -join ', ')). Files cannot be replaced while loaded."
    }
}

function Backup-File([string]$Source) {
    if (-not (Test-Path -LiteralPath $Source)) { return }
    $rel = $Source.Substring($Mods.Length).TrimStart('\')
    $dest = Join-Path $Backup $rel
    if (Test-Path -LiteralPath $dest) { return }   # never overwrite an existing backup
    $dir = Split-Path -Parent $dest
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    Copy-Item -LiteralPath $Source -Destination $dest -Force
    Write-Host "  [backup] $rel"
}

function Backup-CurrentStack {
    if (-not (Test-Path -LiteralPath $Backup)) { New-Item -ItemType Directory -Path $Backup -Force | Out-Null }
    Backup-File (Join-Path $Mods 'AurieLoader.exe')
    Backup-File (Join-Path $Native 'AurieCore.dll')
    Backup-File (Join-Path $Aurie 'YYToolkit.dll')
    Backup-File (Join-Path $Aurie 'ChroniconSkillMeter.dll')
    Backup-File (Join-Path $Aurie 'ChroniconDPS.dll')
}

function New-ModsTree {
    foreach ($d in @($Mods, $Native, $Aurie)) {
        if (-not (Test-Path -LiteralPath $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
    }
}

function Show-Result {
    Write-Host "`nCurrent runtime:" -ForegroundColor Cyan
    foreach ($p in @((Join-Path $Mods 'AurieLoader.exe'), (Join-Path $Native 'AurieCore.dll'), (Join-Path $Aurie 'YYToolkit.dll'))) {
        if (Test-Path -LiteralPath $p) {
            '{0,-22} {1,9} bytes' -f (Split-Path $p -Leaf), (Get-Item -LiteralPath $p).Length
        }
    }
    Get-ChildItem -LiteralPath $Aurie -Filter '*.dll' | ForEach-Object { "aurie/{0,-16} {1,9} bytes" -f $_.Name, $_.Length }

    $ifeo = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\Chronicon.exe'
    if (Test-Path -LiteralPath $ifeo) {
        Get-ChildItem $ifeo -Recurse -ErrorAction SilentlyContinue | ForEach-Object {
            $d = (Get-Item $_.PSPath).GetValue('Debugger')
            if ($d) { "IFEO Debugger       : $d"; "IFEO target exists  : $(Test-Path -LiteralPath $d)" }
        }
    }
}

# ---------------------------------------------------------------- rollback
if ($Rollback) {
    Assert-NoGameProcess
    if (-not (Test-Path -LiteralPath $Backup)) { throw "No backup at '$Backup'." }
    Write-Host 'Rolling back to the backed-up stack:' -ForegroundColor Cyan
    New-ModsTree
    $pairs = @(
        @('AurieLoader.exe',           (Join-Path $Mods 'AurieLoader.exe')),
        @('native\AurieCore.dll',      (Join-Path $Native 'AurieCore.dll')),
        @('aurie\YYToolkit.dll',       (Join-Path $Aurie 'YYToolkit.dll')),
        @('aurie\ChroniconSkillMeter.dll', (Join-Path $Aurie 'ChroniconSkillMeter.dll'))
    )
    foreach ($pair in $pairs) {
        $src = Join-Path $Backup $pair[0]
        if (Test-Path -LiteralPath $src) {
            Copy-Item -LiteralPath $src -Destination $pair[1] -Force
            Write-Host "  [restore] $($pair[0])"
        }
    }
    $control = Join-Path $Aurie 'ChroniconDPS.dll'
    if (Test-Path -LiteralPath $control) {
        $dest = Join-Path $Backup "control-switched-$(Get-Date -Format yyyyMMdd-HHmmss)-ChroniconDPS.dll"
        Move-Item -LiteralPath $control -Destination $dest
        Write-Host "  [park   ] ChroniconDPS.dll -> $dest"
    }
    Show-Result
    Write-Host "`n`nRollback done. Launch the game from Steam to verify." -ForegroundColor Green
    return
}

# ---------------------------------------------------------------- install
Assert-NoGameProcess
Write-Host "Game folder : $GameDir" -ForegroundColor Cyan
Write-Host "Pinned stack: Aurie v1.0.5 + YYToolkit v3.2.3 (x64)" -ForegroundColor Cyan
Write-Host 'Resolving runtime files:' -ForegroundColor Cyan

$loader = Get-PinnedFile $Pinned[0]
$core   = Get-PinnedFile $Pinned[1]
$ytk    = Get-PinnedFile $Pinned[2]

if ($UseControl) {
    $mod = Get-PinnedFile $Control
} else {
    if ([string]::IsNullOrWhiteSpace($ModDll)) { $ModDll = Join-Path $Root 'dist\ChroniconSkillMeter.dll' }
    if (-not (Test-Path -LiteralPath $ModDll)) { $ModDll = Join-Path $Backup 'aurie\ChroniconSkillMeter.dll' }
    if (-not (Test-Path -LiteralPath $ModDll)) {
        throw 'ChroniconSkillMeter.dll not found. Build it (build.bat / GitHub Actions), or pass -ModDll <path>, or use -UseControl.'
    }
    $mod = $ModDll
}

Backup-CurrentStack
New-ModsTree

Write-Host 'Installing:' -ForegroundColor Cyan
Copy-Item -LiteralPath $loader -Destination (Join-Path $Mods 'AurieLoader.exe') -Force
Copy-Item -LiteralPath $core   -Destination (Join-Path $Native 'AurieCore.dll') -Force
Copy-Item -LiteralPath $ytk    -Destination (Join-Path $Aurie 'YYToolkit.dll') -Force
Write-Host '  [install] AurieLoader.exe / native\AurieCore.dll / aurie\YYToolkit.dll'

if ($UseControl) {
    # A/B test: park our DLL outside mods so only the known-good reference mod loads.
    $ours = Join-Path $Aurie 'ChroniconSkillMeter.dll'
    if (Test-Path -LiteralPath $ours) {
        $park = Join-Path $Backup "control-switched-$(Get-Date -Format yyyyMMdd-HHmmss)-ChroniconSkillMeter.dll"
        Move-Item -LiteralPath $ours -Destination $park
        Write-Host "  [park   ] ChroniconSkillMeter.dll -> $park"
    }
    Copy-Item -LiteralPath $mod -Destination (Join-Path $Aurie 'ChroniconDPS.dll') -Force
    Write-Host '  [install] aurie\ChroniconDPS.dll (known-good control mod)'
} else {
    $control = Join-Path $Aurie 'ChroniconDPS.dll'
    if (Test-Path -LiteralPath $control) {
        $park = Join-Path $Backup "control-switched-$(Get-Date -Format yyyyMMdd-HHmmss)-ChroniconDPS.dll"
        Move-Item -LiteralPath $control -Destination $park
        Write-Host "  [park   ] ChroniconDPS.dll -> $park"
    }
    Copy-Item -LiteralPath $mod -Destination (Join-Path $Aurie 'ChroniconSkillMeter.dll') -Force
    Write-Host '  [install] aurie\ChroniconSkillMeter.dll'
}

Show-Result
if ($UseControl) {
    Write-Host "`nCONTROL mode. Start the game: if the original DPS meter works (F1/F2), the framework is fine."
    Write-Host 'Then re-run without -UseControl to test ChroniconSkillMeter.'
} else {
    Write-Host "`nDone. Start the game from Steam; press F5 to toggle the meter."
}
Write-Host "Backup / rollback: $Backup   (or: .\install_runtime.ps1 -Rollback)" -ForegroundColor DarkGray
