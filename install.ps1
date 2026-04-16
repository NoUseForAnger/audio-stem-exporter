# Audio Stem Exporter for OBS — installer
# Right-click -> "Run with PowerShell"

if (-NOT ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Start-Process powershell -Verb RunAs -ArgumentList "-ExecutionPolicy Bypass -File `"$PSCommandPath`""
    exit
}

$root = Split-Path -Parent $PSCommandPath
$dll  = Join-Path $root "build_x64\RelWithDebInfo\obs-mp3-writer.dll"
$dest = "$env:ProgramFiles\obs-studio\obs-plugins\64bit"
$data = "$env:ProgramFiles\obs-studio\data\obs-plugins\obs-mp3-writer\locale"
$ini  = Join-Path $root "data\locale\en-US.ini"

Write-Host "Installing Audio Stem Exporter for OBS..." -ForegroundColor Cyan
Write-Host "DLL: $((Get-Item $dll).Length) bytes"

New-Item -ItemType Directory -Force -Path $data | Out-Null
Copy-Item -Force $dll "$dest\obs-mp3-writer.dll"
Copy-Item -Force $ini "$data\en-US.ini"

Write-Host "Installed successfully." -ForegroundColor Green
Write-Host "Restart OBS to load the updated plugin." -ForegroundColor Green
Read-Host "Press Enter to close"
