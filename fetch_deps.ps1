# Fetches the YYToolkit / Aurie headers required to build ChroniconSkillMeter.
# The layout mirrors atty303/chronicon-dps, which is a verified working combination.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\fetch_deps.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\fetch_deps.ps1 -Mirror "https://ghproxy.net/"
#
# If GitHub is unreachable, manually copy the "ChroniconDPS/include" folder from
# https://github.com/atty303/chronicon-dps into .\extern\ so that you end up with
# extern\YYToolkit\Shared.hpp, extern\YYToolkit\Shared.cpp, extern\Aurie\shared.hpp

param(
    [string]$Mirror = ""
)

$ErrorActionPreference = "Stop"
# Windows PowerShell 5.1 defaults to TLS 1.0/1.1, which GitHub rejects with 403.
try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch { }
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Base = "https://raw.githubusercontent.com/atty303/chronicon-dps/main/ChroniconDPS/include/"

if ($Mirror -ne "") {
    if (-not $Mirror.EndsWith("/")) { $Mirror += "/" }
    $Base = $Mirror + $Base
}

$Files = @(
    @{ Url = "YYToolkit/Shared.hpp"; Out = "YYToolkit/Shared.hpp" },
    @{ Url = "YYToolkit/Shared.cpp"; Out = "YYToolkit/Shared.cpp" },
    @{ Url = "Aurie/shared.hpp";     Out = "Aurie/shared.hpp" },
    @{ Url = "FunctionWrapper/FunctionWrapper.hpp"; Out = "FunctionWrapper/FunctionWrapper.hpp" }
)

foreach ($f in $Files) {
    $dest = Join-Path $Root (Join-Path "extern" $f.Out)
    $dir = Split-Path -Parent $dest
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    if (Test-Path $dest) {
        Write-Host "skip  $($f.Out) (already present)"
        continue
    }
    $url = $Base + $f.Url
    Write-Host "fetch $($f.Out)"
    try {
        Invoke-WebRequest -Uri $url -OutFile $dest -UseBasicParsing -TimeoutSec 30
        Write-Host "      ok ($((Get-Item $dest).Length) bytes)"
    }
    catch {
        Write-Warning "raw download failed: $($_.Exception.Message)"
        if ((Test-Path $dest) -and ((Get-Item $dest).Length -eq 0)) {
            Remove-Item $dest -Force
        }
        # raw.githubusercontent.com is sometimes blocked / fails TLS while the
        # GitHub contents API still works. Fall back to a base64 API response.
        # curl.exe uses a different network/TLS stack than .NET WebRequest and
        # succeeds on some machines where Invoke-RestMethod returns HTTP 403.
        $api = "https://api.github.com/repos/atty303/chronicon-dps/contents/ChroniconDPS/include/$($f.Url)?ref=main"
        $done = $false
        try {
            $tmp = [IO.Path]::GetTempFileName()
            & curl.exe -sS -L --max-time 30 -H "User-Agent: ChroniconSkillMeter-build" -o $tmp $api 2>$null
            if ($LASTEXITCODE -eq 0 -and (Test-Path $tmp) -and ((Get-Item $tmp).Length -gt 0)) {
                $json = Get-Content -Raw $tmp | ConvertFrom-Json
                [IO.File]::WriteAllBytes($dest, [Convert]::FromBase64String(($json.content -replace "\s", "")))
                $done = $true
                Write-Host "      ok via curl + GitHub API ($((Get-Item $dest).Length) bytes)"
            }
            Remove-Item $tmp -Force -ErrorAction SilentlyContinue
        }
        catch { }
        if (-not $done) {
            try {
                $json = Invoke-RestMethod -Uri $api -Headers @{ "User-Agent" = "ChroniconSkillMeter-build" } -TimeoutSec 30
                [IO.File]::WriteAllBytes($dest, [Convert]::FromBase64String(($json.content -replace "\s", "")))
                $done = $true
                Write-Host "      ok via GitHub API ($((Get-Item $dest).Length) bytes)"
            }
            catch {
                Write-Warning "GitHub API fallback failed too: $($_.Exception.Message)"
            }
        }
        if (-not $done) {
            Write-Warning "Retry with -Mirror, or download manually from $url"
            if ((Test-Path $dest) -and ((Get-Item $dest).Length -eq 0)) {
                Remove-Item $dest -Force
            }
        }
    }
}

Write-Host ""
$incomplete = $false
foreach ($f in $Files) {
    $dest = Join-Path $Root (Join-Path "extern" $f.Out)
    if (-not (Test-Path $dest)) {
        Write-Warning "MISSING: $($f.Out)"
        $incomplete = $true
    }
}
if ($incomplete) {
    Write-Warning "Some headers are missing. The build will fail until they are present."
    exit 1
}
Write-Host "All headers present. Run build.bat (or open ChroniconSkillMeter.sln)."
