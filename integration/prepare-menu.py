#!/usr/bin/env python3
"""Genera el menú con el binario absoluto del prefijo, sin depender del PATH."""

from pathlib import Path
import sys


def desktop_string(value):
    return value.replace("\\", "\\\\").replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t")


def prepare_menu(binary):
    if not Path(binary).is_absolute() or "=" in binary:
        raise ValueError("el binario debe tener una ruta absoluta sin el carácter '='")
    # Exec tiene dos niveles de escapes: argumento entre comillas y valor .desktop.
    quoted = "".join("\\" + char if char in '\\"`$' else char for char in binary)
    command = desktop_string('"' + quoted.replace("%", "%%") + '"') + " %f"
    template = Path(__file__).with_name("quicklook.desktop").read_text(encoding="utf-8")
    return template.replace("Exec=quicklook %f", "Exec=" + command).replace(
        "TryExec=quicklook", "TryExec=" + desktop_string(binary)
    )


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("uso: prepare-menu.py /ruta/absoluta/al/binario/quicklook")
    sys.stdout.write(prepare_menu(sys.argv[1]))
