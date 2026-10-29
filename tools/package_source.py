# SPDX-License-Identifier: GPL-3.0-only
"""Create the vasp-render source archive from an explicit release allowlist."""
import argparse
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import re
import tarfile


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output",type=Path,default=Path("artifacts/releases"))
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[1]
    cmake=(root/"CMakeLists.txt").read_text()
    version=re.search(r"project\(vasp_render VERSION ([0-9]+\.[0-9]+\.[0-9]+)",cmake).group(1)
    top=[".gitignore",".gitattributes","CMakeLists.txt","README.md","CHANGELOG.md","LICENSE","THIRD_PARTY_NOTICES.md"]
    files=[root/name for name in top]
    for directory in ["src","presets","docs","tests","tools","vendor"]:
        for path in (root/directory).rglob("*"):
            if "__pycache__" in path.parts or path.suffix in [".pyc",".pyo"]:
                continue
            if path.is_symlink():
                raise ValueError(f"release source must not contain symlinks: {path}")
            if path.is_file():
                files.append(path)
    epoch=int(os.environ.get("SOURCE_DATE_EPOCH","0"))
    args.output.mkdir(parents=True,exist_ok=True)
    destination=args.output/f"vasp-render-{version}.tar.gz"
    hashes={}
    with destination.open("wb") as output:
        with gzip.GzipFile(filename="",fileobj=output,mode="wb",mtime=epoch) as compressed:
            with tarfile.open(fileobj=compressed,mode="w|",format=tarfile.PAX_FORMAT) as archive:
                for path in sorted(files):
                    relative=path.relative_to(root).as_posix()
                    data=path.read_bytes()
                    info=tarfile.TarInfo(f"vasp-render-{version}/{relative}")
                    info.size=len(data);info.mtime=epoch;info.mode=0o644
                    archive.addfile(info,io.BytesIO(data))
                    hashes[relative]=hashlib.sha256(data).hexdigest()
    digest=hashlib.sha256(destination.read_bytes()).hexdigest()
    destination.with_name(destination.name+".sha256").write_text(f"{digest}  {destination.name}\n")
    (args.output/f"vasp-render-{version}-source-manifest.json").write_text(json.dumps({
        "version":version,"archive":destination.name,"sha256":digest,"files":hashes},indent=2)+"\n")
    print(f"{destination}: {len(files)} files, SHA-256 {digest}")


if __name__=="__main__":
    main()
