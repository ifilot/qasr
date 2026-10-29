# SPDX-License-Identifier: GPL-3.0-only
"""XML contracts, independent ElementTree/POSCAR oracles and pinned real runs."""
import gzip
import hashlib
import http.server
import json
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import threading
import unittest
import xml.etree.ElementTree as ET
from png_support import decode_png

EXE = Path(sys.argv.pop(1)).resolve()
ROOT = Path(__file__).resolve().parents[1]
XML_ENABLED = json.loads(subprocess.check_output([EXE, '--build-info']))['xml']
BASIS = [[2, 0, 0], [1, 2, 0], [.5, 1, 2]]
POSITIONS = [[.5, .25, .125], [0, 0, 0]]


def snapshot(name='', basis=BASIS, positions=POSITIONS):
    rows = lambda vs: ''.join('<v>'+' '.join(map(str, v))+'</v>' for v in vs)
    label = f' name="{name}"' if name else ''
    return f'<structure{label}><crystal><varray name="basis">{rows(basis)}</varray></crystal><varray name="positions">{rows(positions)}</varray></structure>'


METADATA = '''<atominfo><atoms>2</atoms><types>2</types>
<array name="atoms"><set><rc><c>Si</c><c>1</c></rc><rc><c>O</c><c>2</c></rc></set></array>
<array name="atomtypes"><set><rc><c>1</c><c>Si</c></rc><rc><c>1</c><c>O</c></rc></set></array></atominfo>'''


def document(steps=None, final=None):
    if steps is None:
        steps = [snapshot()]
    return '<?xml version="1.0"?>\n<modeling>'+METADATA+snapshot('initialpos')+''.join(
        '<calculation><scstep><structure/></scstep>'+s+'</calculation>\n' for s in steps
    )+(final or '')+'</modeling>\n'


class CapabilityTests(unittest.TestCase):
    def test_optional_build(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)/'arbitrary'; p.write_text(document())
            r = subprocess.run([EXE, p, '--inspect'], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0 if XML_ENABLED else 3, r.stderr)
            if not XML_ENABLED:
                self.assertIn('ENABLE_XML=ON', r.stderr)
                r = subprocess.run([EXE, p, '--inspect', '--format', 'xml'], capture_output=True, text=True)
                self.assertEqual(r.returncode, 3)


@unittest.skipUnless(XML_ENABLED, 'XML disabled in this build')
class XmlTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='qasr-xml-')
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def run_cli(self, *args, code=0):
        p = subprocess.run([EXE, *map(str, args)], capture_output=True, text=True, timeout=30)
        self.assertEqual(p.returncode, code, p.stderr)
        if code:
            self.assertEqual(p.stdout, '')
            self.assertTrue(p.stderr)
        return p

    def inspect(self, text, *args, code=0):
        path = self.root/'input.data'; path.write_text(text)
        p = self.run_cli(path, '--inspect', *args, code=code)
        return json.loads(p.stdout) if not code else p

    def test_auto_explicit_bom_and_nonxml_filename(self):
        for text in [document(), '\ufeff'+document(), '\n<!-- header -->\n'+document().split('\n', 1)[1]]:
            for args in [[], ['--format', 'xml']]:
                r = self.inspect(text, *args)
                self.assertEqual(r['format'], 'xml')
                self.assertEqual(r['frame_count'], 1)
                self.assertEqual(r['atoms'][0]['position_A'], [1.3125, .625, .25])
        self.inspect('<html><body>not VASP</body></html>', code=4)
        # POSCAR's unrestricted title may begin with '<'; its numeric header
        # still identifies it correctly instead of being mistaken for XML.
        p = self.root/'POSCAR'; p.write_text('<not_xml>\n1\n2 0 0\n0 2 0\n0 0 2\nSi\n1\nDirect\n0 0 0\n')
        self.assertEqual(json.loads(self.run_cli(p, '--inspect').stdout)['format'], 'poscar')

    def test_frame_selection_and_final_semantics(self):
        second = snapshot(basis=[[3, 0, 0], [0, 3, 0], [0, 0, 3]])
        final = snapshot('finalpos', positions=[[.25, .25, .25], [0, 0, 0]])
        text = document([snapshot(), second], final)
        r = self.inspect(text)
        self.assertEqual((r['frame'], r['frame_count']), (1, 2))
        self.assertEqual(r['atoms'][0]['position_A'], [.875, .75, .5])
        self.assertEqual(self.inspect(text, '--frame', '0')['atoms'][0]['position_A'], [1.3125, .625, .25])
        self.assertEqual(self.inspect(text, '--frame', '1')['lattice_A'], [[3, 0, 0], [0, 3, 0], [0, 0, 3]])
        self.inspect(text, '--frame', '2', code=4)
        self.assertEqual(self.inspect(document([snapshot(), second]))['lattice_A'], [[3, 0, 0], [0, 3, 0], [0, 0, 3]])

    def test_flat_steps_final_only_and_initial_is_not_a_frame(self):
        text = '<modeling>'+METADATA+snapshot('initialpos')+snapshot()+snapshot()+'</modeling>'
        self.assertEqual(self.inspect(text)['frame_count'], 2)
        text = '<modeling>'+METADATA+snapshot('finalpos')+'</modeling>'
        for args in [[], ['--frame', '0']]:
            r = self.inspect(text, *args); self.assertEqual((r['frame'], r['frame_count']), (0, 1))
        self.inspect(text, '--frame', '1', code=4)
        self.inspect('<modeling>'+METADATA+snapshot('initialpos')+'</modeling>', code=4)

    def test_primitive_cells_and_scf_not_frames(self):
        text = document().replace('<atominfo>', snapshot('primitive_cell', positions=[[0, 0, 0]])+'<atominfo>', 1)
        text = text.replace('<scstep>', '<scstep><structure><unrelated/></structure>')
        self.assertEqual(self.inspect(text)['frame_count'], 1)

    def test_species_crosschecks_and_perion_order(self):
        self.inspect(document(), '--species', 'Si,O')
        self.inspect(document(), '--species', 'O,Si', code=5)
        self.inspect(document(), '--species', 'Si', code=5)
        self.inspect(document().replace('<c>O</c><c>2', '<c>Si</c><c>2'), code=5)
        self.inspect(document().replace('<c>1</c><c>O', '<c>0</c><c>O'), code=5)
        self.inspect(document().replace('<c>Si</c>', '<c>Xx</c>', 1), code=5)
        self.inspect(document().replace('<c>O</c><c>2', '<c>O</c><c>3'), code=4)
        # Per-ion order may differ from species order. Two potential types can
        # also represent the same chemical element (as in the real relaxation).
        text = document().replace('<rc><c>Si</c><c>1</c></rc><rc><c>O</c><c>2</c></rc>', '<rc><c>O</c><c>2</c></rc><rc><c>Si</c><c>1</c></rc>')
        self.assertEqual([a['element'] for a in self.inspect(text)['atoms']], ['O', 'Si'])
        self.assertEqual([a['element'] for a in self.inspect(document().replace('<c>O</c>', '<c>Si</c>'))['atoms']], ['Si', 'Si'])

    def test_incomplete_duplicate_and_bad_metadata(self):
        for old, new in [('<atoms>2</atoms>', '<atoms>0</atoms>'), ('<atoms>2</atoms>', '<atoms>1000001</atoms>'),
                         ('<types>2</types>', '<types>119</types>'), ('<types>2</types>', ''),
                         ('<atoms>2</atoms>', '<atoms>2</atoms><atoms>2</atoms>'),
                         ('<rc><c>Si</c><c>1</c></rc>', ''), ('<c>Si</c><c>1</c>', '<c>Si</c>')]:
            with self.subTest(old=old, new=new): self.inspect(document().replace(old, new, 1), code=4)
        self.inspect(document().replace('</atominfo>', '</atominfo>'+METADATA, 1), code=4)

    def test_bad_vectors_and_missing_geometry(self):
        for old, new in [('<v>2 0 0</v>', ''), ('<v>2 0 0</v>', '<v>2 0 0 0</v>'),
                         ('<v>2 0 0</v>', '<v>2 0</v>'), ('<v>2 0 0</v>', '<v>NaN 0 0</v>'),
                         ('<v>2 0 0</v>', '<v>inf 0 0</v>'), ('<v>2 0 0</v>', '<v>2 0 0 #junk</v>'),
                         ('<v>0.5 0.25 0.125</v>', ''),
                         ('<v>0.5 0.25 0.125</v>', '<v><x>0.5</x> 0.25 0.125</v>')]:
            with self.subTest(new=new): self.inspect(document([snapshot().replace(old, new)]), code=4)
        self.inspect(document([snapshot().replace('name="basis"', 'name="wrong"')]), code=4)
        self.inspect(document([snapshot().replace('name="positions"', 'name="wrong"')]), code=4)

    def test_range_and_lattice_validation(self):
        for basis, positions in [([[0, 0, 0]]*3, POSITIONS), ([[1e10, 0, 0], [0, 1, 0], [0, 0, 1]], POSITIONS),
                                  (BASIS, [[1e13, 0, 0], [0, 0, 0]]), (BASIS, [[9e11, 9e11, 9e11], [0, 0, 0]])]:
            self.inspect(document([snapshot(basis=basis, positions=positions)]), code=5)

    def test_duplicate_basis_and_structure_order(self):
        s = snapshot(); basis = '<varray name="basis"><v>2 0 0</v><v>1 2 0</v><v>0.5 1 2</v></varray>'
        self.inspect(document([s.replace('</crystal>', basis+'</crystal>')]), code=4)
        self.inspect(document([s], snapshot('finalpos')+s), code=4)
        self.inspect(document([s], snapshot('finalpos')*2), code=4)
        self.inspect(document().replace('</atominfo>', '</atominfo>'+snapshot('initialpos')), code=4)
        self.inspect('<modeling>'+s+METADATA+'</modeling>', code=4)

    def test_complete_document_and_validate_later_frames(self):
        text = document([snapshot(), snapshot()])
        for bad in [text[:-15], text.replace('</modeling>', ''), text+'</extra>', text.replace('</calculation>', '</wrong>', 1)]:
            self.inspect(bad, code=4)
        self.inspect(document([snapshot(), snapshot().replace('<v>2 0 0</v>', '<v>NaN 0 0</v>')]), '--frame', '0', code=4)
        self.inspect(document(), '--recover', code=2)

    def test_failure_preserves_destination(self):
        src = self.root/'vasprun.xml'; src.write_text(document()[:-15])
        output = self.root/'out.png'; output.write_bytes(b'existing output')
        self.run_cli(src, '-o', output, code=4)
        self.assertEqual(output.read_bytes(), b'existing output')
        self.assertEqual(sorted(p.name for p in self.root.iterdir()), ['out.png', 'vasprun.xml'])

    def test_dtd_and_entities_are_rejected_without_loading(self):
        # A sentinel URI appears in diagnostics only if a loader tries it.
        secret = self.root/'secret'; secret.write_text('PRIVATE_SENTINEL')
        for declaration in [f'<!DOCTYPE modeling SYSTEM "{secret.as_uri()}">',
                            '<!DOCTYPE modeling [<!ENTITY a "Si">]>',
                            f'<!DOCTYPE modeling [<!ENTITY a SYSTEM "{secret.as_uri()}">]>']:
            p = self.inspect(document().replace('<modeling>', declaration+'<modeling>'), code=4)
            self.assertIn('DTDs', p.stderr); self.assertNotIn('PRIVATE_SENTINEL', p.stderr)

    def test_network_dtd_and_xinclude_are_not_fetched(self):
        requests = []
        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                requests.append(self.path); self.send_response(200); self.end_headers(); self.wfile.write(b'<!ENTITY a "Si">')
            def log_message(self, *args): pass
        with http.server.HTTPServer(('127.0.0.1', 0), Handler) as server:
            worker = threading.Thread(target=server.serve_forever, daemon=True); worker.start()
            try:
                uri = f'http://127.0.0.1:{server.server_port}/external'
                self.inspect(document().replace('<modeling>', f'<!DOCTYPE modeling SYSTEM "{uri}"><modeling>'), code=4)
                self.inspect(document().replace('<modeling>', f'<modeling><xi:include xmlns:xi="http://www.w3.org/2001/XInclude" href="{uri}"/>'), code=4)
                self.assertEqual(requests, [])
            finally: server.shutdown(); worker.join()

    def test_depth_text_and_line_limits(self):
        extra = '<ignored>'*66+'</ignored>'*66
        self.inspect(document().replace('<modeling>', '<modeling>'+extra), code=4)
        self.inspect(document().replace('<modeling>', '<modeling><!--'+'x'*65537+'-->'), code=4)
        self.inspect(document().replace('<modeling>', '<modeling><ignored>'+('x'*100+'\n')*700+'</ignored>'), code=4)
        self.inspect(document().replace('<modeling>', '<modeling>\x00'), code=4)
        self.inspect(document().replace('<modeling>', '<modeling><ignored name="'+('x'*100+'\n')*700+'"/>'), code=4)
        self.inspect(document([snapshot().replace('<varray name="basis">', '<varray name="basis">junk')]), code=4)

    def test_ignored_data_fortran_numbers_and_split_text(self):
        text = document().replace('<v>2 0 0</v>', '<v>2D0 <![CDATA[0]]> 0</v>')
        text = text.replace('<modeling>', '<modeling><unknown><r>***** NaN irrelevant</r></unknown>')
        self.assertEqual(self.inspect(text)['atoms'][0]['position_A'], [1.3125, .625, .25])

    def test_random_skew_conversion(self):
        rng = random.Random(7029)
        for _ in range(15):
            b = [[rng.uniform(2, 5), 0, 0], [rng.uniform(-1, 1), rng.uniform(2, 5), 0], [rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(2, 5)]]
            ps = [[rng.uniform(-2, 2) for _ in range(3)] for _ in range(2)]
            result = self.inspect(document([snapshot(basis=b, positions=ps)]))
            for atom, frac in zip(result['atoms'], ps):
                for j, actual in enumerate(atom['position_A']): self.assertAlmostEqual(actual, sum(frac[k]*b[k][j] for k in range(3)), places=12)

    def test_thousand_frame_stream(self):
        text = document([snapshot(positions=[[i/1000, 0, 0], [0, 0, 0]]) for i in range(1000)])
        for flag, index in [('final', 999), ('123', 123)]:
            r = self.inspect(text, '--frame', flag)
            self.assertEqual((r['frame_count'], r['frame']), (1000, index))
            self.assertAlmostEqual(r['atoms'][0]['position_A'][0], index/500)

    def test_real_fixtures_against_independent_oracle_and_poscar_pixels(self):
        fixtures = [
            ('vasprun.relax.xml.gz', '46bea71afefe257ddcb85f8b360d5ee17457216d1e82ba2429bf99693baf5036', 25, 29),
            ('vasprun.md.xml.gz', '61774b76374744f07bb40b2fad2f2a72556be1009ed8541d27c386dff590c8ff', 64, 10),
            ('vasprun.vasp6.xml.gz', '4f06eac39002d48be34e750380d51f7a04a5b5c5f1f96a798918280e78f098e5', 30, 1),
        ]
        for name, digest, atoms, frames in fixtures:
            with self.subTest(name=name):
                data = (ROOT/'tests/fixtures'/name).read_bytes(); self.assertEqual(hashlib.sha256(data).hexdigest(), digest)
                text = gzip.decompress(data).decode(); tree = ET.fromstring(text)
                steps = tree.findall('calculation/structure'); self.assertEqual(len(steps), frames)
                species = [rc.findtext('c').strip() for rc in tree.findall('atominfo/array[@name="atoms"]/set/rc')]
                for selector, node in [('0', steps[0]), (str(frames-1), steps[-1]), ('final', tree.find('structure[@name="finalpos"]'))]:
                    r = self.inspect(text, '--frame', selector)
                    self.assertEqual(r['frame_count'], frames); self.assertEqual(r['atom_count'], atoms)
                    b = [[float(x) for x in v.text.split()] for v in node.findall('crystal/varray[@name="basis"]/v')]
                    p = [[float(x) for x in v.text.split()] for v in node.findall('varray[@name="positions"]/v')]
                    self.assertEqual(r['lattice_A'], b)
                    self.assertEqual([a['element'] for a in r['atoms']], species)
                    for a, f in zip(r['atoms'], p):
                        for j in range(3): self.assertAlmostEqual(a['position_A'][j], sum(f[k]*b[k][j] for k in range(3)), places=11)
                # Build a POSCAR from the XML's direct coordinate strings. The
                # independent parser must produce exactly the same render pixels.
                final = tree.find('structure[@name="finalpos"]')
                ordered = list(dict.fromkeys(species)); counts = [species.count(z) for z in ordered]
                poscar = self.root/'POSCAR'; poscar.write_text('XML equivalent\n1\n'+''.join(v.text.strip()+'\n' for v in final.findall('crystal/varray[@name="basis"]/v'))+
                    ' '.join(ordered)+'\n'+' '.join(map(str, counts))+'\nDirect\n'+''.join(v.text.strip()+'\n' for v in final.findall('varray[@name="positions"]/v')))
                for annotations in [[], ['--labels', 'element', '--legend', 'on', '--axes', 'on', '--scale-bar', 'on']]:
                    for source, png in [(self.root/'input.data', self.root/'xml.png'), (poscar, self.root/'poscar.png')]:
                        self.run_cli(source, '-o', png, '--width', '256', '--height', '256', *annotations)
                    self.assertEqual(decode_png(self.root/'xml.png'), decode_png(self.root/'poscar.png'))


if __name__ == '__main__':
    unittest.main()
