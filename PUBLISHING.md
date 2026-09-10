# Publishing TSGuard 1.0 on GitHub

This tree is already set up so GitHub Actions builds both Windows x64 and Linux x64 and creates installable release files automatically.

## 1. Create the GitHub repository

Create an empty public repository named `TSGuard` on GitHub. Do **not** add a README, license, or `.gitignore` from the website because this source tree already contains them.

If you use GitHub CLI on Arch, the easiest route is:

```fish
sudo pacman -S --needed github-cli git
gh auth login
```

Then, from this project directory:

```fish
git init
git add .
git commit -m "Release TSGuard 1.0"
git branch -M main
gh repo create TSGuard --public --source=. --remote=origin --push
```

If you prefer creating the repository in the browser, replace `YOUR_GITHUB_USERNAME` below and run:

```fish
git init
git add .
git commit -m "Release TSGuard 1.0"
git branch -M main
git remote add origin https://github.com/YOUR_GITHUB_USERNAME/TSGuard.git
git push -u origin main
```

## 2. Check CI before releasing

Open the repository's **Actions** tab. The `CI` workflow should run two jobs:

```text
Linux x64
Windows x64
```

Do not publish the release until both are green. This is especially important because the Windows build is produced by GitHub's Windows runner, not by your Arch machine.

## 3. Publish v1.0

Once CI passes:

```fish
git tag -a v1.0 -m "TSGuard 1.0"
git push origin v1.0
```

That tag automatically starts `.github/workflows/release.yml`.

The release workflow will:

1. Build `tsguard.so` on Linux.
2. Build `tsguard_win64.dll` on Windows x64.
3. Create the universal `TSGuard-v1.0.ts3_plugin` package.
4. Create Windows/Linux fallback installer archives.
5. Generate `SHA256SUMS.txt`.
6. Create the GitHub Release and upload everything.

No personal access token or release secret is required for this workflow: it uses the repository-scoped GitHub Actions token with `contents: write` permission.

## 4. What users download

### Recommended: universal installer

```text
TSGuard-v1.0.ts3_plugin
```

Windows users can normally double-click it. Linux users can pass it to TeamSpeak's `package_inst` executable.

### Fallbacks

```text
TSGuard-v1.0-windows-amd64.zip
TSGuard-v1.0-linux-amd64.tar.gz
```

The Windows archive contains `install.ps1`. The Linux archive contains `install.sh`.

## 5. Future releases

For the next version, change **one authoritative version file**:

```text
VERSION
```

For example, set it to `1.1`, then update `CHANGELOG.md`/README release notes as appropriate. CMake, the plugin binary metadata, package metadata, and the release workflow all derive the version from `VERSION`.

Then commit, push, wait for CI, and tag it. Example for 1.1:

```fish
git add .
git commit -m "Release TSGuard 1.1"
git push
git tag -a v1.1 -m "TSGuard 1.1"
git push origin v1.1
```

## 6. Recommended GitHub repository settings

After publishing, go to **Settings -> Actions -> General** and keep Actions enabled. The release workflow explicitly requests only `contents: write` so it can create/upload GitHub Releases.

Also add a short repository description such as:

```text
TeamSpeak 3 QoL/guard plugin for Windows and Linux — AntiMove/AntiKick, reconnect helpers, Auto Away, AutoFollow and more.
```

Suggested topics:

```text
teamspeak teamspeak3 ts3 plugin linux windows c cmake
```
