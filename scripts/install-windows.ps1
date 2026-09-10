param(
    [Parameter(Mandatory=$false)]
    [string]$PluginPath = "$PSScriptRoot\..\build\out\tsguard_win64.dll"
)

$ErrorActionPreference = 'Stop'
$dest = Join-Path $env:APPDATA 'TS3Client\plugins'
$target = Join-Path $dest 'tsguard_win64.dll'
$legacy = @(
    (Join-Path $dest 'tsguard.dll'),
    (Join-Path $dest 'blondguard.dll'),
    (Join-Path $dest 'blondguard_win64.dll')
)

if (-not (Test-Path $PluginPath)) {
    throw "Plugin binary not found: $PluginPath"
}

New-Item -ItemType Directory -Force -Path $dest | Out-Null
Copy-Item -Force $PluginPath $target
foreach ($old in $legacy) {
    if (Test-Path $old) { Remove-Item -Force $old }
}

Write-Host "Installed TSGuard to: $target"
Write-Host 'Restart TeamSpeak 3, then enable TSGuard in Plugins/Addons if needed.'
