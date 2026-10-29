# SPDX-License-Identifier: GPL-3.0-only
"""Apply scoped lab targets and re-decode benchmark PNGs; not a shared-CI gate."""
import argparse
import hashlib
import json
from pathlib import Path
from benchmark import ROOT, decode_png, digest, summary
from compare_benchmarks import compare


def verify_images(report):
    for c in report['cases']:
        path=Path(c['png'])
        assert digest(path)==c['png_sha256'],c['name']+' PNG hash mismatch'
        w,h,p=decode_png(path)
        assert (w,h)==(c['width'],c['height']),c['name']+' dimensions mismatch'
        assert hashlib.sha256(p).hexdigest()==c['decoded_rgba_sha256'],c['name']+' decoded hash mismatch'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline',type=Path,required=True)
    parser.add_argument('--native',type=Path,required=True)
    parser.add_argument('--windows',type=Path,required=True)
    parser.add_argument('--annotated',type=Path,required=True)
    parser.add_argument('--targets',type=Path,default=ROOT/'docs/performance-targets.json')
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();targets=json.loads(args.targets.read_text())
    baseline=json.loads(args.baseline.read_text());verify_images(baseline)
    refs={c['name']:c for c in baseline['cases']}
    records=[]
    for name,path in [('native',args.native),('windows',args.windows),('annotated',args.annotated)]:
        report=json.loads(path.read_text());rules=targets[name]
        assert report.get('finished_utc') and len(report['cases'])==rules['expected_cases'],name+' incomplete'
        assert len({c['name'] for c in report['cases']})==len(report['cases']),name+' duplicate cases'
        assert targets['cpu_contains'] in report['environment']['cpu'],'targets apply to a different CPU'
        assert (report['work_filesystem']=='v9fs')==(name=='windows'),'unexpected benchmark filesystem'
        verify_images(report)
        if name!='annotated':assert compare(baseline,report)['accepted_lossless_comparison']
        for c in report['cases']:
            assert c['samples']>=targets['minimum_samples'] and len(c['elapsed_ms'])==c['samples']
            for stage,values in c['stages_ms'].items():assert len(values)==c['samples'],stage+' incomplete'
            measured=summary(c['elapsed_ms'])
            assert measured==c['latency_ms'],'summary differs from raw samples'
            assert c['render_counters']['render_threads']==8,'selected worker policy not exercised'
            assert c['render_counters']['png_compression_resolved']==rules['compression'],'wrong encoder policy'
            if name=='native':
                style=c['name'].split('-')[0]
                med_limit=rules[style+'_median_ms'];p95_limit=rules[style+'_p95_ms']
                assert c['rss_kib_separate_run'] and c['rss_kib_separate_run']<=rules['max_rss_kib'],'representative RSS target exceeded'
            else:med_limit=rules['median_ms'];p95_limit=rules['p95_ms']
            assert measured['median']<=med_limit,(name,c['name'],'median',measured['median'],med_limit)
            assert measured['p95']<=p95_limit,(name,c['name'],'p95',measured['p95'],p95_limit)
            if name=='annotated':
                ref=refs[c['name']]
                assert all(c[k]==ref[k] for k in ['input_sha256','instances','bonds'])
                overlays=c['settings']['overlays']
                assert all(overlays[k] for k in ['axes','legend','scale_bar']) and overlays['labels']=='element'
                assert 0<c['render_counters']['overlay_labels']<=c['instances']
                camera=c['camera'];bar=camera['scale_bar']
                assert abs(bar['pixels']-bar['angstrom']*camera['scale'])<=1e-8
                assert bar['label'].endswith('Å') and camera['viewport_px']['height']<c['height']
            records.append({'profile':name,'case':c['name'],'median_ms':measured['median'],
                            'p95_ms':measured['p95'],'median_limit_ms':med_limit,'p95_limit_ms':p95_limit,'passed':True})
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps({'passed':True,'targets':targets,'checks':records},indent=2)+'\n')
    print('Performance acceptance passed:',len(records),'cases')


if __name__=='__main__':main()
