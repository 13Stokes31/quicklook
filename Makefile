PREFIX ?= /usr/local
DESTDIR ?=
CARGO ?= cargo
CARGO_FLAGS ?=
PYTHON ?= python3
JOBS ?= 2

.PHONY: build build-blur build-softblur install install-files install-softblur uninstall install-centering uninstall-centering install-shortcut

build:
	$(CARGO) build --release --locked $(CARGO_FLAGS)
	cmake -S qt -B target/qt-build -DCMAKE_BUILD_TYPE=Release
	cmake --build target/qt-build -j$(JOBS)

build-softblur:
	cmake -S integration/kwin-softblur -B target/softblur-build -DCMAKE_BUILD_TYPE=Release
	cmake --build target/softblur-build -j$(JOBS)

build-blur: build build-softblur

install: build install-files

install-files: install-centering
	$(PYTHON) integration/prepare-menu.py "$(PREFIX)/bin/quicklook" > target/quicklook.desktop
	install -Dm755 target/qt-build/quicklook-qt "$(DESTDIR)$(PREFIX)/bin/quicklook"
	install -Dm755 target/release/quicklook "$(DESTDIR)$(PREFIX)/bin/quicklook-opaque"
	install -Dm755 target/quicklook.desktop "$(DESTDIR)$(PREFIX)/share/kio/servicemenus/quicklook.desktop"
	install -Dm755 integration/configure-shortcut.py "$(DESTDIR)$(PREFIX)/bin/quicklook-dolphin-shortcut"
	install -Dm644 README.md "$(DESTDIR)$(PREFIX)/share/doc/quicklook/README.md"
	install -Dm644 LICENSE "$(DESTDIR)$(PREFIX)/share/licenses/quicklook/LICENSE"
	install -Dm644 LICENSES/GPL-2.0-or-later.txt "$(DESTDIR)$(PREFIX)/share/licenses/quicklook/GPL-2.0-or-later.txt"

install-softblur:
	install -Dm755 target/softblur-build/plugins/kwin/effects/plugins/quicklook_softblur_v2.so "$(DESTDIR)$(PREFIX)/lib/qt6/plugins/kwin/effects/plugins/quicklook_softblur_v2.so"

uninstall:
	rm -f -- "$(DESTDIR)$(PREFIX)/share/kio/servicemenus/quicklook.desktop" "$(DESTDIR)$(PREFIX)/bin/quicklook" "$(DESTDIR)$(PREFIX)/bin/quicklook-opaque" "$(DESTDIR)$(PREFIX)/bin/quicklook-dolphin-shortcut"

install-centering:
	install -Dm644 integration/kwin/metadata.json "$(DESTDIR)$(PREFIX)/share/kwin/scripts/quicklook-center/metadata.json"
	install -Dm644 integration/kwin/contents/code/main.js "$(DESTDIR)$(PREFIX)/share/kwin/scripts/quicklook-center/contents/code/main.js"

install-shortcut:
	$(PYTHON) integration/configure-shortcut.py

uninstall-centering:
	rm -f -- "$(DESTDIR)$(PREFIX)/share/kwin/scripts/quicklook-center/metadata.json" "$(DESTDIR)$(PREFIX)/share/kwin/scripts/quicklook-center/contents/code/main.js"
