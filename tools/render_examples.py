# SPDX-License-Identifier: GPL-3.0-only
"""Render the supplied slab examples with the illustrated top-view preset."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable",type=Path)
    parser.add_argument("--output",type=Path,default=Path("artifacts/illustrated-top"))
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[1]
    source=root/"tests/fixtures/slabs"
    preset=root/"presets/illustrated.json"
    collection=json.loads((source/"manifest.json").read_text())
    args.output.mkdir(parents=True,exist_ok=True)
    records=[]
    for item in collection["structures"]:
        path=source/item["file"]
        name=item["id"]+".png"
        cmd=[str(args.executable.resolve()),str(path),"--config",str(preset)]
        subprocess.run([*cmd,"-o",str(args.output/name)],check=True)
        scene=json.loads(subprocess.check_output([*cmd,"--inspect-scene"],text=True))
        records.append({"id":item["id"],"description":item["description"],"png":name,
                        "input_sha256":hashlib.sha256(path.read_bytes()).hexdigest(),
                        "png_sha256":hashlib.sha256((args.output/name).read_bytes()).hexdigest(),
                        "camera":scene["camera"],"atom_instances":scene["instance_count"],
                        "bond_segments":scene["bond_count"]})
    (args.output/"manifest.json").write_text(json.dumps({
        "preset":"presets/illustrated.json",
        "preset_sha256":hashlib.sha256(preset.read_bytes()).hexdigest(),
        "orientation":"Along a x b, sign toward c; a is aligned with image right. Exact orthographic top view.",
        "images":records},indent=2)+"\n")
    lines=["# Illustrated top views", "",
           "All 20 supplied CONTCAR samples, rendered at 768×768 with flat colored rims,",
           "pale circular highlights, black atom/bond contours, gray bonds, and a white background. The camera",
           "looks down the slab normal, with the first lattice vector pointing right.",
           "Deeper layers fade toward white for readability. Unit-cell boundaries are shown;",
           "all atoms remain in the scene and closer",
           "surfaces occlude deeper layers. Element colors/radii use the explicit",
           "illustrated preset; connectivity retains the Atom Architect bond rules.", "",
           "Source-labeled transition states and convergence have not been independently verified.", "",
           "Recreate from the repository root:", "", "```sh",
           "python3 tools/render_examples.py build-release/vasp-render", "```", "",
           "| Structure | Structure |", "| --- | --- |"]
    for offset in range(0,len(records),2):
        entries=[]
        for item in records[offset:offset+2]:
            entries.append(f"**{item['id']}** — {item['description']}<br>![{item['description']}]({item['png']})")
        lines.append("| "+" | ".join(entries)+" |")
    (args.output/"README.md").write_text("\n".join(lines)+"\n")
    print(f"Rendered {len(records)} top views to {args.output}")


if __name__=="__main__":
    main()
