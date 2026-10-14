#!/usr/bin/env python3
"""Create reproducible release archives without writing outside target/."""
import argparse
import gzip
import hashlib
import io
from pathlib import Path
import subprocess
import tarfile
import tempfile
import tomllib

ROOT = Path(__file__).resolve().parents[1]


def archive(destination, prefix, files):
    with destination.open("wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode="w") as output:
                for name, path, mode in sorted(files):
                    data = path.read_bytes()
                    info = tarfile.TarInfo(prefix + "/" + name)
                    info.size = len(data)
                    info.mode = mode
                    info.mtime = 0
                    output.addfile(info, io.BytesIO(data))
    print(destination.name, hashlib.sha256(destination.read_bytes()).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-only", action="store_true")
    parser.add_argument("--binary-only", action="store_true")
    args = parser.parse_args()
    if args.source_only and args.binary_only:
        parser.error("choose at most one archive type")
    version = tomllib.loads((ROOT / "Cargo.toml").read_text())["package"]["version"]
    output = ROOT / "target/releases"
    output.mkdir(parents=True, exist_ok=True)
    if not args.binary_only:
        entries = subprocess.check_output(["git", "ls-files", "--stage", "-z"], cwd=ROOT).decode().split("\0")
        files = []
        for entry in entries:
            if not entry:
                continue
            metadata, name = entry.split("\t", 1)
            # AUR recipes have the checksums of these archives and live in their own repositories.
            if name.startswith("packaging/aur/"):
                continue
            mode = 0o755 if metadata.split()[0] == "100755" else 0o644
            files.append((name, ROOT / name, mode))
        archive(output / f"quicklook-{version}-source.tar.gz", f"quicklook-{version}", files)
    if not args.source_only:
        with tempfile.TemporaryDirectory(prefix="release-stage-", dir=ROOT / "target") as directory:
            stage = Path(directory)
            subprocess.run(["make", "install-files", "install-softblur", "PREFIX=/usr", f"DESTDIR={stage}"],
                cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
            files = [(str(p.relative_to(stage)), p, p.stat().st_mode & 0o777)
                     for p in stage.rglob("*") if p.is_file()]
            archive(output / f"quicklook-{version}-linux-x86_64.tar.gz",
                f"quicklook-{version}-linux-x86_64", files)


if __name__ == "__main__":
    main()
