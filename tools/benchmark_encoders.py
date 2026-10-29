# SPDX-License-Identifier: GPL-3.0-only
"""Compare encoders on identical RGBA buffers, outside the rendering pipeline."""
import argparse
import hashlib
import json
from pathlib import Path
import random
import shutil
import subprocess
import tempfile
from datetime import datetime, timezone
from benchmark import ROOT, decode_png, digest, environment, summary


def inputs():
    for name, path in [
        ("shaded-512", ROOT/"artifacts/m4-m6/reference/linux-baseline-images/shaded-Co-100.png"),
        ("illustrated-768", ROOT/"artifacts/m4-m6/reference/linux-baseline-images/illustrated-Pd-ceria-249.png"),
    ]:
        w,h,pixels=decode_png(path)
        yield name,w,h,pixels,{"source_png":str(path),"source_sha256":digest(path)}
    w=h=512
    alpha=bytearray()
    for y in range(h):
        for x in range(w):
            a=(3*x+5*y)%256
            alpha.extend((x%256,y%256,(x^y)%256,a) if a else (0,0,0,0))
    yield "alpha-512",w,h,bytes(alpha),{"generator":"RGB=(x,y,x^y)%256, A=(3*x+5*y)%256; canonical zero-alpha RGB"}
    rng=random.Random(20261029)
    noise=rng.randbytes(w*h*3);pixels=bytearray()
    for i in range(0,len(noise),3):
        pixels.extend(noise[i:i+3]);pixels.append(255)
    yield "noise-512",w,h,bytes(pixels),{"generator":"Python Random(20261029).randbytes RGB; A=255"}


def profiles(env):
    for level,filter_name in [(1,"adaptive"),(0,"none"),(1,"none"),(1,"sub"),(1,"up"),
                              (3,"adaptive"),(6,"adaptive"),(9,"adaptive")]:
        yield "libpng",level,filter_name
    if env["build_options"]["ENABLE_LIBDEFLATE"]=="ON":
        for level,filter_name in [(0,"none"),(1,"none"),(1,"sub"),(1,"up"),(1,"adaptive"),
                                  (3,"up"),(6,"up"),(9,"up")]:
            yield "libdeflate",level,filter_name
    if env["build_options"]["ENABLE_CAIRO_EXPERIMENTS"]=="ON":
        yield "cairo",1,"adaptive"  # API's default markers; Cairo controls its own codec.


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable",type=Path,help="CMake Release png-encode-bench executable")
    parser.add_argument("--output",type=Path,default=ROOT/"artifacts/m4-m6/m5/encoders/isolated.json")
    parser.add_argument("--work-dir",type=Path,default=Path("/tmp/qasr-bench-encoders"))
    parser.add_argument("--samples",type=int,default=30)
    args=parser.parse_args()
    if args.samples<2: parser.error("at least two samples required")
    exe=args.executable.resolve();work=args.work_dir.resolve();out=args.output.resolve()
    work.mkdir(parents=True,exist_ok=True);out.parent.mkdir(parents=True,exist_ok=True)
    images=out.parent/(out.stem+"-images");images.mkdir(exist_ok=True)
    env=environment(exe)
    report={"schema":1,"started_utc":datetime.now(timezone.utc).isoformat(),"environment":env,
            "measurement":"in-process encode/close/atomic rename; RGBA read before timing; fresh encoder per sample; no fsync",
            "filesystem":subprocess.check_output(["stat","-f","-c","%T",str(work)],text=True).strip(),
            "samples":args.samples,"cases":[]}
    def save():out.write_text(json.dumps(report,indent=2)+"\n")
    save()
    shutil.copyfile(Path(__file__),out.parent/(out.stem+"-driver.py"))
    for name,w,h,pixels,provenance in inputs():
        raw=work/(name+".rgba");raw.write_bytes(pixels)
        shutil.copyfile(raw,images/raw.name)
        expected=hashlib.sha256(pixels).hexdigest()
        for encoder,level,filter_name in profiles(env):
            profile="cairo-internal" if encoder=="cairo" else f"{encoder}-{level}-{filter_name}"
            png=work/f"{name}-{profile}.png"
            cmd=[str(exe),str(w),str(h),str(raw),str(png),encoder,str(level),filter_name,str(args.samples)]
            result=json.loads(subprocess.check_output(cmd,text=True,timeout=300))
            rss=None
            if Path("/usr/bin/time").exists():
                with tempfile.TemporaryDirectory(prefix="qasr-encoder-rss-") as tmp:
                    stat=Path(tmp)/"rss"
                    subprocess.run(["/usr/bin/time","-f","%M","-o",str(stat),*cmd[:-1],"2"],
                                   check=True,capture_output=True,timeout=300)
                    rss=int(stat.read_text())
            cw,ch,decoded=decode_png(png)
            if (cw,ch)!=(w,h): raise RuntimeError("encoder changed dimensions")
            exact=hashlib.sha256(decoded).hexdigest()==expected
            retained=images/png.name;shutil.copyfile(png,retained)
            record={"name":name,"width":w,"height":h,"raw_input":str(images/raw.name),
                    "rgba_sha256":expected,"provenance":provenance,"encoder":encoder,"profile":profile,
                    "compression":None if encoder=="cairo" else level,
                    "filter":"internal" if encoder=="cairo" else filter_name,
                    "command":cmd,**result,"summary_ms":summary(result["encode_ms"]),
                    "cpu_summary_ms":summary(result["cpu_ms"]),"lossless_rgba":exact,
                    "max_channel_error":max(abs(a-b) for a,b in zip(pixels,decoded)),
                    "alpha_identical":pixels[3::4]==decoded[3::4],
                    "png_bytes":png.stat().st_size,"png_sha256":digest(png),"png":str(retained),
                    "rss_kib_separate_run":rss}
            report["cases"].append(record);save()
            print(name,profile,f"{record['summary_ms']['median']:.3f} ms",f"lossless={exact}",flush=True)
    report["finished_utc"]=datetime.now(timezone.utc).isoformat();save()


if __name__=="__main__":
    main()
