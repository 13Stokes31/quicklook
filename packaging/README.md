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
