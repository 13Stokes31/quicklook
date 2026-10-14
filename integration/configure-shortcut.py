#!/usr/bin/env python3
"""Asigna Espacio a Quick Look en Dolphin, conservando un respaldo local."""
import argparse
import configparser
import json
import os
from pathlib import Path
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
ACTION = "servicemenu_quicklook.desktop::quicklookPreview"


def configure(data_home, config_home, dry_run=False):
    menu = data_home / "kio/servicemenus/quicklook.desktop"
    data_dirs = os.environ.get("XDG_DATA_DIRS", "/usr/local/share:/usr/share").split(":")
    if not menu.is_file() and not any((Path(d) / "kio/servicemenus/quicklook.desktop").is_file()
                                     for d in data_dirs if d):
        raise ValueError("Instala primero el menú de Quick Look")
    destination = data_home / "kxmlgui5/dolphin/dolphinui.rc"
    original = destination.read_bytes() if destination.exists() else None
    if original is None:
        # KXmlGui migra la versión 0 a la interfaz incorporada de Dolphin,
        # conservando ActionProperties. No copiamos menús ni barras de herramientas.
        document = ET.Element("gui", {"name": "dolphin", "version": "0"})
    else:
        parser = ET.XMLParser(target=ET.TreeBuilder(insert_comments=True))
        document = ET.fromstring(original, parser=parser)
        if document.tag != "gui" or document.get("name") != "dolphin":
            raise ValueError("El XML local no corresponde a Dolphin")
    scheme = "Default"
    config = configparser.ConfigParser(interpolation=None, strict=False)
    config.optionxform = str
    config.read(config_home / "dolphinrc")
    if config.has_section("Shortcut Schemes"):
        scheme = config.get("Shortcut Schemes", "Current Scheme", fallback=scheme)
    properties = next((element for element in document.findall("ActionProperties")
                       if element.get("scheme", "Default") == scheme), None)
    if properties is None:
        properties = ET.SubElement(document, "ActionProperties", {"scheme": scheme})
    actions = {a.get("name"): a for a in properties.findall("Action")}
    for name, action in actions.items():
        shortcuts = [s.strip() for s in action.get("shortcut", "").split(";")]
        if "Space" in shortcuts and name not in (ACTION, "toggle_selection_mode"):
            raise ValueError(f"Espacio ya está asignado a {name}; no se cambia ese atajo")
    changes = {}
    for name, value in ((ACTION, "Space"), ("toggle_selection_mode", "")):
        action = actions.get(name)
        old = action.get("shortcut") if action is not None else None
        # Mantener un atajo personalizado distinto para el modo de selección.
        if name == "toggle_selection_mode" and old is not None:
            shortcuts = [s.strip() for s in old.split(";")]
            value = ";".join(s for s in shortcuts if s != "Space")
        if old == value:
            continue
        changes[name] = {"before": old, "after": value}
        if action is None:
            action = ET.SubElement(properties, "Action", {"name": name})
        action.set("shortcut", value)
    if dry_run or not changes:
        print(json.dumps({"changes": changes, "dry_run": dry_run}, ensure_ascii=False))
        return
    if (ROOT / "Cargo.toml").is_file():
        backup_root = ROOT / "target"
    else:
        backup_root = Path(os.environ.get("XDG_STATE_HOME", str(Path.home() / ".local/state"))) / "quicklook"
    backup_root.mkdir(parents=True, exist_ok=True)
    backup = Path(tempfile.mkdtemp(prefix="atajo-antes-", dir=backup_root))
    if original is not None:
        saved = backup / "dolphinui.rc"
        saved.write_bytes(original)
        saved.chmod(0o600)
    (backup / "estado.json").write_text(json.dumps({"destination": str(destination),
        "existed": original is not None, "scheme": scheme, "changes": changes}, indent=2) + "\n")
    payload = b'<?xml version="1.0" encoding="UTF-8"?>\n<!DOCTYPE gui SYSTEM "kpartgui.dtd">\n'
    payload += ET.tostring(document, encoding="utf-8") + b"\n"
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=destination.parent, delete=False) as output:
        temporary = Path(output.name)
        output.write(payload)
    try:
        temporary.chmod((destination.stat().st_mode & 0o777) if original is not None else 0o600)
        temporary.replace(destination)
    finally:
        if temporary.exists():
            temporary.unlink()
    print(f"Espacio → Quick Look. Respaldo: {backup}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-home", type=Path,
        default=Path(os.environ.get("XDG_DATA_HOME", str(Path.home() / ".local/share"))))
    parser.add_argument("--config-home", type=Path,
        default=Path(os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config"))))
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    try:
        configure(args.data_home, args.config_home, args.dry_run)
    except (ValueError, ET.ParseError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
