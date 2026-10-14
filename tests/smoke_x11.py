#!/usr/bin/env python3
"""Comprueba ventana, navegación y cierre en Xvfb, sin usar la sesión personal.

Requiere el binario release, Xvfb, xdotool y libX11, sin módulos Python externos.
"""

import argparse
import ctypes
import os
from pathlib import Path
import select
import struct
import subprocess
import tempfile
import time
import zlib

ROOT = Path(__file__).resolve().parents[1]


def png(path, color, width=1200, height=800):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    rows = (b"\0" + bytes(color) * width) * height
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(rows))
        + chunk(b"IEND", b"")
    )


def wait_for(check, process, timeout=8):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = check()
        if value:
            return value
        assert process.poll() is None, "el visor terminó antes de completar la prueba"
        time.sleep(0.025)
    raise AssertionError("tiempo agotado esperando a la ventana")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "target/release/quicklook")
    binary = parser.parse_args().binary.resolve()
    assert binary.is_file(), "compila primero con cargo build --release --locked"
    with tempfile.TemporaryDirectory(prefix="x11-smoke-", dir=ROOT / "target") as directory:
        work = Path(directory)
        for name in ("runtime", "cache", "config", "data"):
            (work / name).mkdir(mode=0o700)
        palette = work / "config/kdeglobals"
        palette.write_text("[Colors:Window]\nBackgroundNormal=255,255,255\n")
        first = work / "1 prueba;$(literal).png"
        bad = work / "2 dañada.png"
        last = work / "3 final.png"
        png(first, [0, 255, 0])
        bad.write_bytes(b"not an image")
        png(last, [0, 0, 255], width=200, height=400)
        read_fd, write_fd = os.pipe()
        with (work / "xvfb.log").open("w") as server_log, (work / "viewer.log").open("w") as viewer_log:
            server = subprocess.Popen(
                ["Xvfb", "-displayfd", str(write_fd), "-screen", "0", "640x360x24", "-nolisten", "tcp", "-noreset"],
                pass_fds=(write_fd,), stdout=server_log, stderr=server_log,
            )
            os.close(write_fd)
            viewer = None
            display = None
            try:
                assert select.select([read_fd], [], [], 15)[0], "Xvfb no pudo crear una pantalla"
                number = os.read(read_fd, 64).decode().strip()
                assert number.isdigit(), "Xvfb no devolvió un número de pantalla"
                env = os.environ.copy()
                env.pop("WAYLAND_DISPLAY", None)
                env.update(
                    DISPLAY=f":{number}", XDG_RUNTIME_DIR=str(work / "runtime"),
                    XDG_CACHE_HOME=str(work / "cache"), XDG_CONFIG_HOME=str(work / "config"),
                    XDG_DATA_HOME=str(work / "data"),
                    DBUS_SESSION_BUS_ADDRESS="unix:path=" + str(work / "unavailable-bus"),
                )

                def xdo(*args):
                    return subprocess.run(["xdotool", *map(str, args)], env=env, capture_output=True, text=True, timeout=5)

                xlib = ctypes.CDLL("libX11.so.6")
                xlib.XOpenDisplay.argtypes = [ctypes.c_char_p]
                xlib.XOpenDisplay.restype = ctypes.c_void_p
                xlib.XGetImage.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_int, ctypes.c_uint, ctypes.c_uint, ctypes.c_ulong, ctypes.c_int]
                xlib.XGetImage.restype = ctypes.c_void_p
                xlib.XGetPixel.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
                xlib.XGetPixel.restype = ctypes.c_ulong
                xlib.XDestroyImage.argtypes = [ctypes.c_void_p]
                xlib.XCloseDisplay.argtypes = [ctypes.c_void_p]
                xlib.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
                xlib.XDefaultRootWindow.restype = ctypes.c_ulong
                display = xlib.XOpenDisplay(env["DISPLAY"].encode())
                assert display, "no se pudo conectar a Xvfb"

                def open_viewer(path):
                    nonlocal viewer
                    viewer = subprocess.Popen([str(binary), str(path)], env=env, stdout=viewer_log, stderr=viewer_log)
                    window = int(wait_for(lambda: xdo("search", "--onlyvisible", "--pid", viewer.pid).stdout.strip(), viewer).splitlines()[0])
                    assert xdo("windowfocus", "--sync", window).returncode == 0
                    return window

                def ready(window, name, error=False):
                    def check():
                        title = xdo("getwindowname", window).stdout.strip()
                        return title.startswith(name + " — ") and (
                            "No se pudo abrir:" in title if error else title == name + " — Quick Look"
                        )
                    wait_for(check, viewer)

                def pixel_at(window, x, y):
                    image = xlib.XGetImage(display, window, x, y, 1, 1, ctypes.c_ulong(-1).value, 2)
                    assert image, "no se pudo leer el píxel de la ventana de prueba"
                    pixel = xlib.XGetPixel(image, 0, 0) & 0xFFFFFF
                    xlib.XDestroyImage(image)
                    return pixel

                def center(window):
                    geometry = dict(line.split("=", 1) for line in xdo("getwindowgeometry", "--shell", window).stdout.splitlines())
                    width, height = int(geometry["WIDTH"]), int(geometry["HEIGHT"])
                    pixel = pixel_at(window, width // 2, height // 2)
                    return pixel, width, height

                def painted(window, color):
                    return wait_for(lambda: (value if (value := center(window))[0] == color else None), viewer)

                window = open_viewer(first)
                ready(window, first.name)
                _, width, height = painted(window, 0x00FF00)
                def final_size():
                    value = center(window)
                    return value if value[1:] == (576, 324) else None
                _, width, height = wait_for(final_size, viewer)
                assert pixel_at(window, 5, height // 2) == 0xFFFFFF, "el margen no sigue el fondo blanco de KDE"
                assert pixel_at(window, width // 2, 8) == 0xFFFFFF, "falta el marco por encima de la imagen"
                assert pixel_at(window, width // 2, 9) == 0x00FF00, "la imagen no alcanza el 85 % del alto del monitor"
                assert pixel_at(window, width // 2, height - 10) == 0x00FF00
                assert pixel_at(window, width // 2, height - 9) == 0xFFFFFF
                geometry = dict(line.split("=", 1) for line in xdo("getwindowgeometry", "--shell", window).stdout.splitlines())
                assert abs(int(geometry["X"]) - (640 - width) / 2) <= 1, geometry
                assert abs(int(geometry["Y"]) - (360 - height) / 2) <= 1, geometry
                assert xdo("key", "--window", window, "Right").returncode == 0
                ready(window, bad.name, error=True)
                painted(window, 0xE57373)
                xdo("key", "--window", window, "Right")
                ready(window, last.name)
                painted(window, 0x0000FF)
                assert center(window)[1:] == (576, 324), "la imagen vertical cambió el recuadro"
                assert pixel_at(window, 100, height // 2) == 0xFFFFFF, "la imagen vertical no conserva el fondo de KDE"
                xdo("key", "--window", window, "Right")
                ready(window, first.name)
                painted(window, 0x00FF00)
                xdo("key", "--window", window, "space")
                assert viewer.wait(timeout=5) == 0
                palette.write_text("[Colors:Window]\nBackgroundNormal=17,83,149\n")
                for key in ("Escape", None):
                    window = open_viewer(first)
                    ready(window, first.name)
                    painted(window, 0x00FF00)
                    assert pixel_at(window, 5, height // 2) == 0x115395, "el nuevo visor no leyó el cambio de tema"
                    if key:
                        xdo("key", "--window", window, key)
                    else:
                        xdo("windowfocus", xlib.XDefaultRootWindow(display))
                    assert viewer.wait(timeout=5) == 0
                print("Imagen al 85 %, marco al 90 %, fondo de KDE, navegación y cierre: OK")
            except Exception:
                viewer_log.flush()
                server_log.flush()
                print("Código del visor:", None if viewer is None else viewer.poll())
                print("Registro del visor:", (work / "viewer.log").read_text()[-2000:])
                print("Registro de Xvfb:", (work / "xvfb.log").read_text()[-1000:])
                raise
            finally:
                if viewer is not None and viewer.poll() is None:
                    viewer.terminate()
                    viewer.wait(timeout=5)
                if display:
                    xlib.XCloseDisplay(display)
                server.terminate()
                server.wait(timeout=5)
                os.close(read_fd)


if __name__ == "__main__":
    main()
