# SPDX-License-Identifier: GPL-3.0-only
"""Sequential worker protocol, bounded reuse, cancellation and CLI equivalence."""
import contextlib
import io
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

EXE=Path(sys.argv.pop(1)).resolve();ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from generate_m10_workloads import generate,geometry,poscar
from run_m10_workloads import run_suite


class WorkerTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.root=Path(self.tmp.name)
        self.job={'input':str(ROOT/'tests/fixtures/POSCAR.skew'),'output':str(self.root/'out.png'),'config':{'image':{'width':64,'height':64}}}

    def run_lines(self,lines,*options):
        p=subprocess.run([str(EXE),'--batch',*options],input=lines,text=True,capture_output=True,timeout=60)
        self.assertEqual(p.returncode,0,p.stderr)
        return [json.loads(line) for line in p.stdout.splitlines()]

    def request(self,identifier='a',op='render',job=None):
        return {'id':identifier,'op':op,**({'job':job or self.job} if op in ['render','validate'] else {})}

    def test_framing_errors_continue_and_no_overwrite(self):
        valid=json.dumps(self.request())
        results=self.run_lines('not json\n'+json.dumps({'id':'bad','op':'render','job':{'bogus':1}})+'\n'+valid+'\n'+json.dumps(self.request('stats','stats')))
        self.assertEqual([r['ok'] for r in results],[False,False,True,True]);self.assertIsNone(results[0]['id'])
        for bad in ['x'*1048577,'{"id":"x","id":"y","op":"stats"}','{\x00}']:
            results=self.run_lines(bad+'\n'+json.dumps(self.request('ok','stats'))+'\n')
            self.assertEqual([r['ok'] for r in results],[False,True])

    def test_hits_camera_reuse_geometry_and_style_invalidation(self):
        requests=[self.request(str(i)) for i in range(5)]
        requests[2]['job']=json.loads(json.dumps(self.job));requests[2]['job']['config']['camera']={'view':'top'}
        requests[3]['job']=json.loads(json.dumps(self.job));requests[3]['job']['config']['atom_overrides']=[{'select':{'element':'O'},'color':'#ff0000'}]
        requests[4]['job']=json.loads(json.dumps(self.job));requests[4]['job']['config']['geometry']={'expand_xy':[1,1]}
        results=self.run_lines(''.join(json.dumps(v)+'\n' for v in requests))
        self.assertTrue(all(r['ok'] for r in results))
        self.assertEqual([r['result']['cache']['source_hit'] for r in results],[False,True,True,True,True])
        self.assertEqual([r['result']['cache']['scene_hit'] for r in results],[False,True,True,False,False])

    def test_disabled_cache_clear_and_validation(self):
        lines=''.join(json.dumps(v)+'\n' for v in [self.request('1'),self.request('2'),self.request('clear','clear_cache'),self.request('3'),self.request('validate','validate'),self.request('stats','stats')])
        results=self.run_lines(lines,'--cache-mib','0')
        self.assertTrue(all(r['ok'] for r in results));self.assertEqual(results[-1]['result']['retained_bytes'],0)
        results=self.run_lines(lines)
        self.assertFalse(results[3]['result']['cache']['source_hit']);self.assertEqual(results[-1]['result']['retained_bytes'],0)

    def test_all_synthetic_families_match_cli_reference(self):
        suite=self.root/'data';generate(suite,'smoke')
        with contextlib.redirect_stdout(io.StringIO()):
            reference=run_suite(EXE,suite,self.root/'cli',samples=1,warmups=0)
            worker=run_suite(EXE,suite,self.root/'worker',samples=2,warmups=0,backend='worker',reference=self.root/'cli/report.json')
        self.assertTrue(reference['passed'] and worker['passed'])
        self.assertTrue(any(v['cache'] and v['cache']['scene_hit'] for v in worker['results']))
        self.assertTrue(all(not r['cache'] or r['cache']['retained_bytes']<=r['cache']['limit_bytes'] for r in worker['results']))

    def test_cache_capacity_and_long_lived_bound(self):
        source=self.root/'big';source.write_text(poscar(geometry(2000,'bulk',17)))
        job=json.loads(json.dumps(self.job));job['input']=str(source)
        requests=[self.request('big'+str(i),job=job) for i in range(2)]
        results=self.run_lines(''.join(json.dumps(v)+'\n' for v in requests),'--cache-mib','1')
        self.assertTrue(all(r['ok'] for r in results))
        self.assertTrue(results[1]['result']['cache']['source_hit']);self.assertFalse(results[1]['result']['cache']['scene_hit'])
        requests=[self.request(str(i)) for i in range(100)]+[self.request('stats','stats')]
        results=self.run_lines(''.join(json.dumps(v)+'\n' for v in requests),'--cache-mib','1')
        self.assertTrue(all(r['ok'] for r in results));self.assertLessEqual(results[-1]['result']['retained_bytes'],1048576)

    def test_limits_schema_and_host_defaults(self):
        lines=''.join(json.dumps(self.request(str(i),'stats'))+'\n' for i in range(3))
        self.assertEqual(len(self.run_lines(lines,'--max-jobs','2')),2)
        for args in [['--cache-mib','513'],['--cache-mib','1','--cache-mib','1'],['--max-jobs','0']]:
            p=subprocess.run([str(EXE),'--batch',*args],capture_output=True);self.assertEqual(p.returncode,2)
        self.assertEqual(json.loads(subprocess.check_output([str(EXE),'--schema','worker'])),json.loads((ROOT/'schemas/worker.schema.json').read_text()))
        config=self.root/'prefs/qasr';config.mkdir(parents=True);(config/'config.json').write_text('malformed')
        p=subprocess.run([str(EXE),'--batch'],input=json.dumps(self.request())+'\n',env=dict(os.environ,XDG_CONFIG_HOME=str(config.parent)),text=True,capture_output=True)
        self.assertTrue(json.loads(p.stdout)['ok'])
        broken=subprocess.Popen([str(EXE),'--batch'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        try:
            broken.stdout.close();broken.stdin.write(json.dumps(self.request('pipe','stats'))+'\n');broken.stdin.flush();broken.stdin.close()
            broken.wait(timeout=30);self.assertEqual(broken.returncode,7,broken.stderr.read())
        finally:
            if broken.poll() is None:broken.kill();broken.wait()
            for stream in [broken.stdin,broken.stdout,broken.stderr]:stream.close()

    def test_parallel_setup_preserves_alpha_and_adaptive_pixels(self):
        capabilities=json.loads(subprocess.check_output([str(EXE),'--build-info']))
        if not capabilities['openmp']:self.skipTest('OpenMP build required')
        for style in ['shaded','illustrated']:
            for aa in ['4x','adaptive']:
                requests=[]
                for threads in [1,8]:
                    job=json.loads(json.dumps(self.job));job['threads']=threads
                    job['config']['image'].update(width=512,height=512,background='#ffffff00',aa=aa,style=style,png_encoder='libpng')
                    job['config']['overlays']={'legend':True,'labels':'element'}
                    requests.append(self.request(str(threads),job=job))
                results=self.run_lines(''.join(json.dumps(v)+'\n' for v in requests))
                self.assertTrue(all(r['ok'] for r in results))
                self.assertEqual(results[0]['result']['output']['sha256'],results[1]['result']['output']['sha256'])
                self.assertEqual(results[0]['result']['render_threads'],1)
                self.assertGreaterEqual(results[1]['result']['render_threads'],1)
                self.assertLessEqual(results[1]['result']['render_threads'],8)

    def test_sigint_cancels_active_job_and_worker_recovers(self):
        source=self.root/'slow';source.write_text(poscar(geometry(5000,'bulk',19)))
        job=json.loads(json.dumps(self.job));job['input']=str(source)
        output=Path(job['output']);output.write_bytes(b'preserve')
        p=subprocess.Popen([str(EXE),'--batch'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        try:
            # A handshake makes process startup independent of the cancellation test.
            p.stdin.write(json.dumps(self.request('hello','stats'))+'\n');p.stdin.flush();self.assertTrue(json.loads(p.stdout.readline())['ok'])
            p.stdin.write(json.dumps(self.request('slow',job=job))+'\n');p.stdin.flush();time.sleep(.05);p.send_signal(signal.SIGINT)
            reply=json.loads(p.stdout.readline());self.assertFalse(reply['ok']);self.assertEqual(reply['error']['code'],'cancelled')
            self.assertEqual(output.read_bytes(),b'preserve')
            p.stdin.write(json.dumps(self.request('after'))+'\n');p.stdin.flush();self.assertTrue(json.loads(p.stdout.readline())['ok'])
            p.stdin.close();p.wait(timeout=30);self.assertEqual(p.returncode,0,p.stderr.read());self.assertFalse(list(self.root.glob('*.tmp.*')))
        finally:
            if p.poll() is None:p.kill();p.wait()
            for stream in [p.stdin,p.stdout,p.stderr]:stream.close()


if __name__=='__main__':unittest.main()
