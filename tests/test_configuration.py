# SPDX-License-Identifier: GPL-3.0-only
"""Layering, source/cell styling and machine-contract acceptance tests."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

EXE = Path(sys.argv.pop(1)).resolve()
ROOT = Path(__file__).resolve().parents[1]
INPUT = ROOT/'tests/fixtures/POSCAR.skew'


class ConfigurationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.env = dict(os.environ, HOME=str(self.root/'home'), XDG_CONFIG_HOME=str(self.root/'xdg'))
        self.user = self.root/'xdg/qasr/config.json'
        self.cfg = self.root/'config.json'

    def write(self, value, path=None):
        path = path or self.cfg
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value))
        return path

    def call(self, *args, ok=True, stdin=None):
        p = subprocess.run([str(EXE), *map(str,args)], input=stdin, text=True, capture_output=True, env=self.env, cwd=self.root)
        if ok: self.assertEqual(p.returncode, 0, p.stderr)
        else: self.assertNotEqual(p.returncode, 0, p.stdout)
        return p

    def dump(self, *args):
        return json.loads(self.call('--dump-config', *args).stdout)

    def scene(self, config, *args):
        return json.loads(self.call(INPUT, '--config', self.write(config), '--inspect-scene', *args).stdout)

    def error(self, config, code=None, path=None, *args):
        p = self.call('--config', self.write(config), '--validate-config', '--json-errors', *args, ok=False)
        self.assertEqual(p.stdout, '')
        result = json.loads(p.stderr)['error']
        self.assertEqual(result['exit_code'], p.returncode)
        if code: self.assertEqual(result['code'], code)
        if path: self.assertEqual(result['path'], path)
        return result

    def test_discovery_merge_and_provenance(self):
        self.write({'image':{'width':200,'height':210}, 'elements':{'O':{'radius_A':0.7,'color':'#112233'}}, 'bond_cutoffs_A':{'Si-O':2}}, self.user)
        self.write({'image':{'width':220}, 'elements':{'O':{'color':'#334455'}}, 'bond_cutoffs_A':{'O-Si':2.2}})
        d = self.dump('--config',self.cfg,'--width','230')
        self.assertEqual(d['config']['image']['width'],230)
        self.assertEqual(d['config']['image']['height'],210)
        self.assertEqual(d['config']['elements']['O'],{'radius_A':0.7,'color':'#334455'})
        self.assertEqual(d['config']['bond_cutoffs_A'],{'O-Si':2.2})
        self.assertEqual(d['provenance']['image.width']['layer'],'cli')
        self.assertEqual(d['provenance']['image.height']['layer'],'user')
        self.assertEqual(d['provenance']['elements.O.color']['layer'],'config')
        self.assertTrue(d['user_config']['loaded'])

    def test_home_fallback_and_no_cwd_search(self):
        self.env['XDG_CONFIG_HOME']='relative'
        home = self.root/'home/.config/qasr/config.json'
        self.write({'image':{'width':201}},home)
        self.write({'image':{'width':202}},self.root/'config.json')
        self.assertEqual(self.dump()['config']['image']['width'],201)
        self.env['HOME']='relative'
        self.assertEqual(self.dump()['config']['image']['width'],512)

    def test_bad_user_file_and_opt_out(self):
        self.user.parent.mkdir(parents=True)
        self.user.write_text('{"image":')
        self.call('--dump-config',ok=False)
        self.assertEqual(self.dump('--no-user-config')['config']['image']['width'],512)

    def test_initialize_never_overwrites(self):
        result=json.loads(self.call('--init-config').stdout)
        self.assertEqual(result['path'],str(self.user))
        before=self.user.read_bytes()
        self.call('--init-config','--json-errors',ok=False)
        self.assertEqual(self.user.read_bytes(),before)
        self.user.unlink(); self.user.symlink_to(self.write({'image':{'width':128}}))
        self.call('--init-config',ok=False)
        self.assertTrue(self.user.is_symlink())

    def test_user_atom_rules_forbidden(self):
        self.write({'atom_overrides':[{'select':{'element':'O'},'color':'#ff0000'}]},self.user)
        p=self.call('--dump-config','--json-errors',ok=False)
        self.assertEqual(json.loads(p.stderr)['error']['code'],'user_atom_rules_forbidden')
        self.write({'atom_overrides':[]},self.user)
        self.dump()

    def test_dump_reload_centered_and_top(self):
        d=self.dump('--expand-xy','1,2','--view','top')
        self.assertEqual(d['config']['geometry']['repeat'],[1,1,1])
        self.assertEqual(d['derived']['repeat'],[3,5,1])
        self.assertNotIn('up',d['config']['camera'])
        second=self.dump('--config',self.write(d['config']))
        self.assertEqual(second['config'],d['config'])

    def test_duplicate_and_unknown_fields(self):
        self.error({'image':{'widht':22}},path='image.widht')
        self.error({'bond_cutoffs_A':{'Si-O':2,'O-Si':3}})
        self.cfg.write_text('{"image":{"width":100,"width":200}}')
        self.call('--config',self.cfg,'--validate-config',ok=False)

    def test_validation_without_input_is_explicit(self):
        rules={'atom_overrides':[{'select':{'source_indices':[999]},'color':'#ff0000'}]}
        p=self.call('--config',self.write(rules),'--validate-config')
        self.assertFalse(json.loads(p.stdout)['selectors_validated'])
        self.error(rules,'invalid_atom_index',None,INPUT)

    def test_source_rules_and_per_property_precedence(self):
        cfg={'geometry':{'sphere_scale':2},'atom_overrides':[
            {'select':{'element':'Si'},'color':'#aabbcc80','radius_A':0.4},
            {'select':{'source_range':[0,0]},'color':'#11223380'}]}
        s=self.scene(cfg)
        a=[p for p in s['primitives'] if p['kind']=='sphere'][0]
        self.assertEqual(a['rgba'],[17,34,51,128]); self.assertAlmostEqual(a['radius'],0.8)
        self.assertEqual(a['applied_rules'],{'color':1,'radius':0})

    def test_context_then_cell_override_even_when_listed_first(self):
        cfg={'geometry':{'expand_xy':[1,1],'image_colors':'greyscale'},'atom_overrides':[
            {'select':{'source_indices':[0],'cell':[-1,0,0]},'color':'#00ff0080','radius_A':0.9},
            {'select':{'source_indices':[0]},'color':'#ff000080','radius_A':0.4}]}
        spheres=[p for p in self.scene(cfg)['primitives'] if p['kind']=='sphere' and p['source'][0]==0]
        bycell={tuple(p['origin']):p for p in spheres}
        self.assertEqual(bycell[(0,0,0)]['rgba'],[255,0,0,128])
        self.assertEqual(bycell[(-1,0,0)]['rgba'],[0,255,0,128])
        self.assertEqual(bycell[(-1,0,0)]['applied_rules'],{'color':0,'radius':0})
        self.assertEqual(len(set(bycell[(1,0,0)]['rgba'][:3])),1)
        self.assertAlmostEqual(bycell[(1,0,0)]['radius'],0.4)

    def test_cell_rule_chain_and_arrays_replace(self):
        cfg={'geometry':{'repeat':[2,1,1]},'atom_overrides':[
            {'select':{'element':'O','cell':[0,0,0]},'color':'#010203'},
            {'select':{'element':'Si'},'color':'#ff0000'},
            {'select':{'element':'O','cell':[1,0,0]},'color':'#040506'}]}
        atoms=[p for p in self.scene(cfg)['primitives'] if p['kind']=='sphere' and p['element']=='O']
        self.assertEqual([p['rgba'][:3] for p in atoms],[[1,2,3],[4,5,6]])
        self.write({'species':['Si','O']},self.user)
        self.assertEqual(self.dump('--config',self.write({'species':['O']}))['config']['species'],['O'])

    def test_bonds_unchanged(self):
        plain=self.scene({}); styled=self.scene({'atom_overrides':[{'select':{'element':'O'},'radius_A':0.8,'color':'#ff0000'}]})
        bonds=lambda s:[p for p in s['primitives'] if p['kind']=='bond']
        self.assertEqual(bonds(plain),bonds(styled))

    def test_invalid_selectors_and_paths(self):
        cases=[({'source_indices':[-1]},'invalid_atom_index','select.source_indices[0]'),
               ({'source_indices':[0,0]},'duplicate_atom_index','select.source_indices'),
               ({'element':'si'},'invalid_atom_element','select.element'),
               ({'source_range':[1,0]},'invalid_atom_range','select.source_range'),
               ({'cell':[0,0,0]},'empty_atom_selector','select'),
               ({'source_range':[0,1],'source_indices':[0]},'conflicting_atom_selector','select'),
               ({'element':'C'},'empty_atom_selection','select'),
               ({'source_indices':[0],'cell':[-1,0,0]},'atom_cell_out_of_range','select.cell')]
        for selector,code,path in cases:
            with self.subTest(selector=selector):
                self.error({'atom_overrides':[{'select':selector,'color':'#ff0000'}]},code,'atom_overrides[0].'+path,INPUT)

    def test_rules_and_json_size_limits(self):
        rule={'select':{'element':'O'},'color':'#ff0000'}
        self.error({'atom_overrides':[rule]*4097},'atom_rule_limit')
        self.cfg.write_text(' '*1048577)
        self.call('--config',self.cfg,'--validate-config',ok=False)

    def test_cell_work_limit_fails_before_expansion(self):
        rule={'select':{'element':'Si','cell':[0,0,0]},'color':'#ff0000'}
        self.error({'geometry':{'expand_xy':[255,255]},'atom_overrides':[rule]*4096},'atom_rule_work_limit','atom_overrides',INPUT)

    def test_malformed_jobs_and_cli_json_errors(self):
        self.cfg.write_text('{"input":')
        p=self.call('--job',self.cfg,'--json-errors',ok=False)
        self.assertEqual(json.loads(p.stderr)['error']['code'],'invalid_json')
        for value in [{'unknown':1},{'threads':0},{'output':'-'},{'config':{'image':{'width':0}}}]:
            p=self.call('--job',self.write(value),'--validate-config','--json-errors',ok=False)
            self.assertFalse(json.loads(p.stderr)['ok'])

    def test_schemas_match_files(self):
        for kind in ['config','user','job']:
            schema=json.loads(self.call('--schema',kind).stdout)
            self.assertEqual(schema,json.loads((ROOT/'schemas'/f'{kind}.schema.json').read_text()))
            self.assertFalse(schema['additionalProperties'])
        self.assertEqual(json.loads(self.call('--schema').stdout),json.loads((ROOT/'schemas/config.schema.json').read_text()))

    def test_job_relative_paths_cli_precedence_and_stdin(self):
        directory=self.root/'jobs'; directory.mkdir()
        (directory/'POSCAR').write_bytes(INPUT.read_bytes())
        job=self.write({'schema_version':1,'input':'POSCAR','output':'a.png','report':'a.json','no_user_config':True,'threads':1,'config':{'image':{'width':64,'height':64,'aa':'native'}}},directory/'job.json')
        self.call('--job',job,'--width','80')
        report=json.loads((directory/'a.json').read_text())
        self.assertEqual(report['output']['width'],80)
        validation=json.loads(self.call('--job',job,'--validate-config').stdout)
        self.assertTrue(validation['selectors_validated'])
        d=self.dump('--job',job,INPUT,'--threads','auto')
        self.assertEqual(d['runtime']['input'],str(INPUT));self.assertEqual(d['runtime']['threads'],'auto')
        self.call('--job',job,'--config',self.write({}),'--dump-config',ok=False)
        stdin={'input':str(INPUT),'output':'stdin.png','config':{'image':{'width':32,'height':32,'aa':'native'}}}
        self.call('--job','-',stdin=json.dumps(stdin))
        self.assertTrue((self.root/'stdin.png').is_file())
        self.call('--config','-','--validate-config',stdin='{"image":{"width":32}}')

    def test_report_hashes_and_compactness(self):
        out=self.root/'out.png'
        p=self.call(INPUT,'-o',out,'--width','64','--height','64','--aa','native','--report','-')
        report=json.loads(p.stdout)
        self.assertEqual(report['input']['sha256'],hashlib.sha256(INPUT.read_bytes()).hexdigest())
        self.assertEqual(report['output']['sha256'],hashlib.sha256(out.read_bytes()).hexdigest())
        self.assertEqual(set(report['stages_ms']),{'config','parse','scene','render','png'})
        self.assertNotIn('primitives',report);self.assertNotIn('atom_overrides',report['settings'])
        self.assertLess(len(p.stdout),5000)

    def test_report_path_conflicts_preserve_files(self):
        out=self.root/'output.png';out.write_bytes(b'preserve')
        for report in [out,self.root/'./output.png',INPUT,self.root]:
            self.call(INPUT,'-o',out,'--report',report,'--json-errors',ok=False)
            self.assertEqual(out.read_bytes(),b'preserve')
        alias=self.root/'alias';alias.symlink_to(out)
        self.call(INPUT,'-o',out,'--report',alias,ok=False)
        self.assertEqual(out.read_bytes(),b'preserve')

    def test_job_validation_does_not_probe_report_directory(self):
        job=self.write({'input':str(INPUT),'output':'unused.png','report':'missing/report.json'})
        self.call('--job',job,'--validate-config')
        self.assertFalse((self.root/'missing').exists())

    def test_unchanged_pixels_without_preferences(self):
        outputs=[]
        for flags in [[],['--no-user-config']]:
            out=self.root/('out'+str(len(outputs))+'.png')
            self.call(INPUT,'-o',out,'--width','64','--height','64',*flags)
            outputs.append(out.read_bytes())
        self.assertEqual(*outputs)


if __name__=='__main__': unittest.main()
