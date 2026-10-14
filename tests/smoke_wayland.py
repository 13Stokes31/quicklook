#!/usr/bin/env python3
"""Comprueba tamaño y centrado en KWin Wayland virtual, sin usar el perfil personal.

Requiere kwin_wayland, qdbus6, dbus-run-session, c++ y las cabeceras de Qt6DBus.
"""

import argparse
import json
import os
from pathlib import Path
import resource
import shutil
import subprocess
import sys
import tempfile
import time

from smoke_x11 import png

ROOT = Path(__file__).resolve().parents[1]


def wait_for(check, server, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = check()
        if value:
            return value
        assert server.poll() is None, "el KWin virtual terminó antes de completar la prueba"
        time.sleep(0.05)
    raise AssertionError("tiempo agotado en la prueba de KWin Wayland")


def session(work, binary):
    env = os.environ.copy()
    env.pop("DISPLAY", None)
    env.update(WAYLAND_DISPLAY="quicklook-test", LIBGL_ALWAYS_SOFTWARE="1", NO_AT_BRIDGE="1")
    geometry = work / "geometry.jsonl"
    recorder = subprocess.Popen([str(ROOT / "target/kwin-geometry-receiver"), str(geometry)], env=env)
    watcher = work / "watch.js"
    watcher.write_text("""
function watch(w) {
    function report() {
        var g = w.frameGeometry;
        var a = workspace.clientArea(KWin.PlacementArea, w);
        callDBus('org.quicklook.Geometry', '/Geometry', 'org.quicklook.Geometry', 'report',
            JSON.stringify({title:w.caption, resourceClass:String(w.resourceClass),
                x:g.x, y:g.y, width:g.width, height:g.height,
                area:{x:a.x, y:a.y, width:a.width, height:a.height}}));
    }
    w.frameGeometryChanged.connect(report);
    w.captionChanged.connect(report);
    report();
}
workspace.windowAdded.connect(watch);
workspace.windowList().forEach(watch);
""", encoding="utf-8")
    viewer = None
    with (work / "kwin.log").open("w") as log, (work / "viewer.log").open("w") as viewer_log:
        server = subprocess.Popen([
            "kwin_wayland", "--virtual", "--width", "1280", "--height", "800",
            "--socket", "quicklook-test", "--no-lockscreen", "--no-global-shortcuts", "--no-kactivities",
        ], env=env, stdout=log, stderr=log)
        try:
            def qdbus(*args):
                return subprocess.run(["qdbus6", "org.kde.KWin", *args], env=env, capture_output=True, text=True, timeout=5)

            wait_for(lambda: "loadScript" in qdbus("/Scripting").stdout, server)

            def load(script, name):
                result = qdbus("/Scripting", "loadScript", script, name)
                assert result.returncode == 0, result.stderr
                started = qdbus("/Scripting/Script" + result.stdout.strip(), "run")
                assert started.returncode == 0, started.stderr

            load(str(watcher), "quicklook-test-recorder")

            def records():
                if not geometry.exists():
                    return []
                return [json.loads(line) for line in geometry.read_text().splitlines() if line]

            for name, width, height in (("large.png", 2400, 1600), ("small.png", 400, 300), ("portrait.png", 1200, 2400)):
                path = work / name
                png(path, [0, 255, 0], width, height)
                viewer = subprocess.Popen([str(binary), str(path)], env=env, stdout=viewer_log, stderr=viewer_log)
                stable_since = None

                def correct_geometry():
                    nonlocal stable_since
                    candidates = [r for r in records() if r["title"] == name + " — Quick Look"]
                    if not candidates:
                        stable_since = None
                        return None
                    value = candidates[-1]
                    area = value["area"]
                    expected_width = area["width"] * 0.90
                    expected_height = area["height"] * 0.90
                    if abs(value["width"] - expected_width) > 1 or abs(value["height"] - expected_height) > 1:
                        stable_since = None
                        return None
                    if abs(value["x"] - (area["x"] + (area["width"] - value["width"]) / 2)) > 1:
                        stable_since = None
                        return None
                    if abs(value["y"] - (area["y"] + (area["height"] - value["height"]) / 2)) > 1:
                        stable_since = None
                        return None
                    # No dar por buena una ampliación transitoria seguida de
                    # una configuración inicial tardía enviada por KWin.
                    if stable_since is None:
                        stable_since = time.monotonic()
                    return value if time.monotonic() - stable_since >= 0.5 else None

                value = wait_for(correct_geometry, server)
                assert viewer.poll() is None, "el visor se cerró durante la prueba"
                print(f"{name}: {value['width']}×{value['height']}, centrada en ({value['x']}, {value['y']}): OK")
                viewer.terminate()
                viewer.wait(timeout=5)
                viewer = None
        except Exception:
            print("Geometrías registradas:", geometry.read_text()[-4000:] if geometry.exists() else "ninguna")
            print("Registro de KWin:", (work / "kwin.log").read_text(errors="replace")[-2500:])
            raise
        finally:
            if viewer and viewer.poll() is None:
                viewer.terminate()
                viewer.wait(timeout=5)
            server.terminate()
            server.wait(timeout=5)
            recorder.terminate()
            recorder.wait(timeout=5)


def main():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "target/release/quicklook")
    parser.add_argument("--session", type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.session:
        session(args.session, args.binary.resolve())
        return
    flags = subprocess.check_output(["pkg-config", "--cflags", "--libs", "Qt6DBus"], text=True).split()
    subprocess.run([
        "c++", "-fPIC", "-std=c++17", str(ROOT / "tests/kwin_geometry_receiver.cpp"),
        "-o", str(ROOT / "target/kwin-geometry-receiver"), *flags,
    ], check=True)
    with tempfile.TemporaryDirectory(prefix="wayland-smoke-", dir=ROOT / "target") as directory:
        work = Path(directory)
        for name in ("config", "data", "cache", "runtime"):
            (work / name).mkdir(mode=0o700)
        # Activar el paquete exactamente como se haría en el perfil del usuario,
        # pero usando exclusivamente el perfil privado de esta prueba.
        shutil.copytree(ROOT / "integration/kwin", work / "data/kwin/scripts/quicklook-center")
        (work / "config/kwinrc").write_text("[Plugins]\nquicklook-centerEnabled=true\n")
        config = work / "bus.conf"
        config.write_text(f"""<busconfig><type>session</type><listen>unix:tmpdir={work / 'runtime'}</listen>
<auth>EXTERNAL</auth><policy context="default"><allow send_destination="*"/>
<allow receive_sender="*"/><allow own="*"/></policy></busconfig>""")
        env = os.environ.copy()
        env.update(
            XDG_CONFIG_HOME=str(work / "config"), XDG_DATA_HOME=str(work / "data"),
            XDG_CACHE_HOME=str(work / "cache"), XDG_RUNTIME_DIR=str(work / "runtime"),
            PYTHONDONTWRITEBYTECODE="1",
        )
        subprocess.run([
            "dbus-run-session", "--config-file=" + str(config), "--", sys.executable,
            str(Path(__file__).resolve()), "--session", str(work), "--binary", str(args.binary.resolve()),
        ], env=env, check=True, timeout=60)


if __name__ == "__main__":
    main()
