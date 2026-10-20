#!/usr/bin/env python3
"""Visor Qt real con núcleo Rust, eventos de teclado y blur en KWin aislado."""
import argparse
import json
import os
from pathlib import Path
import resource
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib

from smoke_x11 import png, pdf

ROOT = Path(__file__).resolve().parents[1]


def wait(check, process, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = check()
        if value: return value
        assert process.poll() is None, "el proceso terminó antes de completar la prueba"
        time.sleep(0.05)
    raise AssertionError("tiempo agotado esperando el visor Qt")


def documents_session(work, destination, env, server, dbus, log):
    if dbus("/Effects", "isEffectLoaded", "blur").stdout.strip() != "true":
        assert dbus("/Effects", "loadEffect", "blur").stdout.strip() == "true"
    report_path = work / "documents.jsonl"
    viewer = None
    reports = []
    try:
        with report_path.open("w") as output:
            viewer = subprocess.Popen([str(work / "qt-controls"), str(work / "documents/1 notas.md")],
                env=env, stdin=subprocess.PIPE, stdout=output, stderr=log)

            def ready(name, kind, blur="native", after=0):
                def check():
                    lines = report_path.read_text().splitlines()[after:]
                    for line in reversed(lines):
                        if not line.startswith("{"): continue
                        item = json.loads(line)
                        if item["title"] == name + (" — Formato no compatible" if kind == "unsupported" else "") + " — Quick Look" and item["kind"] == kind and item["state"] == 1 and item["blur_mode"] == blur:
                            return item
                value = wait(check, viewer, timeout=65 if kind == "office" else 15)
                assert value["frame_width"] == round(value["screen_width"] * 0.9), value
                assert value["frame_height"] == round(value["screen_height"] * 0.9), value
                assert value["image_width"] <= value["screen_width"] * 0.85 + 1, value
                assert value["image_height"] <= value["screen_height"] * 0.85 + 1, value
                if kind != "image":
                    assert abs(value["image_width"] - value["screen_width"] * 0.85) <= 1, value
                    assert abs(value["image_height"] - value["screen_height"] * 0.85) <= 1, value
                assert value["filename"] == name, value
                assert value["filename_label"] == name, value
                assert value["filename_x"] > 0 and value["filename_y"] == 0, value
                assert value["filename_height"] <= round(value["frame_height"] * (1 - 85 / 90) / 2), value
                if kind == "pdf": assert value["pdf_pages"] == 2, value
                if kind == "folder": assert value["folder_entries"] == 2, value
                if kind == "office": assert value["pdf_pages"] == 1, value
                reports.append(value)
                return value

            def command(value):
                count = len(report_path.read_text().splitlines())
                viewer.stdin.write((value + "\n").encode()); viewer.stdin.flush()
                return count

            def capture(name):
                subprocess.run(["spectacle", "--background", "--nonotify", "--fullscreen", "--output",
                    str(destination / (name + ".png"))], env=env, stdout=log, stderr=log, check=True, timeout=20)

            ready("1 notas.md", "text"); capture("qt-documento-texto")
            command("content-next")
            ready("2 documento.pdf", "pdf"); capture("qt-documento-pdf")
            command("content-next"); ready("3 imagen.png", "image")
            command("previous"); ready("2 documento.pdf", "pdf")
            count = len(report_path.read_text().splitlines())
            assert dbus("/Effects", "unloadEffect", "blur").returncode == 0
            ready("2 documento.pdf", "pdf", "opaque", after=count)
            capture("qt-documento-opaco")
            count = len(report_path.read_text().splitlines())
            assert dbus("/Effects", "loadEffect", "blur").stdout.strip() == "true"
            ready("2 documento.pdf", "pdf", after=count)
            command("content-space")
            assert viewer.wait(timeout=5) == 0
            viewer = None
            viewer = subprocess.Popen([str(work / "qt-controls"), str(work / "documents/4 carpeta")],
                env=env, stdin=subprocess.PIPE, stdout=output, stderr=log)
            ready("4 carpeta", "folder"); capture("qt-documento-carpeta")
            if (work / "documents/5 documento.docx").exists():
                command("content-next"); ready("5 documento.docx", "office"); capture("qt-documento-office")
                count = command("previous"); ready("4 carpeta", "folder", after=count)
            command("content-space")
            assert viewer.wait(timeout=5) == 0
            viewer = None
            viewer = subprocess.Popen([str(work / "qt-controls"), str(work / "documents/6 archivo.zip")],
                env=env, stdin=subprocess.PIPE, stdout=output, stderr=log)
            ready("6 archivo.zip", "unsupported"); capture("qt-documento-no-compatible")
            command("next"); ready("1 notas.md", "text")
            command("content-space")
            assert viewer.wait(timeout=5) == 0
            viewer = None
        (destination / "documentos.json").write_text(json.dumps(reports, indent=2) + "\n")
        print("Texto, PDF, imagen y carpeta directa, contenido al 85 %, marco al 90 %, blur nativo → opaco → nativo y Espacio dentro del contenido: OK", flush=True)
    finally:
        if viewer is not None and viewer.poll() is None:
            viewer.terminate(); viewer.wait(timeout=5)


def session(work, scale, host_socket, exercise_activation, exercise_update, documents_only=False):
    env = os.environ.copy()
    env.pop("DISPLAY", None)
    env.update(WAYLAND_DISPLAY="qq", QT_QPA_PLATFORM="wayland", QT_QPA_PLATFORMTHEME="", QT_SCALE_FACTOR="1",
        NO_AT_BRIDGE="1", XDG_DATA_DIRS="/usr/local/share:/usr/share",
        QT_PLUGIN_PATH=str(ROOT / "target/softblur-build/plugins"))
    for key in ("QT_SCREEN_SCALE_FACTORS", "QT_FONT_DPI"):
        env.pop(key, None)
    if exercise_update: env['QT_PLUGIN_PATH'] = str(work / 'plugins')
    if host_socket: env.pop("LIBGL_ALWAYS_SOFTWARE", None)
    else: env["LIBGL_ALWAYS_SOFTWARE"] = "1"
    destination = ROOT / f"target/qt-wayland-{'gpu' if host_socket else 'software'}-{scale:g}"
    destination.mkdir(exist_ok=True)
    server = background = viewer = None
    reports = []
    with (work / "session.log").open("w") as log:
        try:
            subprocess.run(["kbuildsycoca6", "--noincremental"], env=env, stdout=log, stderr=log, check=True)
            command = ["kwin_wayland", "--width", "1280", "--height", "800", "--scale", str(scale),
                "--socket", "qq", "--no-lockscreen", "--no-global-shortcuts", "--no-kactivities"]
            command += ["--wayland-display", host_socket] if host_socket else ["--virtual"]
            server = subprocess.Popen(command, env=env, stdout=log, stderr=log)
            def dbus(path, *args):
                return subprocess.run(["qdbus6", "org.kde.KWin", path, *args], env=env,
                    capture_output=True, text=True, timeout=5)
            wait(lambda: "loadScript" in dbus("/Scripting").stdout, server)
            if scale != 1:
                configured = subprocess.run(["kscreen-doctor", f"output.1.scale.{scale}"], env=env,
                    capture_output=True, text=True, timeout=15)
                assert configured.returncode == 0, configured.stdout + configured.stderr
            if documents_only:
                background = subprocess.Popen([str(work / "blur-reference"), "--test-background",
                    "--test-stripe-width", "32"], env=env, stdout=log, stderr=log)
                documents_session(work, destination, env, server, dbus, log)
                return
            loaded = dbus("/Effects", "loadEffect", "quicklook_softblur" if exercise_update else "quicklook_softblur_v2")
            assert loaded.stdout.strip() == "true", loaded.stdout + loaded.stderr
            assert dbus("/QuickLookSoftBlur", "ready").stdout.strip() == "true"
            if exercise_update:
                assert dbus('/QuickLookSoftBlur', 'protocolVersion').stdout.strip() == '1'
                assert dbus('/Effects', 'unloadEffect', 'quicklook_softblur').returncode == 0
                plugin_dir = work / 'plugins/kwin/effects/plugins'
                shutil.copy2(ROOT / 'target/softblur-build/plugins/kwin/effects/plugins/quicklook_softblur_v2.so',
                    plugin_dir / 'quicklook_softblur_v2.so')
                assert dbus('/Effects', 'loadEffect', 'quicklook_softblur_v2').stdout.strip() == 'true'
            assert dbus('/QuickLookSoftBlur', 'protocolVersion').stdout.strip() == '2'
            if exercise_update: print('Actualización del módulo v1 → v2 sin reiniciar KWin privado: OK', flush=True)
            background = subprocess.Popen([str(work / "blur-reference"), "--test-background",
                "--test-stripe-width", "32"], env=env, stdout=log, stderr=log)
            time.sleep(0.5)
            def capture(name):
                subprocess.run(["spectacle", "--background", "--nonotify", "--fullscreen", "--output",
                    str(destination / (name + ".png"))], env=env, stdout=log, stderr=log, check=True, timeout=20)
            capture('background')
            # Ventana ajena al efecto propio: el blur general sigue su configuración.
            for strength in (3, 15):
                subprocess.run(["kwriteconfig6", "--file", str(work / "config/kwinrc"), "--group",
                    "Effect-blur", "--key", "BlurStrength", str(strength)], env=env, check=True)
                assert dbus("/Effects", "reconfigureEffect", "blur").returncode == 0
                viewer = subprocess.Popen([str(work / "blur-reference"),
                    str(work / "images/1 verde.png")], env=env, stdout=log, stderr=log)
                time.sleep(1)
                capture(f"nativo-{strength}")
                viewer.terminate(); viewer.wait(timeout=5); viewer = None

            report_path = work / "report.jsonl"
            with report_path.open("w") as output:
                viewer = subprocess.Popen([str(work / "qt-controls"), str(work / "images/1 verde.png")],
                    env=env, stdin=subprocess.PIPE, stdout=output, stderr=log)
                def records():
                    return [json.loads(line) for line in report_path.read_text().splitlines() if line.startswith('{')]
                def ready(title, state, blur="custom", after=0):
                    def check():
                        values = records()[after:]
                        matches = [v for v in values if v["title"].startswith(title) and v["state"] == state
                                   and v["blur_mode"] == blur]
                        return matches[-1] if matches else None
                    value = wait(check, viewer)
                    assert "[QLB:" not in value["title"], value
                    assert abs(value["frame_width"] - value["screen_width"] * 0.9) <= 1, value
                    assert abs(value["frame_height"] - value["screen_height"] * 0.9) <= 1, value
                    if host_socket:
                        # El backend Wayland anidado anuncia la escala del anfitrión,
                        # que puede prevalecer sobre la solicitada al iniciar KWin.
                        assert value["device_pixel_ratio"] > 0, value
                    else:
                        assert abs(value["device_pixel_ratio"] - scale) < 0.001, value
                    assert value['blur_enabled'] == (blur != 'opaque'), value
                    assert value['opacity'] == (1.0 if blur == 'opaque' else 0.4), value
                    assert 0 < value['corner_radius'] <= 12, value
                    if state == 1:
                        assert value["image_width"] <= value["frame_width"] * 85 / 90 + 1, value
                        assert value["image_height"] <= value["frame_height"] * 85 / 90 + 1, value
                    reports.append(value)
                    time.sleep(0.25)
                    return value
                def command(value):
                    viewer.stdin.write((value + "\n").encode()); viewer.stdin.flush()
                ready("1 verde.png", 1); capture("qt-inicial")
                count = len(records()); command("next"); ready("2 error.png", 2, after=count); capture("qt-error")
                count = len(records()); command("next"); ready("3 azul.png", 1, after=count); capture("qt-horizontal")
                count = len(records()); command("next"); ready("4 alpha.png", 1, after=count); capture("qt-alpha")
                count = len(records()); command("next"); ready("1 verde.png", 1, after=count)
                count = len(records()); command("rapid"); ready("1 verde.png", 1, after=count); capture("qt-rapida")
                command("repeat-space")
                wait(lambda: "repeat-space: ignorado" in report_path.read_text(), viewer)
                assert viewer.poll() is None
                count = len(records())
                assert dbus("/Effects", "unloadEffect", "quicklook_softblur_v2").returncode == 0
                ready("1 verde.png", 1, blur="native", after=count); capture("qt-nativo")
                count = len(records()); command("previous"); ready("4 alpha.png", 1, blur="native", after=count)
                capture("qt-nativo-alpha")
                count = len(records()); command("next"); ready("1 verde.png", 1, blur="native", after=count)
                count = len(records())
                assert dbus("/Effects", "unloadEffect", "blur").returncode == 0
                ready("1 verde.png", 1, blur="opaque", after=count); capture("qt-sin-efecto")
                count = len(records())
                assert dbus("/Effects", "loadEffect", "blur").stdout.strip() == "true"
                ready("1 verde.png", 1, blur="native", after=count); capture("qt-nativo-recargado")
                count = len(records())
                assert dbus("/Effects", "loadEffect", "quicklook_softblur_v2").stdout.strip() == "true"
                ready("1 verde.png", 1, after=count); capture("qt-recargado")
                command("space"); assert viewer.wait(timeout=5) == 0; viewer = None
                for action in ("escape", "focus-away"):
                    viewer = subprocess.Popen([str(work / "qt-controls"), str(work / "images/1 verde.png")],
                        env=env, stdin=subprocess.PIPE, stdout=log, stderr=log)
                    time.sleep(1)
                    viewer.stdin.write((action + "\n").encode()); viewer.stdin.flush()
                    assert viewer.wait(timeout=5) == 0, action
                    viewer = None
            checked = subprocess.run([str(work / "check-qt-captures"), str(destination)],
                capture_output=True, text=True, check=True, env=env)
            print(checked.stdout, end="", flush=True)
            if exercise_activation:
                if exercise_update:
                    assert dbus('/Effects', 'unloadEffect', 'quicklook_softblur_v2').returncode == 0
                    assert dbus('/Effects', 'loadEffect', 'quicklook_softblur').stdout.strip() == 'true'
                    assert dbus('/QuickLookSoftBlur', 'protocolVersion').stdout.strip() == '1'
                    subprocess.run(['kwriteconfig6', '--file', str(work / 'config/kwinrc'), '--group',
                        'Plugins', '--key', 'quicklook_softblurEnabled', '--type', 'bool', 'true'], env=env, check=True)
                prefix = work / 'prefix'
                (prefix / 'bin').mkdir(parents=True)
                installed = prefix / 'bin/quicklook'
                original = ROOT / 'target/release/quicklook'
                shutil.copy2(original, installed)
                original_config = (work / 'config/kwinrc').read_text()
                module = ROOT / 'target/softblur-build/plugins/kwin/effects/plugins/quicklook_softblur_v2.so'
                activated = subprocess.run([sys.executable, str(ROOT / 'integration/activate-blur.py'),
                    '--activate', '--prefix', str(prefix), '--module', str(module)], env=env,
                    capture_output=True, text=True, check=True)
                assert installed.read_bytes() == (ROOT / 'target/qt-build/quicklook-qt').read_bytes()
                assert (prefix / 'bin/quicklook-opaque').read_bytes() == original.read_bytes()
                assert dbus('/QuickLookSoftBlur', 'protocolVersion').stdout.strip() == '2'
                assert dbus('/Effects', 'isEffectLoaded', 'quicklook_softblur').stdout.strip() == 'false'
                backup = Path(activated.stdout.strip().split('Respaldo: ')[-1])
                subprocess.run(['kwriteconfig6', '--file', str(work / 'config/kwinrc'),
                    '--group', 'QuicklookUnrelatedTest', '--key', 'sentinel', 'preserved'], env=env, check=True)
                restored = subprocess.run([sys.executable, str(ROOT / 'integration/activate-blur.py'),
                    '--restore', str(backup)], env=env, capture_output=True, text=True, check=True)
                assert installed.read_bytes() == original.read_bytes()
                assert 'sentinel=preserved' in (work / 'config/kwinrc').read_text()
                assert 'quicklook_softblur_v2Enabled=' not in (work / 'config/kwinrc').read_text()
                assert 'BlurStrength=15' in original_config and 'BlurStrength=15' in (work / 'config/kwinrc').read_text()
                if exercise_update:
                    assert dbus('/QuickLookSoftBlur', 'protocolVersion').stdout.strip() == '1'
                    assert dbus('/Effects', 'isEffectLoaded', 'quicklook_softblur').stdout.strip() == 'true'
                    assert 'quicklook_softblurEnabled=true' in (work / 'config/kwinrc').read_text()
                print('Activación/reversión en prefijo privado: binario, respaldo y preferencias ajenas conservados: OK', flush=True)
            actual_scale = reports[-1]['device_pixel_ratio']
            (destination / "resultados.json").write_text(json.dumps({"reports": reports,
                "metrics": json.loads(checked.stdout), "scale": actual_scale,
                "requested_scale": scale, "hardware": bool(host_socket)}, indent=2) + "\n")
            print(f"Qt + núcleo Rust: navegación, alpha opaco, cambios rápidos, blur propio → nativo → opaco y recargas; escala real {actual_scale}: OK", flush=True)
        except Exception as error:
            if isinstance(error, subprocess.CalledProcessError): print(error.stderr)
            print((work / "session.log").read_text(errors="replace")[-4500:])
            raise
        finally:
            for process in (viewer, background, server):
                if process is not None and process.poll() is None:
                    process.terminate(); process.wait(timeout=5)


def main():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, help=argparse.SUPPRESS)
    parser.add_argument("--scale", type=float, default=1.0, choices=(1.0, 1.5, 2.0))
    parser.add_argument("--documents-only", action="store_true", help="texto/PDF con blur nativo y opaco, sin módulo propio de KWin")
    parser.add_argument("--gpu", action="store_true", help="usar una ventana anidada en la GPU real")
    parser.add_argument("--host-socket", help=argparse.SUPPRESS)
    parser.add_argument('--exercise-activation', action='store_true', help='probar activación y reversión en un prefijo privado')
    parser.add_argument('--exercise-update', action='store_true', help='probar actualización del módulo nativo sin reiniciar el compositor privado')
    args = parser.parse_args()
    if args.session:
        session(args.session, args.scale, args.host_socket, args.exercise_activation, args.exercise_update, args.documents_only)
        return
    host = None
    if args.gpu:
        display = os.environ["WAYLAND_DISPLAY"]
        host = display if display.startswith("/") else str(Path(os.environ["XDG_RUNTIME_DIR"]) / display)
    with tempfile.TemporaryDirectory(prefix="q-", dir=ROOT / "target") as directory:
        work = Path(directory)
        flags = subprocess.check_output(["pkg-config", "--cflags", "--libs", "Qt6Test", "Qt6Widgets", "Qt6DBus", "Qt6Concurrent", "KF6WindowSystem"], text=True).split()
        subprocess.run(["c++", "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror", "-fPIC",
            str(ROOT / "tests/qt_controls.cpp"), "-I" + str(ROOT / "target/qt-build/quicklook-qt_autogen/include"),
            str(ROOT / "target/qt-build/quicklook-qt_autogen/mocs_compilation.cpp"),
            str(ROOT / "qt/officeconverter.cpp"),
            str(ROOT / "target/release/libquicklook_core.a"), *flags, "-ldl", "-lpthread", "-lm",
            "-o", str(work / "qt-controls")], check=True)
        subprocess.run(["c++", "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror", "-fPIC",
            str(ROOT / "tests/blur_reference.cpp"), *flags, "-o", str(work / "blur-reference")], check=True)
        gui_flags = subprocess.check_output(["pkg-config", "--cflags", "--libs", "Qt6Gui"], text=True).split()
        subprocess.run(["c++", "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror", "-fPIC",
            str(ROOT / "tests/check_qt_captures.cpp"), *gui_flags, "-o", str(work / "check-qt-captures")], check=True)
        if args.documents_only:
            shutil.copy2(ROOT / "target/qt-build/quicklook-pdf.so", work / "quicklook-pdf.so")
        for name in ("config", "data", "cache", "state", "r", "images", "documents"):
            (work / name).mkdir(mode=0o700)
        shutil.copytree(ROOT / "integration/kwin", work / "data/kwin/scripts/quicklook-center")
        if args.exercise_update:
            plugin_dir = work / 'plugins/kwin/effects/plugins'
            plugin_dir.mkdir(parents=True)
            shutil.copy2(ROOT / 'target/medicion-carga/softblur-v1.so', plugin_dir / 'quicklook_softblur.so')
        (work / "config/kwinrc").write_text("[Plugins]\nquicklook-centerEnabled=true\nblurEnabled=true\n"
            "[Effect-blur]\nBlurStrength=15\nNoiseStrength=0\n")
        (work / "config/kdeglobals").write_text("[Colors:Window]\nBackgroundNormal=255,255,255\n")
        png(work / "images/1 verde.png", [0, 255, 0], 800, 1200)
        (work / "images/2 error.png").write_bytes(b"invalid")
        png(work / "images/3 azul.png", [0, 0, 255], 1200, 600)
        (work / "documents/1 notas.md").write_text("# Texto literal\nPrueba de lectura y selección.\n" * 50)
        (work / "documents/6 archivo.zip").write_bytes(b"PK\x03\x04binary")
        pdf(work / "documents/2 documento.pdf")
        png(work / "documents/3 imagen.png", [0, 0, 255], 1200, 600)
        folder = work / "documents/4 carpeta"
        (folder / "subcarpeta").mkdir(parents=True)
        (folder / "directo.txt").write_text("Contenido directo.\n")
        (folder / "subcarpeta/anidado.txt").write_text("No se debe recorrer.\n")
        office_fixtures = os.environ.get("QUICKLOOK_OFFICE_FIXTURES")
        if office_fixtures:
            shutil.copy2(Path(office_fixtures) / "text.docx", work / "documents/5 documento.docx")
        def chunk(kind, value):
            return struct.pack(">I", len(value)) + kind + value + struct.pack(">I", zlib.crc32(kind + value))
        rows = (b"\0" + bytes([255, 0, 0, 128]) * 200) * 400
        (work / "images/4 alpha.png").write_bytes(b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", 200, 400, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))
        bus = work / "bus.conf"
        bus.write_text(f'<busconfig><type>session</type><listen>unix:tmpdir={work / "r"}</listen>'
            '<auth>EXTERNAL</auth><policy context="default"><allow send_destination="*"/>'
            '<allow receive_sender="*"/><allow own="*"/></policy></busconfig>')
        env = os.environ.copy()
        env.update(XDG_CONFIG_HOME=str(work / "config"), XDG_DATA_HOME=str(work / "data"),
            XDG_CACHE_HOME=str(work / "cache"), XDG_STATE_HOME=str(work / "state"), XDG_RUNTIME_DIR=str(work / "r"))
        command = ["dbus-run-session", "--config-file=" + str(bus), "--", sys.executable,
            str(Path(__file__).resolve()), "--session", str(work), "--scale", str(args.scale)]
        if host: command += ["--host-socket", host]
        if args.documents_only: command.append("--documents-only")
        if args.exercise_activation: command.append('--exercise-activation')
        if args.exercise_update: command.append('--exercise-update')
        subprocess.run(command, env=env, check=True, timeout=120)


if __name__ == "__main__": main()
