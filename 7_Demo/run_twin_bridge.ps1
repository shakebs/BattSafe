param(
    [string]$TwinHost = "127.0.0.1",
    [int]$TwinPort = 5001,
    [int]$DashPort = 5000,
    [string]$BoardPort = "",
    [switch]$Inline
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path "7_Demo\\digital_twin\\main.py")) {
    throw "Run this script from repository root (BattSafe)."
}

$repo = (Get-Location).Path
$twinCmd = "Set-Location '$repo\\7_Demo'; python -m digital_twin.main --no-serial"

$dashCmd = "Set-Location '$repo'; python 7_Demo\\dashboard\\src\\server.py --twin-bridge --host 127.0.0.1 --web-port $DashPort --twin-url http://$TwinHost`:$TwinPort"
if ($BoardPort) {
    $dashCmd += " --port $BoardPort"
}

if ($Inline) {
    Write-Host "Start Digital Twin in another terminal:"
    Write-Host "  $twinCmd"
    Write-Host ""
    Write-Host "Then run dashboard bridge:"
    Write-Host "  $dashCmd"
    exit 0
}

Start-Process powershell -ArgumentList "-NoExit", "-Command", $twinCmd | Out-Null
Start-Sleep -Seconds 2
Start-Process powershell -ArgumentList "-NoExit", "-Command", $dashCmd | Out-Null

Write-Host "Launched:"
Write-Host "  Twin input dashboard:  http://$TwinHost`:$TwinPort"
Write-Host "  Output dashboard:      http://127.0.0.1:$DashPort"
Write-Host ""
Write-Host "If board is not auto-detected, rerun with: -BoardPort COMx"
