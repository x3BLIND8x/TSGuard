# TSGuard

TSGuard is a native **TeamSpeak 3** client plugin for **Windows x64 and Linux x64**, targeting **Plugin API 26** (TeamSpeak 3.6.0+).

## Features

- **AntiMove / AntiKick** — one combined switch. If another client/admin moves you or kicks you from a channel, TSGuard requests a move back to the previous channel.
- **Anti Server Kick** — optional reconnect about 1 second after a server kick and requests the saved last channel again.
- **Anti Server Temporary Ban** — optional reconnect after the ban has actually expired: `ban duration + 1 second`. A permanent/indefinite ban is never retried.
- **Poke/Message Back** — optional exact echo of incoming private text messages and poke text to the sender. A loop guard prevents two echo clients from endlessly bouncing the same text.
- **Auto Away** — optional Away status after 5 minutes of TeamSpeak inactivity; automatically clears the Away state when activity returns if TSGuard set it.
- **Anti Server/Channel Group Removal** — if another client removes one of your server groups, or resets your current channel group to the server default, TSGuard requests restoration of the previous group. The normal TeamSpeak permission system still decides whether the request succeeds.
- **AutoFollow** — follow another client's channel changes by client ID or client context menu.
- **Return to previous channel** — command/menu/hotkey.
- **Move/kick/group logging**.
- **Clean chat status output** — the `[TSGuard]` prefix is explicitly white; status output stays green, with ON values green and OFF values red.
- **Compact plugin-menu status section** — one status row per feature. Bright/enabled means ON; grey/disabled means OFF.
- **Safety limiters** — automated move/group actions are limited to 3 per 5 seconds; server reconnects are limited to 3 per 30 seconds.

## Install a release

The GitHub release workflow produces a universal TeamSpeak package:

```text
TSGuard-v1.0.ts3_plugin
```

### Windows

Download `TSGuard-v1.0.ts3_plugin` from Releases and double-click it. TeamSpeak's package installer should open and install the plugin. Restart TeamSpeak 3 afterward.

If the package association is broken, use the fallback archive `TSGuard-v1.0-windows-amd64.zip`, extract it, then right-click `install.ps1` and run it with PowerShell. It installs to:

```text
%APPDATA%\TS3Client\plugins\tsguard_win64.dll
```

### Linux

Download `TSGuard-v1.0.ts3_plugin` and open it with TeamSpeak's `package_inst` executable. A manual fallback archive is also generated:

```text
TSGuard-v1.0-linux-amd64.tar.gz
```

Extract it and run:

```bash
./install.sh
```

The fallback installer copies the plugin to:

```text
~/.ts3client/plugins/tsguard.so
```

Restart TeamSpeak 3 after installation.

## Build from source

### Arch Linux

```bash
sudo pacman -S --needed base-devel git cmake
./scripts/bootstrap-sdk.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./scripts/install.sh build/out/tsguard.so
```

You can still use the convenience Makefile:

```bash
make sdk
make
make install
```

### Windows x64

Install **Git**, **CMake**, and **Visual Studio 2022 Build Tools** with the Desktop development with C++ workload. Then from PowerShell:

```powershell
./scripts/bootstrap-sdk.ps1
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
./scripts/install-windows.ps1 -PluginPath ./build/out/tsguard_win64.dll
```

## Commands

Plugin command prefix: `/tsg`

```text
/tsg status
/tsg antimove on|off|toggle
/tsg serverkick on|off|toggle
/tsg tempban on|off|toggle
/tsg echo on|off|toggle
/tsg autoaway on|off|toggle
/tsg groups on|off|toggle
/tsg logging on|off|toggle
/tsg back [channelPassword]
/tsg follow <clientID>
/tsg follow off
/tsg help
```

`/tsg antikick` remains as a compatibility alias for `/tsg antimove`.

## Menus

Client context menu:

```text
Follow/unfollow client
```

Plugins -> TSGuard:

```text
Toggle AntiMove / AntiKick
Toggle Anti Server Kick
Toggle Anti Server Temporary Ban
Toggle Poke/Message Back
Toggle Auto Away (5 min)
Toggle Anti Group Removal
Return to last channel
--- Current status ---
● AntiMove / AntiKick
● Anti Server Kick
● Anti Server Temporary Ban
● Poke/Message Back
● Auto Away
● Anti Group Removal
```

In the status section, an enabled/bright row means ON and a disabled/grey row means OFF. TeamSpeak Plugin API 26 does not expose dynamic menu-row hiding/renaming.

## Defaults

Per server tab/session:

```text
AntiMove / AntiKick:               ON
Anti Server Kick:                  OFF
Anti Server Temporary Ban:         OFF
Poke/Message Back:                 OFF
Auto Away:                         OFF
Anti Server/Channel Group Removal: OFF
Logging:                           ON
AutoFollow:                        OFF
```

Settings are currently in-memory and reset when TeamSpeak/plugin restarts.

## Hotkeys

TSGuard registers hotkey actions for:

- Toggle AntiMove / AntiKick
- Toggle Anti Server Kick
- Toggle Anti Server Temporary Ban
- Toggle Poke/Message Back
- Toggle Auto Away
- Toggle Anti Group Removal
- Return to last channel
- Stop AutoFollow

## Reconnect details / limitations

TSGuard snapshots the TeamSpeak-provided server host/port/password, nickname, current channel path, and channel password while connected. Delayed reconnect uses TeamSpeak's `guiConnect` helper and asks to reconnect to that channel.

- A temporary ban is **not bypassed**. TSGuard waits for the server-reported ban duration to end and adds 1 second before reconnecting.
- Permanent/indefinite bans are not auto-retried.
- If the original connection used a non-default identity/profile, TeamSpeak's plugin API does not expose the currently selected identity/profile cleanly. The reconnect helper therefore relies on the client's default/current GUI settings and may require a manual reconnect for unusual setups.
- Server/channel passwords are only available to the plugin if TeamSpeak has them available for that connection/channel.
- Group restoration only submits a normal permission-checked TeamSpeak request. It does not grant permissions or bypass the server permission system.
- Repeated admin actions can still win; safety limiters intentionally stop infinite move/reconnect/group wars.

## Automated builds and releases

This repository includes two GitHub Actions workflows:

- `.github/workflows/ci.yml` — compiles TSGuard on Windows x64 and Linux x64 for every push/PR.
- `.github/workflows/release.yml` — when you push a version tag such as `v1.0`, it builds both platforms, packages them, calculates SHA-256 checksums, creates a GitHub Release, and uploads:

```text
TSGuard-v1.0.ts3_plugin
TSGuard-v1.0-windows-amd64.zip
TSGuard-v1.0-linux-amd64.tar.gz
SHA256SUMS.txt
```

See [`PUBLISHING.md`](PUBLISHING.md) for the exact first-release commands.

## SDK

`scripts/bootstrap-sdk.sh` and `scripts/bootstrap-sdk.ps1` fetch the official `teamspeak/ts3client-pluginsdk` tag **26**.

## License

See [`LICENSE`](LICENSE).
