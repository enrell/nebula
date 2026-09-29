# Publishing to the AUR

Two packages live here: `nebula-bin` (prebuilt tarball from the GitHub release, uses the system Qt) and `nebula-git`
(builds the latest commit). The AUR is a git host: each package is a repo containing `PKGBUILD` and `.SRCINFO`.

## One-time setup
1. Register at <https://aur.archlinux.org/register> (this is separate from the Arch mailing lists / forum accounts).
2. Add your SSH **public** key under *My Account* on the AUR.

## Publish a package
```sh
cd packaging/aur/nebula-bin                       # or nebula-git
git clone ssh://aur@aur.archlinux.org/nebula-bin.git /tmp/aur-nebula-bin   # empty repo the first time
cp PKGBUILD .SRCINFO /tmp/aur-nebula-bin/
cd /tmp/aur-nebula-bin
git add PKGBUILD .SRCINFO && git commit -m "nebula-bin 0.1.0" && git push origin master
```
The AUR branch is `master`. Repeat with `nebula-git`.

## Release a new version
1. Tag and push: `git tag -a vX.Y.Z -m "nebula X.Y.Z" && git push origin vX.Y.Z` (CI builds and publishes the tarball, the AppImage and `SHA256SUMS`).
2. In `nebula-bin/PKGBUILD` set `pkgver=X.Y.Z`, `pkgrel=1` and the tarball's `sha256sums` (`gh release download vX.Y.Z -p SHA256SUMS -O -`).
3. `makepkg --printsrcinfo > .SRCINFO`, test with `makepkg -f`, then push to the AUR as above.

`nebula-git` needs no edits per release (it computes its version from `git describe`).
