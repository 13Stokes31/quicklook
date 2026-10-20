#!/usr/bin/env python3
"""Genera los seis formatos Office y prueba el lector real con un perfil privado."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run(command, env, timeout=60):
    process = subprocess.Popen(command, env=env, start_new_session=True)
    try:
        code = process.wait(timeout=timeout)
    except BaseException:
        if process.poll() is None:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
        raise
    if code:
        raise subprocess.CalledProcessError(code, command)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixtures-only", action="store_true",
        help="Guarda los seis documentos en target/office-fixtures sin abrir el lector")
    args = parser.parse_args()
    program = shutil.which("libreoffice") or shutil.which("soffice")
    if not program:
        parser.error("Se necesita LibreOffice para esta prueba opcional")
    checker = ROOT / "target/qt-build/check-documents"
    if not args.fixtures_only and not checker.is_file():
        parser.error("Compila primero las pruebas Qt con QUICKLOOK_BUILD_TESTS=ON")
    with tempfile.TemporaryDirectory(prefix="office-smoke-", dir=ROOT / "target") as directory:
        work = Path(directory)
        for name in ("config", "cache", "state", "runtime", "tmp", "converted"):
            (work / name).mkdir(mode=0o700)
        env = os.environ.copy()
        env.update(XDG_CONFIG_HOME=str(work / "config"), XDG_CACHE_HOME=str(work / "cache"),
            XDG_STATE_HOME=str(work / "state"), XDG_RUNTIME_DIR=str(work / "runtime"),
            TMPDIR=str(work / "tmp"), SAL_USE_VCLPLUGIN="svp", QT_QPA_PLATFORM="offscreen")
        for seed, formats in (("text.fodt", ("odt", "doc", "docx")), ("sheet.fods", ("ods", "xls", "xlsx"))):
            for format in formats:
                run([program, "-env:UserInstallation=" + (work / "profile").as_uri(),
                    "--headless", "--norestore", "--convert-to", format,
                    "--outdir", str(work / "converted"), str(ROOT / "tests/fixtures/office" / seed)], env)
                assert (work / "converted" / (Path(seed).stem + "." + format)).is_file(), format
        if args.fixtures_only:
            destination = ROOT / "target/office-fixtures"
            destination.mkdir(exist_ok=True)
            for path in (work / "converted").iterdir():
                shutil.copy2(path, destination / path.name)
            print("Documentos sintéticos:", destination, flush=True)
        else:
            files = list((work / "converted").iterdir())
            before = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
            env["QUICKLOOK_OFFICE_FIXTURES"] = str(work / "converted")
            run([str(checker), "officeRealFormats", "-platform", "offscreen"], env, timeout=450)
            assert before == {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
            print("Seis formatos Office en el lector real; originales conservados: OK", flush=True)


if __name__ == "__main__":
    main()
