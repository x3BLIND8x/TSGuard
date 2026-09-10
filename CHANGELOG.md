# Changelog

## 1.0

- Promoted TSGuard to its first stable release.
- Added Windows x64 support while keeping Linux x64 support.
- Replaced Linux-only worker/thread primitives with a small Windows/Linux portability layer.
- Added CMake as the cross-platform build system while keeping the Linux convenience Makefile.
- Added automated GitHub Actions CI builds for Windows and Linux.
- Added automated tagged GitHub Releases with a universal `.ts3_plugin`, platform-specific fallback installers, and SHA-256 checksums.
- Kept the explicit white `[TSGuard]` chat prefix and compact one-row-per-feature menu status display.

## 0.2.2

- Renamed the entire project and plugin to **TSGuard** across plugin metadata, menus, logs, hotkeys, source/build names, docs, and installed binary.
- Changed the chat announcement prefix to explicit white: `[TSGuard]` now renders white instead of inheriting TeamSpeak's blue formatting.

## 0.2.1

- Replaced duplicated ON/OFF status rows with one status row per feature.
- Bright/enabled status row means ON; grey/disabled means OFF.

## 0.2.0

- Combined AntiMove and channel AntiKick into one toggle.
- Added Anti Server Kick reconnect support.
- Added temporary-ban expiry reconnect support (`ban duration + 1 second`).
- Added Poke/Message Back with loop protection.
- Added Auto Away after five minutes of inactivity.
- Added permission-checked server/channel group restoration.
- Removed duplicated `TSGuard:` prefix from submenu labels.
