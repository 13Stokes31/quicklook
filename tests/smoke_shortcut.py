#!/usr/bin/env python3
"""Dolphin real en Xvfb: Espacio abre la selección y vuelve a cerrar Quick Look."""
import argparse
import json
import os
from pathlib import Path
import select
import shutil
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ET

from smoke_x11 import png, pdf

ROOT = Path(__file__).resolve().parents[1]


def wait(check, process, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = check()
        if value:
            return value
        assert process.poll() is None, "Dolphin terminó antes de completar la prueba"
        time.sleep(0.1)
    raise AssertionError("tiempo agotado esperando a Dolphin/Quick Look")


def session(work):
    selected = next((work / "images").iterdir())
    env = os.environ.copy()
    env.pop("WAYLAND_DISPLAY", None)
    env.update(QT_QPA_PLATFORM="xcb", QT_QPA_PLATFORMTHEME="", NO_AT_BRIDGE="1")
    read_fd, write_fd = os.pipe()
    dolphin = None
    with (work / "session.log").open("w") as log:
        server = subprocess.Popen(["Xvfb", "-displayfd", str(write_fd), "-screen", "0",
            "1280x800x24", "-nolisten", "tcp", "-noreset"], pass_fds=(write_fd,), stdout=log, stderr=log)
        os.close(write_fd)
        try:
            assert select.select([read_fd], [], [], 15)[0]
            number = os.read(read_fd, 64).decode().strip()
            assert number.isdigit()
            env["DISPLAY"] = ":" + number
            subprocess.run(["kbuildsycoca6", "--noincremental"], env=env, stdout=log, stderr=log, check=True)
            dolphin = subprocess.Popen(["dolphin", "--new-window", str(work / "images")],
                env=env, stdout=log, stderr=log)

            def xdo(*args):
                return subprocess.run(["xdotool", *map(str, args)], env=env,
                    capture_output=True, text=True, timeout=5)

            window = wait(lambda: xdo("search", "--onlyvisible", "--pid", dolphin.pid).stdout.strip(), dolphin).splitlines()[-1]
            xdo("windowfocus", "--sync", window)
            # La lista debe haber terminado de cargar antes de seleccionar.
            time.sleep(2)
            xdo("key", "--window", window, "ctrl+a")
            time.sleep(0.3)
            xdo("key", "--window", window, "space")
            preview = wait(lambda: xdo("search", "--onlyvisible", "--name", selected.stem + ".*Quick Look").stdout.strip(), dolphin).splitlines()[-1]
            xdo("windowfocus", "--sync", preview)
            # Abrir una ventana no demuestra que se haya cargado el documento.
            # Esperar el estado del visor y, en Office, el PDF convertido.
            def rendered():
                report = work / "preview.jsonl"
                if not report.is_file(): return None
                for line in reversed(report.read_text().splitlines()):
                    if not line.startswith("{"): continue
                    value = json.loads(line)
                    if value["state"] == 2: raise AssertionError(value)
                    if value["state"] == 1:
                        return value
                return None
            value = wait(rendered, dolphin, timeout=65 if selected.suffix == ".docx" else 15)
            assert value["title"] == selected.name + " — Quick Look", value
            if selected.is_dir(): assert value["folder_entries"] == 2, value
            if selected.suffix in (".pdf", ".docx"): assert value["pdf_pages"] > 0, value
            xdo("key", "--window", preview, "space")
            wait(lambda: not xdo("search", "--onlyvisible", "--name", selected.stem + ".*Quick Look").stdout.strip(), dolphin)
            xml = ET.parse(work / "data/kxmlgui5/dolphin/dolphinui.rc").getroot()
            assert xml.find("MenuBar") is not None, "la migración de XML eliminó los menús"
            shortcuts = {a.get("name"): a.get("shortcut") for a in xml.findall("ActionProperties/Action")}
            assert shortcuts["servicemenu_quicklook.desktop::quicklookPreview"] == "Space"
            assert shortcuts["toggle_selection_mode"] == ""
            print(f"Dolphin real: Espacio abre y carga {selected.name} y la cierra; menús conservados: OK", flush=True)
        except Exception:
            print((work / "session.log").read_text(errors="replace")[-3000:])
            report = work / "preview.jsonl"
            if report.is_file(): print(report.read_text(errors="replace")[-3000:])
            raise
        finally:
            if dolphin is not None and dolphin.poll() is None:
                dolphin.terminate()
                dolphin.wait(timeout=5)
            server.terminate()
            server.wait(timeout=5)
            os.close(read_fd)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, help=argparse.SUPPRESS)
    parser.add_argument("--binary", type=Path, default=ROOT / "target/qt-build/quicklook-qt")
    parser.add_argument("--kind", choices=("image", "text", "pdf", "folder", "office"), default="image")
    args = parser.parse_args()
    if args.session:
        session(args.session)
        return
    with tempfile.TemporaryDirectory(prefix="atajo-", dir=ROOT / "target") as directory:
        work = Path(directory)
        for name in ("config", "data/kio/servicemenus", "cache", "state", "r", "images"):
            (work / name).mkdir(parents=True, mode=0o700)
        if args.kind == "image":
            png(work / "images/foto prueba.png", [0, 255, 0], 400, 300)
        elif args.kind == "text":
            (work / "images/texto prueba.md").write_text("# Texto literal\nPrueba con acentos.\n")
        elif args.kind == "pdf":
            pdf(work / "images/documento prueba.pdf")
        elif args.kind == "folder":
            folder = work / "images/carpeta prueba"
            folder.mkdir()
            (folder / "directo.txt").write_text("Solo contenido directo.\n")
            (folder / "subcarpeta").mkdir()
            (folder / "subcarpeta/no recorrer.txt").write_text("Contenido anidado.\n")
        else:
            fixtures = Path(os.environ.get("QUICKLOOK_OFFICE_FIXTURES", str(ROOT / "target/office-fixtures")))
            if not (fixtures / "text.docx").is_file():
                parser.error("Genera antes los documentos: python3 tests/smoke_office.py --fixtures-only")
            shutil.copy2(fixtures / "text.docx", work / "images/documento prueba.docx")
        launcher = work / "preview"
        launcher.write_text("#!/usr/bin/python3\nimport os, sys\n"
            f"fd = os.open({str(work / 'preview.jsonl')!r}, os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600)\n"
            "os.dup2(fd, 1)\nos.dup2(fd, 2)\nos.close(fd)\n"
            f"os.execv({str(args.binary.absolute())!r}, [{str(args.binary.absolute())!r}, '--report', *sys.argv[1:]])\n")
        launcher.chmod(0o755)
        menu = work / "data/kio/servicemenus/quicklook.desktop"
        menu.write_bytes(subprocess.check_output([sys.executable, str(ROOT / "integration/prepare-menu.py"),
            str(launcher)]))
        menu.chmod(0o755)
        env = os.environ.copy()
        env.update(XDG_CONFIG_HOME=str(work / "config"), XDG_DATA_HOME=str(work / "data"),
            XDG_CACHE_HOME=str(work / "cache"), XDG_STATE_HOME=str(work / "state"), XDG_RUNTIME_DIR=str(work / "r"))
        subprocess.run([sys.executable, str(ROOT / "integration/configure-shortcut.py")], env=env, check=True)
        bus = work / "bus.conf"
        bus.write_text(f'<busconfig><type>session</type><listen>unix:tmpdir={work / "r"}</listen>'
            '<auth>EXTERNAL</auth><policy context="default"><allow send_destination="*"/>'
            '<allow receive_sender="*"/><allow own="*"/></policy></busconfig>')
        subprocess.run(["dbus-run-session", "--config-file=" + str(bus), "--", sys.executable,
            str(Path(__file__).resolve()), "--session", str(work)], env=env, check=True, timeout=100)


if __name__ == "__main__":
    main()
