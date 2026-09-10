$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Dst = Join-Path $Root 'third_party\ts3client-pluginsdk'
$Header = Join-Path $Dst 'include\ts3_functions.h'

if (Test-Path $Header) {
    Write-Host "TeamSpeak Plugin SDK already present: $Dst"
    exit 0
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Dst) | Out-Null
git clone --depth 1 --branch 26 https://github.com/teamspeak/ts3client-pluginsdk.git $Dst
Write-Host 'Fetched official TeamSpeak 3 Plugin SDK tag 26.'
