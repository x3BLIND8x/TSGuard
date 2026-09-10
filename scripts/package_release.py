#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import shutil
import tarfile
import tempfile
import zipfile
from pathlib import Path


def write_text(path: Path, text: str) -> None:
    path.write_text(text.replace("\r\n", "\n"), encoding="utf-8", newline="\n")


def zip_tree(src: Path, dst: Path) -> None:
    with zipfile.ZipFile(dst, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for p in sorted(src.rglob("*")):
            if p.is_file():
                zf.write(p, p.relative_to(src).as_posix())


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def main() -> None:
    ap = argparse.ArgumentParser(description="Build TSGuard release packages")
    ap.add_argument("--version", required=True)
    ap.add_argument("--linux", required=True, type=Path)
    ap.add_argument("--windows", required=True, type=Path)
    ap.add_argument("--out", default=Path("dist"), type=Path)
    args = ap.parse_args()

    root = Path(__file__).resolve().parents[1]
    source_version = (root / "VERSION").read_text(encoding="utf-8").strip()
    if args.version != source_version:
        raise SystemExit(f"Source VERSION is {source_version!r}; got release version {args.version!r}")
    if not args.linux.is_file() or not args.windows.is_file():
        raise SystemExit("Both Linux and Windows plugin binaries are required")

    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    for old in out.iterdir():
        if old.is_file():
            old.unlink()

    package_ini = (root / "packaging" / "package.ini.in").read_text(encoding="utf-8").replace("@VERSION@", args.version)

    with tempfile.TemporaryDirectory() as td:
        td = Path(td)

        # Universal TeamSpeak Package Installer bundle.
        universal = td / "universal"
        plugins = universal / "plugins"
        plugins.mkdir(parents=True)
        write_text(universal / "package.ini", package_ini)
        shutil.copy2(args.linux, plugins / "tsguard.so")
        shutil.copy2(args.windows, plugins / "tsguard_win64.dll")
        ts3_pkg = out / f"TSGuard-v{args.version}.ts3_plugin"
        zip_tree(universal, ts3_pkg)

        # Linux manual installer archive.
        linux_dir = td / f"TSGuard-v{args.version}-linux-amd64"
        linux_dir.mkdir()
        shutil.copy2(args.linux, linux_dir / "tsguard.so")
        install_sh = linux_dir / "install.sh"
        write_text(install_sh, """#!/usr/bin/env bash
set -euo pipefail
DEST="${TS3_PLUGIN_DIR:-$HOME/.ts3client/plugins}"
mkdir -p "$DEST"
install -m 0755 "$(dirname "$0")/tsguard.so" "$DEST/tsguard.so"
rm -f "$DEST/blondguard.so"
echo "Installed TSGuard to $DEST/tsguard.so"
echo "Restart TeamSpeak 3."
""")
        install_sh.chmod(0o755)
        write_text(linux_dir / "README.txt", "Run ./install.sh, then restart TeamSpeak 3.\n")
        linux_tar = out / f"TSGuard-v{args.version}-linux-amd64.tar.gz"
        with tarfile.open(linux_tar, "w:gz") as tf:
            tf.add(linux_dir, arcname=linux_dir.name)

        # Windows manual installer archive.
        win_dir = td / f"TSGuard-v{args.version}-windows-amd64"
        win_dir.mkdir()
        shutil.copy2(args.windows, win_dir / "tsguard_win64.dll")
        write_text(win_dir / "install.ps1", r'''$ErrorActionPreference = 'Stop'
$dest = Join-Path $env:APPDATA 'TS3Client\plugins'
New-Item -ItemType Directory -Force -Path $dest | Out-Null
Copy-Item -Force (Join-Path $PSScriptRoot 'tsguard_win64.dll') (Join-Path $dest 'tsguard_win64.dll')
@('tsguard.dll','blondguard.dll','blondguard_win64.dll') | ForEach-Object {
    $old = Join-Path $dest $_
    if (Test-Path $old) { Remove-Item -Force $old }
}
Write-Host "Installed TSGuard to $dest\tsguard_win64.dll"
Write-Host 'Restart TeamSpeak 3.'
''')
        write_text(win_dir / "README.txt", "Right-click install.ps1 -> Run with PowerShell, then restart TeamSpeak 3.\n")
        win_zip = out / f"TSGuard-v{args.version}-windows-amd64.zip"
        zip_tree(win_dir, win_zip)

    artifacts = sorted(p for p in out.iterdir() if p.is_file())
    sums = out / "SHA256SUMS.txt"
    write_text(sums, "".join(f"{sha256(p)}  {p.name}\n" for p in artifacts))
    print("Created:")
    for p in sorted(out.iterdir()):
        print(f"  {p}")


if __name__ == "__main__":
    main()
