#!/usr/bin/env python3
"""Prueba el lanzamiento nativo de KDE con un PATH sin ~/.local/bin.

Requiere c++, pkg-config y las cabeceras de Qt6/KF6 ya instaladas.
No usa la sesión de Plasma ni abre ventanas; ejecuta un receptor de argumentos.
"""

import json
import os
from pathlib import Path
import resource
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    flags = subprocess.check_output(["pkg-config", "--cflags", "--libs", "Qt6Gui"], text=True).split()
    includes = ["-I/usr/include/KF6/" + name for name in (
        "KIO", "KIOCore", "KIOGui", "KCoreAddons", "KService", "KConfig", "KConfigCore", "KConfigGui",
    )]
    checker = ROOT / "target/check-dolphin"
    subprocess.run([
        "c++", "-fPIC", "-std=c++17", str(ROOT / "tests/check_dolphin.cpp"), "-o", str(checker),
        *includes, *flags, "-lKF6KIOGui", "-lKF6KIOCore", "-lKF6ConfigGui", "-lKF6ConfigCore", "-lKF6CoreAddons",
    ], check=True)
    with tempfile.TemporaryDirectory(prefix="dolphin-smoke-", dir=ROOT / "target") as directory:
        work = Path(directory)
        for name in ("data/kio/servicemenus", "config", "cache", "runtime", "prefix con espacios $literal/bin"):
            (work / name).mkdir(parents=True, mode=0o700)
        (work / "data/mime").symlink_to("/usr/share/mime", target_is_directory=True)
        binary = work / "prefix con espacios $literal/bin/quicklook"
        binary.write_text(
            "#!/usr/bin/python3\nimport json, os, sys\n"
            "destination = os.environ['QUICKLOOK_TEST_RESULT']\n"
            "with open(destination + '.tmp', 'w') as output:\n"
            "    json.dump(sys.argv[1:], output)\n"
            "os.replace(destination + '.tmp', destination)\n",
            encoding="utf-8",
        )
        binary.chmod(0o755)
        selected = work / "foto con espacios;$(literal).png"
        selected.write_bytes(b"test selection")
        menu = work / "data/kio/servicemenus/quicklook.desktop"
        menu.write_bytes((ROOT / "integration/quicklook.desktop").read_bytes())
        menu.chmod(0o755)
        env = os.environ.copy()
        env.update(
            PATH="/usr/bin:/bin", QT_QPA_PLATFORM="offscreen",
            XDG_DATA_HOME=str(work / "data"), XDG_DATA_DIRS=str(work / "data"),
            XDG_CONFIG_HOME=str(work / "config"), XDG_CACHE_HOME=str(work / "cache"),
            XDG_RUNTIME_DIR=str(work / "runtime"),
            DBUS_SESSION_BUS_ADDRESS="unix:path=" + str(work / "unavailable-bus"),
            QUICKLOOK_TEST_RESULT=str(work / "received.json"),
        )
        args = [str(checker), str(menu), str(selected)]
        broken = subprocess.run(args, env=env, capture_output=True, text=True, timeout=10)
        assert broken.returncode == 1, broken.stdout + broken.stderr
        assert "not valid" in broken.stdout or "no es válido" in broken.stdout, broken.stdout
        generated = subprocess.check_output([sys.executable, str(ROOT / "integration/prepare-menu.py"), str(binary)])
        menu.write_bytes(generated)
        subprocess.run(["desktop-file-validate", str(menu)], check=True)
        fixed = subprocess.run(args, env=env, capture_output=True, text=True, timeout=10)
        assert fixed.returncode == 0, fixed.stdout + fixed.stderr
        received = work / "received.json"
        deadline = time.monotonic() + 5
        while not received.exists() and time.monotonic() < deadline:
            time.sleep(0.025)
        assert json.loads(received.read_text()) == [str(selected)]
        print("KDE: fallo original reproducido; acción corregida ejecutada sin PATH de usuario, con espacios y caracteres de shell: OK")


if __name__ == "__main__":
    main()
