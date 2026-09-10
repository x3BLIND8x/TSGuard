# Publishing TSGuard

## First push

Configure Git once if you have not already:

```bash
git config --global user.name "YOUR_NAME"
git config --global user.email "YOUR_GITHUB_NOREPLY_EMAIL"
git config --global init.defaultBranch main
```

Create the repository from the project directory:

```bash
git init
git add .
git commit -m "Initial release"
git branch -M main
gh repo create TSGuard --public --source=. --remote=origin --push
```

Open the repository's **Actions** tab and confirm the Windows and Linux jobs pass.

## Create a release

The version used by the plugin is stored in `VERSION`.

Commit any final changes, then tag the commit:

```bash
git add .
git commit -m "Prepare release"
git push

git tag -a v1.0 -m "v1.0"
git push origin v1.0
```

The release workflow builds both binaries and publishes:

```text
TSGuard.ts3_plugin
TSGuard-windows-amd64.zip
TSGuard-linux-amd64.tar.gz
SHA256SUMS.txt
```

## Future releases

Update `VERSION`, commit the change, and push a matching tag. Example:

```bash
printf '1.1\n' > VERSION
git add VERSION CHANGELOG.md
git commit -m "Prepare release"
git push

git tag -a v1.1 -m "v1.1"
git push origin v1.1
```
