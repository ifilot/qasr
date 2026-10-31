# SPDX-License-Identifier: GPL-3.0-only
"""Compare peak RSS for short/long streaming orbit and trajectory movies."""
import argparse
import hashlib
import json
from pathlib import Path
import math
import os
import statistics
import subprocess
import tempfile


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline',type=Path);parser.add_argument('candidate',type=Path)
    parser.add_argument('--output',type=Path,required=True);parser.add_argument('--samples',type=int,default=3)
    args=parser.parse_args()
    if args.samples<3:parser.error('Use at least three memory measurements per executable')
    binaries={'baseline':args.baseline.resolve(),'candidate':args.candidate.resolve()}
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    rows=[]
    with tempfile.TemporaryDirectory(prefix='qasr-movie-memory-') as tmp:
        work=Path(tmp);os.environ['XDG_CONFIG_HOME']=str(work/'preferences')
        for mode in ['orbit','trajectory']:
            for length in [16,1024]:
                source=work/'source.xyz'
                source.write_text(''.join(f'3\nSynthetic memory-control frame\nH {-2+.1*math.sin(i/15):.10f} 0 0\nH 0 0 0\nH 2 0 0\n' for i in range(length if mode=='trajectory' else 1)))
                video=work/'movie.mp4';report=work/'movie.json';rss=work/'rss.txt'
                expected=None;expected_report=None;measurements={};commands={}
                for label,exe in binaries.items():
                    command=[str(exe),str(source),'--no-user-config','--movie',mode,'--movie-codec','mpeg4','--width','128','--height','128','--aa','native','--cell','off','--threads','1','-o',str(video),'--report',str(report)]
                    if mode=='orbit':command+=['--movie-frames',str(length)]
                    commands[label]=command;values=[]
                    for _ in range(args.samples):
                        subprocess.run(['/usr/bin/time','-f','%M','-o',str(rss),*command],check=True,capture_output=True,timeout=180)
                        values.append(int(rss.read_text()))
                        digest=sha(video);data=json.loads(report.read_text());data.pop('timings_ms')
                        if expected is None:expected=digest;expected_report=data
                        assert digest==expected and data==expected_report,'Movie output/report changed'
                        assert data['movie']['encoded_frames']==length
                    measurements[label]={'peak_rss_KiB':values,'median_peak_rss_KiB':statistics.median(values)}
                media=json.loads(subprocess.check_output(['ffprobe','-v','error','-count_frames','-show_entries','stream=codec_name,width,height,nb_read_frames,duration','-of','json',video]))
                assert int(media['streams'][0]['nb_read_frames'])==length
                rows.append({'mode':mode,'frames':length,'input_sha256':sha(source),'encoded_sha256':expected,'commands':commands,'measurements':measurements,'media':media})
                print(mode,length,measurements,flush=True)
    for mode in ['orbit','trajectory']:
        short,long=[r for r in rows if r['mode']==mode]
        for label in binaries:
            assert long['measurements'][label]['median_peak_rss_KiB']<short['measurements'][label]['median_peak_rss_KiB']+16384,'Unexpected memory growth with movie length'
    result={'passed':True,'scope':'Three-or-more separate process peak-RSS samples; 128² native AA, one thread, native MPEG-4, 16/1024 output frames. Exact encoded bytes and reports checked across binaries; ffprobe checks actual frame counts.','samples':args.samples,'binaries':{k:{'path':str(v),'sha256':sha(v)} for k,v in binaries.items()},'cases':rows}
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(result,indent=2)+'\n')


if __name__=='__main__':main()
