# SPDX-License-Identifier: GPL-3.0-only
"""Optional linking, decoded movie timing/motion, fixed framing and failure atomicity."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest

EXE=Path(sys.argv.pop(1)).resolve()
ROOT=Path(__file__).resolve().parents[1]
CAP=json.loads(subprocess.check_output([EXE,'--build-info']))
ENABLED=CAP['ffmpeg']
PROBE=shutil.which('ffprobe')
DECODE=shutil.which('ffmpeg')


def xyz(frames):
    return ''.join(f'2\nreaction coordinate\nO 0 0 0\nH {x} 0 0\n' for x in frames)


class MovieTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)
        self.source=self.root/'reaction.xyz';self.source.write_text(xyz([-1,2,5]))
        self.output=self.root/'movie.mp4';self.report=self.root/'report.json'
        self.base=[str(EXE),str(self.source),'--no-user-config','--width','128','--height','128','--aa','native','--cell','off','--view','0,0,1','--up','0,1,0']

    def call(self,*flags,code=0,env=None):
        p=subprocess.run([*self.base,*map(str,flags)],text=True,capture_output=True,timeout=45,env=env)
        self.assertEqual(p.returncode,code,p.stderr)
        return p

    def movie(self,mode='orbit',*flags,**kwargs):
        args=['--movie',mode,'--movie-fps','12','--report',self.report,'-o',self.output]
        if mode=='orbit':args+=['--movie-frames','8']
        self.call(*args,*flags,**kwargs)
        report=json.loads(self.report.read_text())
        self.assertEqual(report['input_sha256'],hashlib.sha256(self.source.read_bytes()).hexdigest())
        self.assertEqual(report['output_sha256'],hashlib.sha256(self.output.read_bytes()).hexdigest())
        self.assertGreater(report['timings_ms']['total'],0)
        self.assertEqual(self.output.read_bytes()[4:8],b'ftyp')
        return report

    def probe(self):
        if not PROBE:self.skipTest('ffprobe is optional for decoded validation')
        return json.loads(subprocess.check_output([PROBE,'-v','error','-count_frames','-show_streams','-show_frames','-of','json',self.output]))

    def pixels(self):
        if not DECODE:self.skipTest('ffmpeg CLI is optional for decoded validation')
        data=subprocess.check_output([DECODE,'-v','error','-i',self.output,'-f','rawvideo','-pix_fmt','rgb24','-threads','1','pipe:1'])
        stride=128*128*3
        self.assertEqual(len(data)%stride,0)
        return [data[i:i+stride] for i in range(0,len(data),stride)]

    def test_capability_and_disabled_error(self):
        if ENABLED:
            self.assertTrue(CAP['ffmpeg'])
        else:
            self.output.write_bytes(b'keep')
            p=self.call('--movie','orbit','-o',self.output,'--json-errors',code=3)
            self.assertEqual(json.loads(p.stderr)['error']['code'],'movie_unavailable')
            self.assertEqual(self.output.read_bytes(),b'keep')
            job={'input':str(self.source),'output':str(self.output),'movie':{'mode':'orbit'},'config':{'image':{'width':128,'height':128}}}
            p=subprocess.run([EXE,'--batch'],input=json.dumps({'id':'disabled','op':'render','job':job})+'\n',text=True,capture_output=True,check=True)
            self.assertFalse(json.loads(p.stdout)['ok'])
        if shutil.which('ldd'):
            libraries=subprocess.check_output(['ldd',EXE],text=True)
            for name in ['libavcodec.so','libavformat.so','libavutil.so','libswscale.so']:
                self.assertEqual(name in libraries,ENABLED)

    def test_mp4_requires_explicit_movie_mode(self):
        self.call('-o',self.output,code=2)
        self.assertFalse(self.output.exists())

    @unittest.skipUnless(ENABLED,'FFmpeg build only')
    def test_orbit_timestamps_frames_and_motion(self):
        report=self.movie('orbit','--movie-codec','mpeg4')
        self.assertEqual(report['movie']['encoded_frames'],8)
        self.assertFalse(report['camera']['fixed'])
        p=self.probe();s=p['streams'][0]
        self.assertEqual((s['width'],s['height']),(128,128))
        self.assertEqual(s['nb_read_frames'],'8');self.assertEqual(s['avg_frame_rate'],'12/1')
        self.assertAlmostEqual(float(s['duration']),8/12,places=5)
        times=[float(f['pts_time']) for f in p['frames']]
        self.assertEqual(times,sorted(times));self.assertEqual(len(set(times)),8)
        frames=self.pixels();self.assertNotEqual(frames[0],frames[2])

    @unittest.skipUnless(ENABLED,'FFmpeg build only')
    def test_streaming_needs_no_external_executable(self):
        report=self.movie('orbit','--movie-codec','mpeg4',env={**os.environ,'PATH':'/no/executables'})
        self.assertEqual(report['movie']['encoder'],'mpeg4')
        self.assertFalse(list(self.root.glob('*.png')))
        self.assertFalse(list(self.root.glob('*.tmp.*')))

    @unittest.skipUnless(ENABLED,'FFmpeg build only')
    def test_trajectory_fixed_camera_and_interpolation(self):
        config={'image':{'style':'illustrated','outline_width':0},'elements':{'O':{'color':'#ff0000','radius_A':.4},'H':{'color':'#0000ff','radius_A':.2}},'bond_cutoffs_A':{'H-O':.01}}
        cfg=self.root/'style.json';cfg.write_text(json.dumps(config))
        report=self.movie('trajectory','--movie-interpolate','1','--config',cfg)
        self.assertEqual(report['movie']['encoded_frames'],5);self.assertTrue(report['camera']['fixed'])
        frames=self.pixels();self.assertEqual(len(frames),5)
        red=[];blue=[];widths=[]
        for frame in frames:
            r=[];b=[]
            for i in range(128*128):
                rr,gg,bb=frame[3*i:3*i+3]
                if rr>130 and rr>gg*1.6 and rr>bb*1.6:r.append(i%128)
                if bb>110 and bb>gg*1.5 and bb>rr*1.5:b.append(i%128)
            self.assertGreater(len(r),10);self.assertGreater(len(b),4)
            red.append(sum(r)/len(r));blue.append(sum(b)/len(b));widths.append(max(r)-min(r))
        self.assertLess(max(red)-min(red),1)
        self.assertLessEqual(max(widths)-min(widths),1)
        self.assertEqual(blue,sorted(blue));self.assertGreater(blue[-1]-blue[0],40)

    @unittest.skipUnless(ENABLED,'FFmpeg build only')
    def test_outcar_and_xml_ionic_snapshots(self):
        self.source=ROOT/'tests/fixtures/OUTCAR.relax';self.base[1]=str(self.source)
        report=self.movie('trajectory','--movie-interpolate','2')
        self.assertEqual(report['movie']['encoded_frames'],4)
        if CAP['xml']:
            metadata='<atominfo><atoms>2</atoms><types>2</types><array name="atoms"><set><rc><c>Si</c><c>1</c></rc><rc><c>O</c><c>2</c></rc></set></array><array name="atomtypes"><set><rc><c>1</c><c>Si</c></rc><rc><c>1</c><c>O</c></rc></set></array></atominfo>'
            basis='<crystal><varray name="basis"><v>4 0 0</v><v>0 4 0</v><v>0 0 4</v></varray></crystal>'
            def snapshot(x, name=''):
                return f'<structure{name}>{basis}<varray name="positions"><v>0.5 0.5 0.5</v><v>{x} 0 0</v></varray></structure>'
            xml='<modeling>'+metadata+''.join('<calculation>'+snapshot(x)+'</calculation>' for x in [0,.2])+snapshot(.2,' name="finalpos"')+'</modeling>'
            self.source=self.root/'vasprun.xml';self.source.write_text(xml);self.base[1]=str(self.source)
            report=self.movie('trajectory');self.assertEqual(report['movie']['encoded_frames'],2)

    @unittest.skipUnless(ENABLED,'FFmpeg build only')
    def test_malformed_identity_and_options_preserve_destination(self):
        self.output.write_bytes(b'old movie')
        for flags in [ ['--width','127'],['--background','#ffffff00'],['--movie-axis','0,0,0'],['--movie-frames','1'],['--movie-fps','0'],['--movie-codec','bogus'],['--movie-interpolate','2'] ]:
            if flags[0]=='--width':
                args=[EXE,self.source,'--no-user-config','--movie','orbit','--height','128',*flags,'-o',self.output]
                p=subprocess.run(list(map(str,args)),capture_output=True);self.assertEqual(p.returncode,2)
            else:self.call('--movie','orbit',*flags,'-o',self.output,code=2)
            self.assertEqual(self.output.read_bytes(),b'old movie')
        self.call('--movie-fps','30','-o',self.output,code=2)
        self.call('--movie','trajectory','--frame','0','-o',self.output,code=2)
        self.call('--movie','trajectory','--movie-frames','10','-o',self.output,code=2)
        self.source.write_text(xyz([-1,2]).replace('H 2','C 2'))
        self.call('--movie','trajectory','-o',self.output,code=5)
        self.source.write_text(xyz([-1,2])+'2\nbroken\nO 0 0 0\n')
        self.call('--movie','trajectory','-o',self.output,code=4)
        self.assertEqual(self.output.read_bytes(),b'old movie');self.assertFalse(list(self.root.glob('*.tmp.*')))

    @unittest.skipUnless(ENABLED,'FFmpeg build only')
    def test_failed_commit_cleans_temporary(self):
        self.output.mkdir()
        p=self.call('--movie','orbit','--movie-frames','2','--json-errors','-o',self.output,code=7)
        self.assertEqual(json.loads(p.stderr)['error']['exit_code'],7)
        self.assertTrue(self.output.is_dir());self.assertFalse(list(self.root.glob('*.tmp.*')))

    @unittest.skipUnless(ENABLED,'FFmpeg build only')
    def test_json_job_and_worker(self):
        job={'input':self.source.name,'output':self.output.name,'movie':{'mode':'orbit','frames':3,'fps':9,'codec':'mpeg4'},'no_user_config':True,'config':{'image':{'width':128,'height':128,'aa':'native'},'overlays':{'cell':False}}}
        path=self.root/'movie-job.json';path.write_text(json.dumps(job))
        p=subprocess.run([EXE,'--job',path,'--report','-'],text=True,capture_output=True,check=True)
        self.assertEqual(json.loads(p.stdout)['movie']['encoded_frames'],3)
        p=subprocess.run([EXE,'--job',path,'--dump-config'],text=True,capture_output=True,check=True)
        self.assertEqual(json.loads(p.stdout)['runtime']['movie']['fps'],9)
        job['input']=str(self.source);job['output']=str(self.output)
        request={'id':'movie','op':'render','job':job}
        p=subprocess.run([EXE,'--batch'],input=json.dumps(request)+'\n',text=True,capture_output=True,check=True)
        response=json.loads(p.stdout);self.assertTrue(response['ok']);self.assertEqual(response['result']['movie']['encoded_frames'],3)

    @unittest.skipUnless(ENABLED and os.name=='posix','FFmpeg/POSIX only')
    def test_worker_cancellation_preserves_destination(self):
        self.output.write_bytes(b'keep movie')
        job={'input':str(self.source),'output':str(self.output),'movie':{'mode':'orbit','frames':100000,'codec':'mpeg4'},'config':{'image':{'width':256,'height':256,'aa':'native'}}}
        p=subprocess.Popen([EXE,'--batch'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        try:
            p.stdin.write(json.dumps({'id':'cancel','op':'render','job':job})+'\n');p.stdin.flush()
            for _ in range(400):
                if list(self.root.glob('*.tmp.*')):break
                if p.poll() is not None:self.fail('worker exited before encoding')
                time.sleep(.01)
            else:self.fail('movie encoding did not start')
            p.send_signal(signal.SIGINT)
            stdout,stderr=p.communicate(timeout=15)
            response=json.loads(stdout);self.assertFalse(response['ok'],stderr)
            self.assertEqual(self.output.read_bytes(),b'keep movie');self.assertFalse(list(self.root.glob('*.tmp.*')))
        finally:
            if p.poll() is None:p.kill();p.communicate()


if __name__=='__main__':unittest.main()
