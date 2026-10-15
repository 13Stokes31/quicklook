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

## Publication status

The GitHub release `v0.2.0` and `quicklook-bin` on AUR are public. Both recipes
have been built and verified, including the 41 Rust tests. The source package
name `quicklook` belonged to a retired statistics tool. AUR restored that entry
as an orphan and filed the adoption request, but requires a Package Maintainer
to approve it before allowing a push. Do not advertise the current AUR
`quicklook` entry as this image viewer until the new recipe is published.

The prepared source recipe is in `packaging/aur/quicklook/`. On the release
workstation its publication clone is `target/aur-publish/quicklook`, with the
recipe already committed. Once AUR grants maintainership, publish it from that
clone:

```sh
git -c core.sshCommand='ssh -F /dev/null -o BatchMode=yes -o StrictHostKeyChecking=yes' push origin master
```

Then verify AUR reports version `0.2.0-1`, maintainer `Stokes`, and the GitHub
URL for this project before updating the installation instructions. Published
release archives and their checksums remain unchanged while adoption is pending.
