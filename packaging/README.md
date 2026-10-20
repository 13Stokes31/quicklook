# Releases and AUR

`packaging/aur/quicklook` builds the tagged version from the source release;
`packaging/aur/quicklook-bin` installs the upstream x86_64 binary archive.
Both use SHA-256 checksums and opt-in user integration. `.SRCINFO` is generated
with `makepkg --printsrcinfo` and committed alongside each `PKGBUILD` in AUR.

Build with generic x86-64 compiler settings; do not use `target-cpu=native` or
`-march=native` for the binary release. Native soft blur is built against KWin
6.7.5; its KWin factory embeds that exact plugin interface version. Neither
viewer package pins the installed KWin version. Source builds skip the native
module on other KWin versions and retain native blur/opaque fallback.

After staging the release sources with `git add`, run:

```sh
make build-blur
python3 packaging/release.py
```

Build the native module in a KWin 6.7.5 environment. On newer KWin versions,
run `make build` and reuse the unchanged, verified 6.7.5 module from the previous
official release for the binary archive; the viewer uses native blur/opaque fallback.

The script writes deterministic archives under `target/releases/`, without
modifying the installed application. Source archives omit AUR recipes to avoid
self-referential checksums. Everything else in the Git index is included.
Update the two PKGBUILD checksums, generate `.SRCINFO`, and validate both recipes
with makepkg before publishing. Commit, push and tag the corresponding version;
attach the archives to the GitHub release. Confirm that the downloads work
without authentication before pushing the recipes to AUR.

Do not enable KWin plugins or change Dolphin shortcuts from pacman install hooks.
The optional configurator can find a system menu and stores user backups under
XDG_STATE_HOME; it runs only when explicitly invoked by the user.

## Packaging document previews

Version `0.3.0` adds text and PDF previews. Version `0.4.0` adds Office documents and direct folder contents. Historical
release archives remain unchanged. A release with PDF needs `qt6-webengine`
as a source build dependency and an optional runtime dependency of both packages.
The PDF module is `usr/lib/quicklook/quicklook-pdf.so`; `make install-files`
stages it through CMake when enabled. The viewer loads it on demand and keeps
images/text usable if it cannot load. A build configured without PDF does not
stage a stale module left by a previous build in the same directory.

Office previews use the optional `libreoffice` virtual package together with
the PDF module. Qt Concurrent belongs to the existing `qt6-base` dependency;
folder previews need neither optional dependency.

Validation commands are in README.md. The source recipe runs both the Rust and
Qt document tests. Package versions, checksums and `.SRCINFO` must refer to the
same release.

## Publication status

The project distributes GitHub releases and the `quicklook-bin` AUR package.
Verify the release downloads work without authentication and that AUR reports
the intended version, maintainer `Stokes` and this project's GitHub URL after
each publication.

The source recipe is in `packaging/aur/quicklook/`. Its package name belonged to
a retired statistics tool. AUR rejected adoption request PRQ#88354 and deleted
that old entry on 19 October 2026. Deletion does not itself authorize restoring
the entry for a different project. Reuse of that name remains a pending matter
for AUR staff; do not restore the retired entry or advertise `quicklook` as
installable until it is resolved. The outstanding decision is tracked in
ROADMAP.md.
