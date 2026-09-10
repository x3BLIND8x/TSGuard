# TSGuard

TSGuard is a TeamSpeak 3 client plugin for Windows x64 and Linux x64.

## Features

- **AntiMove / AntiKick** — returns you to the previous channel after another client moves or channel-kicks you.
- **Anti Server Kick** — reconnects after a server kick and returns to the last channel when possible.
- **Anti Server Temporary Ban** — reconnects after the temporary ban expires, with a one-second buffer.
- **Poke/Message Back** — sends incoming private messages and pokes back to the sender.
- **Auto Away** — sets Away after five minutes of TeamSpeak inactivity and clears it when activity resumes.
- **Anti Group Removal** — attempts to restore removed server/channel groups when your permissions allow it.
- **AutoFollow** — follows another client between channels.
- **Return to last channel** — available from the plugin menu, command, and hotkey.
- **Event logging** — logs move, kick, ban, and group events.

## Install

Download `TSGuard.ts3_plugin` from the latest GitHub Release and open it with the TeamSpeak package installer.

Platform-specific installers are also published:

- Windows: `TSGuard-windows-amd64.zip`
- Linux: `TSGuard-linux-amd64.tar.gz`

### Windows

The manual installer copies `tsguard_win64.dll` to:

```text
%APPDATA%\TS3Client\plugins\
```

Extract `TSGuard-windows-amd64.zip`, then run `install.ps1` with PowerShell.

### Linux

The manual installer copies `tsguard.so` to:

```text
~/.ts3client/plugins/
```

Extract `TSGuard-linux-amd64.tar.gz`, then run:

```bash
./install.sh
```

Restart TeamSpeak after installing or updating the plugin.

## Commands

Command prefix: `/tsg`

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

`/tsg antikick` is an alias for `/tsg antimove`.

## Defaults

| Feature | Default |
| --- | --- |
| AntiMove / AntiKick | On |
| Anti Server Kick | Off |
| Anti Server Temporary Ban | Off |
| Poke/Message Back | Off |
| Auto Away | Off |
| Anti Group Removal | Off |
| Event logging | On |
| AutoFollow | Off |

Settings are currently kept for the active TeamSpeak session and reset when the plugin/client restarts.

## Build

TSGuard uses the TeamSpeak 3 Plugin SDK API 26 and CMake.

### Arch Linux

```bash
sudo pacman -S --needed base-devel git cmake
./scripts/bootstrap-sdk.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./scripts/install.sh build/out/tsguard.so
```

The convenience Makefile also works:

```bash
make sdk
make
make install
```

### Windows x64

Install Git, CMake, and Visual Studio 2022 Build Tools with the Desktop development with C++ workload. Then run:

```powershell
./scripts/bootstrap-sdk.ps1
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
./scripts/install-windows.ps1 -PluginPath ./build/out/tsguard_win64.dll
```

## Releases

GitHub Actions builds both platforms on pushes and pull requests. Pushing a version tag such as `v1.0` creates a GitHub Release with:

```text
TSGuard.ts3_plugin
TSGuard-windows-amd64.zip
TSGuard-linux-amd64.tar.gz
SHA256SUMS.txt
```

See [PUBLISHING.md](PUBLISHING.md) for the release workflow.

## License

See [LICENSE](LICENSE).
