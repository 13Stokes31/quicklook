#!/usr/bin/env python3
"""Planifica o activa el visor Qt y su efecto ya instalado. No obtiene privilegios."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
EFFECT_ID = 'quicklook_softblur_v2'
LEGACY_ID = 'quicklook_softblur'
NATIVE_MODULE = Path('/usr/lib/qt6/plugins/kwin/effects/plugins/quicklook_softblur_v2.so')
MODULE_SOURCE = ROOT / 'target/softblur-build/plugins/kwin/effects/plugins/quicklook_softblur_v2.so'
VIEWER_SOURCE = ROOT / 'target/qt-build/quicklook-qt'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def bus(path, *args):
    return subprocess.check_output(['qdbus6', 'org.kde.KWin', path, *args], text=True, timeout=10).strip()


def setting(config, value, effect=EFFECT_ID):
    args = ['kwriteconfig6', '--file', str(config), '--group', 'Plugins', '--key', effect + 'Enabled']
    args += ['--delete'] if value is None else ['--type', 'bool', value]
    subprocess.run(args, check=True)


def old_setting(config, effect=EFFECT_ID):
    group = ''
    if config.exists():
        for line in config.read_text().splitlines():
            if line.startswith('['): group = line.strip()
            elif group == '[Plugins]' and line.startswith(effect + 'Enabled='):
                return line.split('=', 1)[1].strip()
    return None


def unload(effect):
    if bus('/Effects', 'isEffectLoaded', effect) == 'true':
        bus('/Effects', 'unloadEffect', effect)
    deadline = time.monotonic() + 5
    while bus('/Effects', 'isEffectLoaded', effect) == 'true':
        if time.monotonic() > deadline: raise RuntimeError('No se pudo descargar el efecto anterior')
        time.sleep(0.05)


def reload_center(source):
    if bus('/Scripting', 'isScriptLoaded', 'quicklook-center') == 'true':
        bus('/Scripting', 'unloadScript', 'quicklook-center')
    deadline = time.monotonic() + 5
    while bus('/Scripting', 'isScriptLoaded', 'quicklook-center') == 'true':
        if time.monotonic() > deadline: raise RuntimeError('No se pudo descargar el centrado anterior')
        time.sleep(0.05)
    script_id = bus('/Scripting', 'loadScript', str(source), 'quicklook-center')
    if not script_id.isdigit(): raise RuntimeError('No se pudo cargar el complemento de centrado')
    bus('/Scripting/Script' + script_id, 'run')
    if bus('/Scripting', 'isScriptLoaded', 'quicklook-center') != 'true':
        raise RuntimeError('El complemento de centrado no se volvió a cargar')


def install(source, destination, mode):
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=destination.parent, delete=False) as output:
        temporary = Path(output.name)
        with source.open('rb') as input_file: shutil.copyfileobj(input_file, output)
    try:
        temporary.chmod(mode)
        temporary.replace(destination)
    finally:
        if temporary.exists(): temporary.unlink()


def targets(prefix, data):
    return [(VIEWER_SOURCE, prefix / 'bin/quicklook', 0o755),
        (ROOT / 'integration/kwin/metadata.json', data / 'kwin/scripts/quicklook-center/metadata.json', 0o644),
        (ROOT / 'integration/kwin/contents/code/main.js', data / 'kwin/scripts/quicklook-center/contents/code/main.js', 0o644)]


def known_qt_viewer(executable, opaque):
    """Reconocer también una interfaz Qt anterior, sin confundirla con el estable."""
    current = digest(executable)
    if current == digest(VIEWER_SOURCE): return True
    if not opaque.is_file(): return False
    stable = digest(opaque)
    for record in ROOT.glob('target/blur-antes-*/estado.json'):
        state = json.loads(record.read_text())
        files = state.get('files', [])
        if not files or files[0]['destination'] != str(executable) or state.get('opaque') != str(opaque):
            continue
        if files[0]['installed_sha256'] != current: continue
        # Compatibilidad con el respaldo creado antes de guardar la huella del estable.
        saved = record.parent / files[0]['backup']
        previous_stable = state.get('opaque_sha256')
        if previous_stable is None and saved.is_file(): previous_stable = digest(saved)
        if previous_stable == stable: return True
    return False


def activate(args, config, data):
    if not args.module.is_file() or digest(args.module) != digest(MODULE_SOURCE):
        raise RuntimeError('El módulo nativo no está instalado o no coincide con el compilado; no se activa nada')
    information = bus('/KWin', 'supportInformation')
    if 'KWin version: 6.7.5' not in information:
        raise RuntimeError('La sesión actual no usa KWin 6.7.5')
    executable = args.prefix / 'bin/quicklook'
    opaque = args.prefix / 'bin/quicklook-opaque'
    if not executable.is_file(): raise RuntimeError('Falta el visor estable instalado')
    entries = targets(args.prefix, data)
    already_installed = known_qt_viewer(executable, opaque)
    if already_installed and not opaque.exists():
        raise RuntimeError('La interfaz Qt está instalada pero falta su respaldo opaco; se conserva el estado actual')
    if not already_installed and opaque.exists() and digest(opaque) != digest(executable):
        raise RuntimeError('Ya existe un quicklook-opaque diferente; no se sobrescribe')
    ROOT.joinpath('target').mkdir(exist_ok=True)
    backup = Path(tempfile.mkdtemp(prefix='blur-antes-', dir=ROOT / 'target'))
    state = {'config': str(config), 'setting_before': old_setting(config), 'files': [], 'effect_id': EFFECT_ID,
        'module': str(args.module), 'effect_loaded_before': bus('/Effects', 'isEffectLoaded', EFFECT_ID) == 'true',
        'legacy_setting_before': old_setting(config, LEGACY_ID),
        'legacy_effect_loaded_before': bus('/Effects', 'isEffectLoaded', LEGACY_ID) == 'true',
        'opaque': str(opaque), 'opaque_existed': opaque.exists(),
        'opaque_sha256': digest(opaque) if opaque.exists() else digest(executable)}
    if args.prefix.resolve().is_relative_to(ROOT / 'target'):
        state['private_bus_sha256'] = hashlib.sha256(os.environ.get('DBUS_SESSION_BUS_ADDRESS', '').encode()).hexdigest()
    for number, (source, destination, mode) in enumerate(entries):
        entry = {'destination': str(destination), 'existed': destination.exists(),
            'installed_sha256': digest(source), 'backup': str(number)}
        if destination.exists():
            entry['mode'] = destination.stat().st_mode & 0o777
            saved = backup / str(number)
            saved.write_bytes(destination.read_bytes()); saved.chmod(0o600)
        state['files'].append(entry)
    (backup / 'estado.json').write_text(json.dumps(state, indent=2) + '\n')
    try:
        unload(LEGACY_ID)
        if not state['effect_loaded_before']:
            if bus('/Effects', 'loadEffect', EFFECT_ID) != 'true':
                raise RuntimeError('KWin no pudo cargar el efecto')
        if bus('/QuickLookSoftBlur', 'ready') != 'true' or bus('/QuickLookSoftBlur', 'protocolVersion') != '2':
            raise RuntimeError('El efecto no está listo o su protocolo no es compatible')
        if not opaque.exists(): install(executable, opaque, 0o755)
        for source, destination, mode in entries: install(source, destination, mode)
        setting(config, 'true')
        setting(config, None, LEGACY_ID)
        # Volver a cargar el script para que reconozca ambos identificadores.
        reload_center(entries[2][1])
        print(f'Visor con blur activado. Estable: {opaque}. Respaldo: {backup}')
    except Exception:
        restore(backup, strict=False)
        raise


def restore(backup, strict=True):
    state = json.loads((backup / 'estado.json').read_text())
    if state.get('private_bus_sha256') and state['private_bus_sha256'] != hashlib.sha256(
            os.environ.get('DBUS_SESSION_BUS_ADDRESS', '').encode()).hexdigest():
        raise RuntimeError('El respaldo pertenece a una sesión privada diferente; no se modifica la sesión actual')
    # No restaurar todo kwinrc: conservar cualquier cambio ajeno posterior.
    for entry in state['files']:
        destination = Path(entry['destination'])
        if strict and destination.exists() and digest(destination) != entry['installed_sha256']:
            raise RuntimeError(f'Ha cambiado {destination}; se conserva y no se restaura automáticamente')
    for entry in state['files']:
        destination = Path(entry['destination'])
        if entry['existed']:
            install(backup / entry['backup'], destination, entry['mode'])
        elif destination.exists(): destination.unlink()
    effect = state.get('effect_id', LEGACY_ID)
    config = Path(state['config'])
    setting(config, state['setting_before'], effect)
    if not state['effect_loaded_before']:
        unload(effect)
    if 'legacy_setting_before' in state:
        setting(config, state['legacy_setting_before'], LEGACY_ID)
        if state['legacy_effect_loaded_before'] and bus('/Effects', 'isEffectLoaded', LEGACY_ID) != 'true':
            bus('/Effects', 'loadEffect', LEGACY_ID)
    reload_center(Path(state['files'][2]['destination']))
    if state['effect_loaded_before'] and bus('/Effects', 'isEffectLoaded', effect) != 'true':
        bus('/Effects', 'loadEffect', effect)
    print('Visor anterior y centrado restaurados; se conserva el estado previo de los efectos propios')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--activate', action='store_true', help='aplicar el plan después de autorizarlo')
    parser.add_argument('--restore', type=Path, help='restaurar un respaldo creado por este programa')
    parser.add_argument('--prefix', type=Path, default=Path.home() / '.local')
    parser.add_argument('--module', type=Path, default=NATIVE_MODULE)
    args = parser.parse_args()
    data = Path(os.environ.get('XDG_DATA_HOME', str(Path.home() / '.local/share')))
    config = Path(os.environ.get('XDG_CONFIG_HOME', str(Path.home() / '.config'))) / 'kwinrc'
    try:
        if args.restore:
            restore(args.restore)
        elif args.activate:
            activate(args, config, data)
        else:
            for source in (VIEWER_SOURCE, MODULE_SOURCE):
                if not source.is_file(): raise RuntimeError('Compila primero con make build-blur')
            print(json.dumps({'requires_system_install': not args.module.is_file()
                    or digest(args.module) != digest(MODULE_SOURCE),
                'native_module': {'source': str(MODULE_SOURCE), 'destination': str(args.module), 'sha256': digest(MODULE_SOURCE)},
                'user_files': [str(destination) for _, destination, _ in targets(args.prefix, data)],
                'viewer_sha256': digest(VIEWER_SOURCE),
                'stable_copy': str(args.prefix / 'bin/quicklook-opaque'),
                'setting': 'Plugins/' + EFFECT_ID + 'Enabled=true', 'config_file': str(config),
                'legacy_effect': 'desactivar solo ' + LEGACY_ID + '; conservar su archivo para revertir',
                'changes_global_blur_strength': False}, indent=2))
    except (RuntimeError, subprocess.SubprocessError, OSError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__': main()
