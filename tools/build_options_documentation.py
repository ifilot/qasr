# SPDX-License-Identifier: GPL-3.0-only
"""Build the complete user options guide and render its downloadable recipes."""
import argparse
import hashlib
from html import escape
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
GROUPS = {
    'input': 'Read and inspect a structure',
    'appearance': 'Style, camera and image quality',
    'geometry': 'Atoms, bonds and periodic cells',
    'annotations': 'Cell lines, labels and annotations',
    'files': 'Output and reusable settings',
    'performance': 'Speed and automation',
    'movies': 'Camera orbits and reaction movies',
}
CATALOG = []


def add(group, flags, title, values, description, example, note=''):
    CATALOG.append(dict(group=group, flags=flags.split(), title=title, values=values,
                        description=description, example=example, note=note))


def render(flags='', source='CONTCAR.co', output='structure.png'):
    return f'qasr examples/{source} --no-user-config {flags} -o {output}'.replace('  ', ' ')


add('input', '--format', 'Choose the input reader', 'auto (default), poscar, contcar, outcar, xml, xyz, extxyz, gaussian-input, gaussian-output, ams',
    'Normally QASR detects the format from the file contents. Choose a reader explicitly when you want to constrain detection.',
    render('--format xyz --cell off', 'ring.xyz'), 'XML requires an XML-enabled build. CIF files must first be converted to a supported format.')
add('input', '--frame', 'Select a geometry frame', 'final (default), or a zero-based integer',
    'Choose the final geometry or a particular ionic snapshot. Frame 0 is the first snapshot; a single-frame input accepts only 0 or final.',
    render('--frame 0 --view top', 'OUTCAR.relax'))
add('input', '--recover', 'Read a complete snapshot from an unfinished OUTCAR', 'Off by default; no value after the flag',
    'Permit an unfinished OUTCAR when it contains a complete usable snapshot. A warning records recovery; malformed coordinates are still errors.',
    render('--recover --frame final', 'OUTCAR.relax'))
add('input', '--species', 'Supply missing element identities', 'Comma-separated symbols in source population order',
    'Provide species for an older POSCAR without an element-symbol row. Explicit identities must agree with any identities already in the input.',
    render('--species Si,O', 'POSCAR.skew'))
add('input', '--inspect', 'Check the input without rendering', 'No output image required',
    'Print normalized geometry, selected frame, cell vectors, periodic axes and effective settings as JSON.',
    'qasr examples/POSCAR.skew --no-user-config --inspect')
add('input', '--inspect-scene', 'Inspect the displayed atoms and bonds', 'No output image required',
    'Print atom instances, complete bonds, cell edges and the fitted camera. Useful for checking expansion and connectivity.',
    'qasr examples/CONTCAR.co --no-user-config --expand-xy 1,1 --inspect-scene')
add('input', '--build-info', 'Check compiled capabilities', 'Run alone',
    'Report the program version and features such as OpenMP, XML, JPEG, WebP and polygon faces.', 'qasr --build-info')
add('input', '--help', 'See the command-line synopsis', 'Run alone',
    'Print the supported command-line syntax for your executable.', 'qasr --help')
add('input', '--version', 'Check the installed version', 'Run alone',
    'Print the executable name and version.', 'qasr --version')
add('appearance', '--style', 'Choose a drawing style', 'shaded (default), illustrated, ortep, balsac',
    'Use lighting for depth, flat illustrated contours, ORTEP spherical arcs, or BALSAC-inspired highlights. A style flag does not load a preset palette or camera.',
    render('--config examples/illustrated.json --style illustrated'))
add('appearance', '--tone', 'Use monochrome or two-color stippling', 'color (default), monocolor, dualtone',
    'Apply a shared black/white or navy/gold dithered palette to ORTEP or BALSAC spheres.',
    render('--style ortep --tone dualtone --cell off'), 'Choose ortep or balsac when using a non-color tone.')
add('appearance', '--view', 'Aim the camera', '1,1,1 (default), top, or X,Y,Z',
    'Set the direction toward the camera. Top looks along the actual a × b surface normal and aligns a with image right.',
    render('--config examples/illustrated.json --view top'), 'Top requires independent a/b cell vectors. Use a numeric direction for a cell-free molecule.')
add('appearance', '--up', 'Rotate the image in its plane', '0,0,1 by default; top view chooses a-based alignment unless overridden',
    'Choose the world direction that projects toward the top of the image. It must not be parallel to the viewing direction.',
    render('--view 0,0,1 --up 0,1,0'))
add('appearance', '--width --height', 'Set output dimensions', '1–16384 pixels per dimension; default 512 × 512',
    'Set the pixel dimensions of the final image. More pixels retain detail for export or printing, at the cost of more work and memory.',
    render('--width 1200 --height 900'), 'The total-pixel and memory budgets can reject otherwise valid dimensions.')
add('appearance', '--background', 'Choose a background or transparency', '#RRGGBB or #RRGGBBAA; default opaque white',
    'Set canvas color and opacity. An alpha suffix of 00 makes the background fully transparent; ff is opaque.',
    render("--background '#ffffff00'"), 'Quote color strings in a shell. JPEG requires an opaque result.')
add('appearance', '--aa', 'Smooth image edges', 'native, 2x (default), 4x, adaptive',
    'Choose the sampling quality. Native is useful for previews; 2× and 4× take more samples. Adaptive selectively refines supported geometry.',
    render('--width 768 --height 768 --aa 4x'), 'Some styles and scene features use full 2× sampling in adaptive mode.')
add('appearance', '--outline-width', 'Set atom and bond ink width', '0–8 output pixels; default 1',
    'Adjust illustrated, ORTEP and BALSAC contours. Zero removes the contours and ORTEP arcs.',
    render('--config examples/illustrated.json --outline-width 1.5'))
add('appearance', '--backend', 'Choose the rendering backend', 'zbuffer (default), cairo',
    'The Z-buffer resolves intersecting surfaces and supports the full drawing interface. Cairo is an optional experimental painter with depth and feature limitations.',
    render('--backend zbuffer'), 'Cairo requires a special build and does not support every style, polygon faces, annotations or multiple render threads.')
add('geometry', '--repeat', 'Repeat a periodic cell', 'NX,NY,NZ, each 1–1024; default 1,1,1',
    'Repeat the structure in the positive lattice-vector directions. Every bond requires both endpoint atoms in the displayed cells.',
    render('--repeat 2,2,1 --view top'), 'Only periodic axes can repeat. Reset centered expansion before using an ordinary repeat.')
add('geometry', '--expand-xy', 'Add neighbors around the primary cell', 'NX,NY, each 0–511; default 0,0',
    'Add this many neighboring cells on each side along a and b. A value of 1,1 gives a centered 3 × 3 patch. Bonds to included neighbors remain connected.',
    render('--config examples/illustrated.json --expand-xy 1,1'), 'Bonds stop at the outer displayed boundary. Centered expansion and nontrivial ordinary repetition are mutually exclusive.')
add('geometry', '--image-colors', 'Distinguish periodic neighbors', 'normal, muted, greyscale; centered expansion defaults to muted',
    'Keep the primary cell in its element palette and choose how neighboring atom copies are colored. Geometry and atom opacity are preserved.',
    render('--config examples/illustrated.json --expand-xy 1,1 --image-colors greyscale'))
add('geometry', '--sphere-scale', 'Scale every atom radius', 'Positive multiplier; default 1',
    'Multiply the display radii of all atoms. Smaller spheres reveal bonds and polygon faces without moving atoms or changing connectivity.',
    render('--sphere-scale 0.45 --bond-colors atoms --cell off', 'ring.xyz'))
add('geometry', '--bond-radius', 'Set bond thickness', 'Positive radius in Å; default 0.12',
    'Set the radius of the bond cylinders. Their diameter is twice this value. Display thickness does not change which atoms are connected.',
    render('--bond-radius 0.075 --bond-colors atoms --cell off', 'ring.xyz'))
add('geometry', '--bond-colors', 'Color both halves from their atoms', 'uniform (default), atoms',
    'With atoms, the first half takes one endpoint atom’s color and the second half takes the other. The halves meet at the midpoint.',
    render('--config examples/ring-bonds.json --bond-colors atoms', 'ring.xyz'))
add('geometry', '--faces', 'Fill a polygon or zeolite tile', 'Path to a polygon-face JSON file',
    'Load an ordered polygon boundary from explicit vertices or zero-based atom indices. A single face is sufficient; a complete cage is optional. Alpha in the face color controls translucency.',
    render('--config examples/zeolite-faces.json --faces examples/tiles.json', 'cage.xyz'), 'For visible oxygen atoms, include the bridging O positions in the polygon loop. CIF input requires conversion first.')
add('annotations', '--cell', 'Show the unit-cell edges', 'on (default), off',
    'Draw depth-tested cell edges. Hiding them also removes cell and vacuum extents from auto-framing.',
    render('--view top --cell off'))
add('annotations', '--cell-width', 'Set unit-cell stroke thickness', 'Greater than 0, up to 32 output pixels; default 2',
    'Change the width of the inner cell line independently of atom and bond outlines.',
    render('--view top --cell-width 3'))
add('annotations', '--cell-style', 'Use dashed unit-cell edges', 'solid (default), dashed',
    'Choose a continuous or dashed stroke for the cell boundary.',
    render('--config examples/cell-contour.json --cell-style dashed'))
add('annotations', '--cell-color', 'Choose the cell stroke color', '#RRGGBB or #RRGGBBAA; default #464646',
    'Set the inner unit-cell line color and opacity. Use black with a white contour for contrast over colored atoms.',
    render("--view top --cell-color '#000000' --cell-outline-width 1.5"))
add('annotations', '--cell-outline-width', 'Add a white contour', '0–8 output pixels per side; default 0',
    'Surround the inner cell stroke with white. It follows dash segments and their ends; zero disables it.',
    render('--config examples/cell-contour.json --cell-outline-width 1.5'))
add('annotations', '--cell-dash-length --cell-dash-gap', 'Adjust the dash pattern', '1–256 output pixels; default length 8, gap 5',
    'Set the length of each dash and the blank space between dashes. These settings apply to dashed cell lines.',
    render('--view top --cell-style dashed --cell-dash-length 12 --cell-dash-gap 6'))
add('annotations', '--labels', 'Label visible atoms', 'none (default), element, index',
    'Display element symbols or zero-based rendered-instance indices. Collision filtering suppresses labels that would overlap or lie behind surfaces.',
    render('--config examples/illustrated.json --labels element'))
add('annotations', '--legend', 'Show element colors and counts', 'off (default), on',
    'Add a legend that counts all displayed atom instances, including repeated and hidden atoms.',
    render('--config examples/illustrated.json --legend on'))
add('annotations', '--axes', 'Add Cartesian axes', 'off (default), on',
    'Project the Cartesian X, Y and Z directions into the image. These are world axes, rather than lattice a, b and c.',
    render('--view top --axes on'))
add('annotations', '--scale-bar', 'Show physical scale', 'off (default), on',
    'Add a fitted bar labeled in angstroms, based on the final camera scale.',
    render('--config examples/illustrated.json --scale-bar on'))
add('files', '--output -o', 'Write a PNG, JPEG or WebP image', 'Local destination path; no default image path',
    'The filename extension selects the image format. PNG is always supported; JPEG and lossless WebP require optional build capabilities.',
    render('--view top', output='cobalt.png'), 'JPEG needs opaque pixels. Images cannot be written to stdout.')
add('files', '--png-encoder', 'Choose the PNG encoder', 'libdeflate when available, otherwise libpng; optional cairo',
    'Select an encoder supported by your build. Libdeflate and libpng preserve image pixels; the experimental Cairo encoder has transparent-RGB limitations.',
    render('--png-encoder libpng --png-compression 6'))
add('files', '--png-compression', 'Balance PNG size and write time', 'auto (default), or integer 0–9',
    'Choose lossless compression effort. Auto selects an encoding level for the destination filesystem. Start at 1 for compact output with modest encoding effort.',
    render('--png-compression 1'))
add('files', '--png-filter', 'Choose the PNG row filter', 'none, sub, up, adaptive; default depends on encoder',
    'Control the scanline filter before compression. It changes encoding effort and size, while libpng/libdeflate preserve the decoded pixels.',
    render('--png-encoder libpng --png-filter adaptive --png-compression 6'))
add('files', '--config', 'Reuse a JSON recipe', 'One JSON path, or - for stdin',
    'Load rendering settings, then override selected fields with explicit command-line flags. Objects merge by field and arrays replace.',
    render('--config examples/illustrated.json --width 1000'))
add('files', '--no-user-config', 'Start from reproducible built-in defaults', 'No value after the flag',
    'Skip automatically discovered personal preferences for this invocation. Explicit recipes and command-line options still apply.',
    render('--view top'))
add('files', '--init-config', 'Create personal preferences', 'Run alone; refuses to overwrite an existing file',
    'Create a starter config in your user configuration directory. Edit it to set the defaults you use most often.', 'qasr --init-config',
    'Uses an absolute XDG_CONFIG_HOME, otherwise HOME/.config. Never copy a complete machine-specific invocation into this preferences file.')
add('files', '--dump-config', 'See settings and where they came from', 'No image required',
    'Print resolved configuration, runtime settings and per-field provenance. Useful when a personal default, recipe and CLI flag interact.',
    'qasr --no-user-config --config examples/illustrated.json --dump-config')
add('files', '--validate-config', 'Check a recipe before rendering', 'Input path optional unless the settings need input identity',
    'Validate combined settings, selectors and build requirements without writing an image. Include an input to check atom indices and element identities.',
    'qasr examples/CONTCAR.co --no-user-config --config examples/source-highlight.json --validate-config')
add('files', '--schema', 'Get machine-readable JSON rules', 'config (default), user, job, worker, faces; run alone',
    'Print a Draft 2020-12 schema for settings, preferences, invocations, batch requests or polygon-face files.', 'qasr --schema config')
add('files', '--job', 'Bundle input, output and settings', 'JSON path, or - for stdin; exclusive with --config',
    'Load a complete invocation containing paths and embedded rendering settings. Paths in a job file resolve relative to that file.',
    'qasr --job examples/highlight-job.json')
add('files', '--report', 'Save a reproducibility record', 'JSON path, or - for stdout',
    'Write a successful render’s settings, counts, input/output hashes and stage timings. It complements the image with a compact record.',
    render('--view top --report render-report.json'))
add('files', '--json-errors', 'Return structured error details', 'No value after the flag',
    'Format failures as JSON on stderr with an error code, exit status, message and setting path where available.',
    'qasr examples/POSCAR.skew --no-user-config --json-errors --validate-config')
add('files', '--', 'Use a filename beginning with a dash', 'Put rendering options before the terminator',
    'End option parsing so the next positional token can be a literal input path beginning with a dash.',
    'qasr --no-user-config -o structure.png -- -structure.vasp', 'Replace -structure.vasp with your filename; the other examples use files from the example kit.')
add('performance', '--threads', 'Choose CPU parallelism', 'auto (default), or 1–256',
    'Choose render threads. Auto uses one for small images and up to eight for images at least 512 × 512, subject to environment limits.',
    render('--threads 4 --width 768 --height 768'), 'Multiple threads require OpenMP. Divide CPUs between processes when launching several workers.')
add('performance', '--benchmark-stages', 'Measure the rendering steps', 'No value after the flag',
    'Write configuration, parse, scene, rendering and encoding timings to stderr. Repeat a comparable job to distinguish sustained behavior from one-off variation.',
    render('--benchmark-stages --threads 4 --width 768 --height 768'))
add('performance', '--batch', 'Render several requests in one process', 'First argument; read JSONL from stdin',
    'Process render, validate, stats and clear_cache requests sequentially. Retain input and scenes within the cache budget; each render can use CPU threads.',
    'cd examples\nqasr --batch < worker-requests.jsonl', 'Start in the directory containing the supplied inputs. Batch workers skip implicit personal preferences.')
add('performance', '--cache-mib', 'Bound retained batch data', '0–512 MiB; default 64; batch only',
    'Set the input/scene cache budget. Zero disables retention. This is a cache limit rather than a cap on all process memory.',
    'cd examples\nqasr --batch --cache-mib 128 < worker-requests.jsonl')
add('performance', '--max-jobs', 'Stop a batch after a chosen count', '1–1000000; default reads until end of input; batch only',
    'Exit after this many nonblank requests. Useful when supervising or periodically replacing a worker.',
    'cd examples\nqasr --batch --max-jobs 3 < worker-requests.jsonl')

add('movies', '--movie', 'Make a camera orbit or reaction movie', 'orbit, trajectory; no movie by default',
    'Stream rendered frames directly into an MP4. Orbit moves the camera around one structure; trajectory plays all snapshots with fixed framing.',
    render('--movie orbit --movie-frames 60 --movie-fps 24 --cell off', 'cage.xyz', 'orbit.mp4'),
    'Requires ENABLE_FFMPEG=ON. Use even image dimensions and an opaque background. See the movie guide for input and codec requirements.')
add('movies', '--movie-fps', 'Set movie playback speed', 'Integer 1–120; default 30',
    'Choose how many encoded frames play per second. This sets visual playback speed without assuming a physical simulation time step.',
    render('--movie trajectory --movie-fps 24 --config examples/reaction.json', 'reaction.xyz', 'reaction.mp4'), 'Requires a build with FFmpeg support.')
add('movies', '--movie-frames', 'Choose the length of an orbit', '2–100000; default 120; orbit only',
    'Set the number of camera poses. At 24 fps, 120 frames play for five seconds. Full turns avoid duplicating the first pose at the end.',
    render('--movie orbit --movie-frames 120 --movie-fps 24 --cell off', 'cage.xyz', 'orbit.mp4'), 'Requires a build with FFmpeg support.')
add('movies', '--movie-axis', 'Choose the world axis of rotation', 'Nonzero X,Y,Z; default 0,0,1; orbit only',
    'Orbit around a world Cartesian axis through the displayed structure center. View, up and right rotate together without a pole flip.',
    render('--movie orbit --movie-axis 0,1,0 --movie-frames 60 --cell off', 'cage.xyz', 'orbit.mp4'), 'Requires a build with FFmpeg support.')
add('movies', '--movie-angle', 'Set the orbit angle and direction', 'Nonzero −3600 to 3600 degrees; default 360; orbit only',
    'Choose a full revolution or a partial arc. Negative angles reverse direction. Partial arcs include both endpoint views.',
    render('--movie orbit --movie-angle 180 --movie-frames 60 --cell off', 'cage.xyz', 'orbit.mp4'), 'Requires a build with FFmpeg support.')
add('movies', '--movie-interpolate', 'Smooth a supplied reaction path', '0–1000 inserted geometries per interval; default 0; trajectory only',
    'Insert linearly interpolated Cartesian positions and cell vectors between source snapshots. The fixed camera fits every displayed pose.',
    render('--movie trajectory --movie-interpolate 3 --config examples/reaction.json', 'reaction.xyz', 'reaction.mp4'),
    'Requires FFmpeg support. This smooths a supplied path; it does not calculate a reaction, infer atom mapping or unwrap periodic positions.')
add('movies', '--movie-codec', 'Select the MP4 encoder', 'auto (default), h264, mpeg4',
    'Auto prefers FFmpeg’s libx264 H.264 encoder and otherwise uses native MPEG-4 Part 2. An explicit encoder must be available in the linked libraries.',
    render('--movie orbit --movie-codec mpeg4 --movie-frames 60 --cell off', 'cage.xyz', 'orbit.mp4'),
    'Requires FFmpeg support. YUV 4:2:0 MP4 output uses no alpha channel and is not lossless RGB.')

EXTRA_FIELDS = {
    'schema_version': ('Settings format version; omit or use 1.', '1'),
    '$schema': ('Optional JSON Schema URL for editor assistance.', '"https://ifilot.github.io/qasr/schemas/config.schema.json"'),
    'image.depth_fade': ('Fade deeper illustrated layers toward white, from 0 to 1.', '0.6'),
    'camera.projection': ('Projection model; orthographic is the supported value.', '"orthographic"'),
    'camera.margin': ('Fractional fitting space on each side; at least 0 and less than 0.5.', '0.06'),
    'elements': ('Map element symbols to display color and/or radius_A. Unspecified elements use built-in styles.', '{"Si":{"color":"#70a9d2","radius_A":0.32}}'),
    'bond_cutoffs_A': ('Map element pairs to connectivity cutoffs in Å. This sets connectivity, independently of cylinder radius.', '{"Si-O":1.9}'),
    'atom_overrides': ('Apply ordered source-index, element and optional cell-specific display overrides. Indices are zero-based.', '[{"select":{"source_indices":[6]},"color":"#ee6633","radius_A":0.95}]'),
    'species': ('Supply ordered element symbols for otherwise unlabeled input species.', '["Si","O"]'),
    'faces': ('Embed polygon faces directly in a recipe; see the face-field reference below.', '[{"vertices":[[0,0,0],[3,0,0],[0,3,0]],"color":"#54b6d880"}]'),
    'faces_file': ('Load faces from a separate JSON file, relative to the recipe file.', '"tiles.json"'),
    'elements.<symbol>.color': ('Set an element display color, including optional alpha.', '"#70a9d2"'),
    'elements.<symbol>.radius_A': ('Set an element display radius in Å, before sphere scaling.', '0.32'),
    'atom_overrides[].select.element': ('Match a source element; combines with other selector criteria.', '"Si"'),
    'atom_overrides[].select.source_indices': ('Match specific zero-based source atom indices.', '[6,34]'),
    'atom_overrides[].select.source_range': ('Match an inclusive zero-based source-index range.', '[0,9]'),
    'atom_overrides[].select.cell': ('Limit an override to a specific displayed lattice origin.', '[1,0,0]'),
    'atom_overrides[].color': ('Replace the matched atom color; later rules take precedence.', '"#ee6633"'),
    'atom_overrides[].radius_A': ('Replace the matched atom display radius in Å.', '0.95'),
}
CLI_FIELDS = {'image.'+x: '--'+x.replace('_','-') for x in ['width','height','background','style','tone','aa','backend','outline_width','png_encoder','png_filter','png_compression']}
CLI_FIELDS.update({'camera.view':'--view','camera.up':'--up','geometry.repeat':'--repeat','geometry.expand_xy':'--expand-xy','geometry.image_colors':'--image-colors','geometry.sphere_scale':'--sphere-scale','geometry.bond_radius_A':'--bond-radius','geometry.bond_colors':'--bond-colors'})
CLI_FIELDS.update({'overlays.'+x:'--'+x.replace('_','-') for x in ['cell','cell_width','cell_style','cell_color','cell_outline_width','cell_dash_length','cell_dash_gap','labels','axes','legend','scale_bar']})
FACE_HELP = {
    'vertices': 'Ordered 3D points forming a face boundary; 3–64 vertices. Do not repeat the first point at the end.',
    'coordinates': 'cartesian (default) uses Å; fractional uses the full cell basis. Applies to explicit vertices.',
    'atom_indices': 'An ordered loop of 3–64 zero-based source atom indices; use this instead of vertices to follow atoms.',
    'cell_offsets': 'One integer lattice offset per atom index; omitted offsets are zero. Nonzero offsets require a periodic axis. Uses wrapped primary-cell atom positions.',
    'color': 'Fill color #RRGGBB[AA]; default #43a6cf80. The alpha suffix sets translucency.',
    'outline_color': 'Boundary color #RRGGBB[AA]; default #23556e.',
    'outline_width': 'Inset boundary width in output pixels, 0–8; default 0 removes the outline.',
    'repeat': 'true (default) translates whole faces with repeat/expansion; false draws a face once.',
}


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('executable',type=Path)
    args=parser.parse_args();exe=args.executable.resolve();site=ROOT/'docs/website';downloads=site/'downloads'
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    version=json.loads(subprocess.check_output([exe,'--build-info']))['version']
    flags=set(re.findall(r'--[a-z][a-z-]*',subprocess.check_output([exe,'--help'],text=True)))
    covered={flag for item in CATALOG for flag in item['flags']}
    assert flags<=covered, 'Missing CLI options: '+str(flags-covered)
    for name in ['cage.xyz','ring.xyz','tiles.json','single-face.json','LTA.cif']:
        shutil.copyfile(ROOT/'examples/zeolite'/name, downloads/name)
    recipes=[
        ('zeolite-faces','Polygon faces on a complete cage','cage.xyz', ROOT/'examples/zeolite/renders/02-lta-polygon-tiles.json',
         'A complete LTA alpha cage with translucent faces spanning both Si and O atoms.', 'tiles.json'),
        ('single-polygon','One translucent polygon','ring.xyz', ROOT/'examples/zeolite/renders/03-single-face-translucent.json',
         'A single ordered ring is enough to define a tile; no closed cage is required.', 'single-face.json'),
        ('ring-bonds','Atom-colored bond halves','ring.xyz', ROOT/'examples/zeolite/renders/06-atom-colored-bonds.json',
         'Each half of a Si–O bond takes the color of its originating atom.', None),
        ('thin-bonds','Thin bond cylinders','ring.xyz', ROOT/'examples/zeolite/renders/07-thin-bonds.json',
         'A small bond radius leaves more space around a polygonal ring.', None),
        ('thick-bonds','Thicker bond cylinders','ring.xyz', ROOT/'examples/zeolite/renders/08-thick-bonds.json',
         'The same atoms and connectivity with a larger display radius for bonds.', None),
        ('cell-contour','Dashed cell with a white contour','CONTCAR.co', ROOT/'examples/cell-lines/05-dashed-white-contour.json',
         'Black dashes remain legible over atoms thanks to the white surround.', None),
    ]
    visuals=[]
    for name,title,source,path,caption,faces in recipes:
        config=json.loads(path.read_text())
        if faces:config['faces_file']=faces
        recipe=downloads/(name+'.json');recipe.write_text(json.dumps(config,indent=2)+'\n')
        image=site/'assets'/(name+'.png')
        cmd=[str(exe),str(downloads/source),'--no-user-config','--config',str(recipe),'-o',str(image)]
        subprocess.run(cmd,check=True,capture_output=True)
        scene=json.loads(subprocess.check_output(cmd[:-2]+['--inspect-scene']))
        atoms={(p['source'][0],tuple(p['origin'])) for p in scene['primitives'] if p['kind']=='sphere'}
        for p in scene['primitives']:
            if p['kind']=='bond':
                assert (p['source'][0],tuple(p['origin'])) in atoms
                assert (p['source'][1],tuple(a+b for a,b in zip(p['origin'],p['image']))) in atoms
        visuals.append(dict(id=name,title=title,caption=caption,source=source,image='assets/'+image.name,recipe='downloads/'+recipe.name,
                            png_sha256=sha(image),input_sha256=sha(downloads/source),config_sha256=sha(recipe),
                            command=render('--config examples/'+name+'.json',source,name+'.png')))
    e=escape
    intro='''<!doctype html>
<!-- SPDX-License-Identifier: GPL-3.0-only -->
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><meta name="description" content="Every QASR command-line option explained, with searchable topics, defaults, copyable examples and visual recipes for atoms, bonds, polygon faces and cell lines."><title>All options &amp; examples — QASR</title><link rel="icon" href="assets/favicon.svg" type="image/svg+xml"><link rel="stylesheet" href="styles.css"><script src="options.js" defer></script></head>
<body><a class="skip" href="#guide">Skip to options</a><main id="guide" class="standalone-guide">
<nav class="guide-nav" aria-label="Handbook navigation"><a href="index.html">← QASR handbook</a><a href="configuration.html">Defaults &amp; atom styling</a><a href="batch.html">Batch rendering &amp; speed</a><a href="movies.html">Movies &amp; reaction paths</a></nav>
<header class="guide-header"><p class="eyebrow">FIND THE CONTROL YOU NEED</p><h1>All options &amp; examples</h1><p class="lede">Choose what to draw, how it looks, and how to save it. Browse every command-line control, then open an option for its values, default and a practical example.</p></header>
<div class="callout"><strong>Run the examples</strong><p>Build or install <code>qasr</code> and place it on your PATH, or replace <code>qasr</code> below with <code>build-release/qasr</code>. Extract the <a href="downloads/qasr-examples.zip" download>example kit</a> into <code>examples/</code> and run from its parent directory. The examples skip personal preferences for reproducibility. Explicit command-line flags override a JSON recipe.</p></div>
<section id="visual-recipes" class="doc-section"><h2>Start with a visual recipe</h2><p>These are actual renders from the downloadable inputs and recipes. Each recipe fixes the palette, camera and dimensions shown.</p><div class="options-recipes">'''
    intro=intro.replace('<head>', '<head><meta name="qasr-version" content="'+version+'">', 1)
    chunks=[intro]
    for r in visuals:
        extra=''
        if r['id']=='zeolite-faces':
            extra='<a href="downloads/tiles.json" download>Face polygons ↓</a>'
        elif r['id']=='single-polygon':
            extra='<a href="downloads/single-face.json" download>Face polygon ↓</a>'
        chunks.append(f'<article class="image-card"><a href="{r["image"]}"><img src="{r["image"]}" width="900" height="900" alt="{e(r["caption"])}" loading="lazy"></a><div class="recipe-body"><h3>{e(r["title"])}</h3><p>{e(r["caption"])}</p><div class="recipe-links"><a href="downloads/{r["source"]}" download>Input ↓</a><a href="{r["recipe"]}" download>Recipe ↓</a>{extra}</div><pre><code>{e(r["command"])}</code></pre></div></article>')
    chunks.append('''</div><p class="small">Zeolite geometry is derived from the <a href="downloads/zeolite-provenance.md">cited IZA LTA framework</a>. Download the example kit to get all files together. The <a href="index.html#periodic">periodic-cell examples</a> show connections to included neighboring cells.</p></section>
<section id="options" class="doc-section"><h2>Explore every command-line option</h2><div class="option-controls" hidden><label for="option-search">Search by feature, flag or word<input id="option-search" type="search" placeholder="Try bond, transparent, polygon or --view" autocomplete="off"></label><label for="option-group">Topic<select id="option-group"><option value="">All topics</option>''')
    for key,title in GROUPS.items():chunks.append(f'<option value="{key}">{e(title)}</option>')
    chunks.append('</select></label><button type="button" id="option-clear" class="button quiet">Clear filters</button></div><p id="option-count" role="status" aria-live="polite">All options are listed below.</p><nav class="option-topics" aria-label="Option topics">')
    for key,title in GROUPS.items():chunks.append(f'<a href="#{key}">{e(title)}</a>')
    chunks.append('<a href="#json-reference">JSON settings</a><a href="#face-fields">Polygon fields</a></nav><p id="option-empty" hidden>No options match. Try a shorter term or choose all topics.</p>')
    for key,title in GROUPS.items():
        chunks.append(f'<section class="option-group" id="{key}" data-group="{key}"><h3>{e(title)}</h3>')
        for item in [i for i in CATALOG if i['group']==key]:
            anchor='option-'+item['flags'][0].lstrip('-') if item['flags'][0]!='--' else 'option-terminator'
            labels=' / '.join(item['flags'])
            chunks.append(f'<details class="option-card" id="{anchor}" data-option><summary><span><code>{e(labels)}</code><strong>{e(item["title"])}</strong></span><span class="option-expand" aria-hidden="true">+</span></summary><div class="option-body"><p>{e(item["description"])}</p><p class="option-values"><b>Values &amp; default:</b> {e(item["values"])}</p><pre><code>{e(item["example"])}</code></pre>')
            if item['note']:chunks.append(f'<p class="small">{e(item["note"])}</p>')
            chunks.append(f'<a class="option-permalink" href="#{anchor}">Link to this option</a></div></details>')
        chunks.append('</section>')
    chunks.append('</section><section id="json-reference" class="doc-section"><h2>JSON settings, including the extra controls</h2><p>A recipe is an object with sections such as <code>image</code>, <code>camera</code> and <code>geometry</code>. The table lists every top-level rendering setting and its nested fields. Open the linked option for its explanation; JSON-only controls include layer fading, framing margins, element palettes, bond cutoffs and individual atom styles.</p><p>Use <code>qasr --schema config</code> for complete structural limits and <code>--validate-config</code> for input-dependent checks. Runtime paths, frame selection and threads belong in a <a href="schemas/job.schema.json">job</a>, rather than a rendering recipe. <a href="configuration.html">Read the configuration guide →</a></p><div class="table-wrap"><table><thead><tr><th>JSON setting</th><th>Purpose</th><th>Example value</th></tr></thead><tbody>')
    schema=json.loads((ROOT/'schemas/config.schema.json').read_text());defaults=json.loads(subprocess.check_output([exe,'--no-user-config','--dump-config']))['config']
    for name,prop in schema['properties'].items():
        fields=[(name+'.'+k,v) for k,v in prop['properties'].items()] if 'properties' in prop else [(name,prop)]
        for path,field in fields:
            if path in EXTRA_FIELDS:
                text,value=EXTRA_FIELDS[path];description=e(text)
            else:
                flag=CLI_FIELDS[path];item=next(i for i in CATALOG if flag in i['flags'])
                description=e(item['description'])+' <a href="#option-'+item['flags'][0][2:]+'">'+e(flag)+' example →</a>'
                value=defaults
                for part in path.split('.'):value=value.get(part) if isinstance(value,dict) else None
                value=json.dumps(value,separators=(',',':')) if value is not None else '[0,1,0]'
            chunks.append(f'<tr data-json-field="{e(path)}"><td><code>{e(path)}</code></td><td>{description}</td><td><code>{e(value)}</code></td></tr>')
    for path,(description,value) in EXTRA_FIELDS.items():
        if '<symbol>' in path or '[]' in path:
            chunks.append(f'<tr data-json-field="{e(path)}"><td><code>{e(path)}</code></td><td>{e(description)}</td><td><code>{e(value)}</code></td></tr>')
    chunks.append('</tbody></table></div><h3>Change colors, connectivity and fading together</h3><pre><code>'+e(json.dumps({'image':{'style':'illustrated','depth_fade':0.6},'camera':{'view':[0,0,1],'up':[0,1,0],'margin':0.08},'elements':{'Si':{'color':'#70a9d2','radius_A':0.32},'O':{'color':'#ed7667','radius_A':0.23}},'bond_cutoffs_A':{'Si-O':1.9},'geometry':{'bond_radius_A':0.075,'bond_colors':'atoms'}},indent=2))+'</code></pre><p>Save as <code>my-style.json</code>, then run <code>qasr examples/ring.xyz --config my-style.json -o ring.png</code>. Colors and display radii affect appearance; pair cutoffs determine connectivity. This cell-free ring uses an explicit view and up direction.</p></section>')
    face_schema=json.loads((ROOT/'schemas/faces.schema.json').read_text())['properties']['faces']['items']['properties']
    assert set(face_schema)==set(FACE_HELP), 'Undocumented face fields'
    chunks.append('<section id="face-fields" class="doc-section"><h2>Define a polygon face</h2><p>Use a JSON object with a <code>faces</code> array. Each entry describes one ordered boundary using either explicit vertices or atom indices. Both directions around the boundary are accepted. Include oxygen vertices when the displayed Si–O ring needs to follow its actual boundary.</p><div class="table-wrap"><table><thead><tr><th>Face field</th><th>Meaning</th></tr></thead><tbody>')
    for name,description in FACE_HELP.items():chunks.append(f'<tr><td><code>{name}</code></td><td>{e(description)}</td></tr>')
    chunks.append('</tbody></table></div><pre><code>'+e((downloads/'single-face.json').read_text().strip())+'''</code></pre><p>Save this face file next to <a href="downloads/ring.xyz" download>ring.xyz</a>. Use the <a href="downloads/single-polygon.json" download>single-polygon recipe</a> for the exact translucent example above. Face-field constraints can be inspected in <a href="schemas/faces.schema.json">the face schema</a>.</p>
<h3 id="face-geometry">Coordinates, periodic offsets and transparency</h3>
<p>Explicit Cartesian vertices are in angstroms and are not wrapped. Fractional vertices require all three cell vectors. Atom indices refer to the full, zero-based source list printed by <code>--inspect</code> for the selected frame. Atom-based vertices use wrapped primary-cell positions plus their <code>cell_offsets</code>; give one offset for every index when a ring crosses a cell boundary.</p>
<pre><code>{"faces":[{"atom_indices":[0,3,7,4],"cell_offsets":[[0,0,0],[1,0,0],[1,0,0],[0,0,0]],"color":"#54b6d880"}]}</code></pre>
<p>This illustrates offset syntax; use indices that exist in your input. Faces translate as complete polygons during repetition. They are not inferred, clipped to the cell, or recolored as periodic atom copies. Include the surrounding atom copies that your intended boundary needs. CIF must first be converted to a supported geometry format.</p>
<p>Simple concave loops are supported. Nonplanar loops become a piecewise planar surface through the original 3D vertices; degenerate or self-intersecting projected loops fail. Split a folded shape into separate faces. Fill and outline colors are independent of the atom style and tone.</p>
<p>Faces render on both sides and blend in sample-depth order with one another and the nearest atom/bond surface. This does not provide transparent-atom volume rendering. Opaque faces hide label anchors; translucent faces allow labels. Internal triangulation edges do not create outlines or double-alpha seams.</p>
<h3 id="face-configuration">Load faces in recipes and worker jobs</h3>
<p>Use <code>--faces FILE</code>, root-level <code>faces: [...]</code>, or root-level <code>faces_file: "polygons.json"</code>. A single configuration layer cannot contain both face forms. Relative paths resolve against the containing config/job file, or the working directory for stdin/worker requests. Higher layers replace the array; an empty array clears it; CLI faces are the final override. Personal defaults cannot store faces.</p>
<p>Dumps contain inline definitions. Workers reload referenced face files and invalidate scene caches when those definitions change. <code>--inspect-scene</code> exposes face vertices, origins and boundary flags. To check atom indices and triangulation without writing an image, supply your input with <code>--validate-config</code>.</p>
<h3 id="face-limits">Limits and supported combinations</h3>
<p>Face JSON is limited to 1 MiB and 32 nesting levels: at most 4096 faces, 3–64 vertices per face, 65536 input vertices, 262144 expanded triangles, 64 overlapping face layers per sample and 200 million tile/sample candidates. Allocations share the renderer’s 1 GiB allocation budget. Unknown/duplicate fields and nonfinite numbers fail explicitly.</p>
<p>Faces and atom-colored bonds require the Z-buffer backend. Adaptive AA uses the full 2× grid for faces and styled cell edges; native, 2× and 4× sampling remain available. Both endpoint atoms are mandatory for every displayed bond: expansion restores connections to included neighbors, while connections beyond the outermost displayed cell are omitted.</p></section><footer><a href="index.html">QASR user handbook</a><a href="batch.html">Batch rendering &amp; speed →</a></footer></main><div id="options-toast" role="status" aria-live="polite"></div></body></html>''')
    (site/'options.html').write_text('\n'.join(chunks)+'\n')
    # Keep all visual recipe inputs together and preserve source attribution.
    (downloads/'zeolite-provenance.md').write_text('''# LTA example geometry

Source: IZA Structure Commission Database of Zeolite Structures.
Credit: Ch. Baerlocher and L. B. McCusker / IZA-SC.
Accessed: 2026-10-31.

Framework: https://europe.iza-structure.org/IZA-SC/framework_coord.php?ID=138
Original CIF: https://europe.iza-structure.org/IZA-SC/download_cif.php?ID=138
The supplied LTA.cif preserves the original source header.

The source is DLS76-optimized idealized pure-silica geometry, with a cubic
cell length of 11.919 Å. It is not a measured sample or simulation result.

cage.xyz includes the periodic images needed for one complete alpha-cage
boundary: 48 Si, 72 bridging O and 144 Si–O bonds. tiles.json defines its
26 faces. Every face follows the actual alternating Si–O boundary.

ring.xyz isolates one eight-membered Si ring and its eight bridging O atoms,
followed by a rigid translation and rotation. single-face.json follows all
16 atom positions. The ring is a cell-free molecule in the rendered example.

The renderer fills the supplied polygons; it does not infer zeolite tilings
or import CIF directly. The original CIF is included as source attribution.
''')
    extra_files=['cage.xyz','ring.xyz','tiles.json','single-face.json','LTA.cif','zeolite-provenance.md',
                 'reaction.xyz','reaction.json',
                 'source-highlight.json','cell-highlight.json','highlight-job.json','worker-requests.jsonl',
                 *[r['id']+'.json' for r in visuals]]
    kit=downloads/'qasr-examples.zip'
    with zipfile.ZipFile(kit) as archive: names=set(archive.namelist())|set(extra_files)
    with zipfile.ZipFile(kit,'w',zipfile.ZIP_DEFLATED) as archive:
        for name in sorted(names):archive.write(downloads/name,name)
    examples=json.loads((site/'examples-manifest.json').read_text())
    examples['downloads'].update({name:sha(downloads/name) for name in [*extra_files,'qasr-examples.zip']})
    (site/'examples-manifest.json').write_text(json.dumps(examples,indent=2)+'\n')
    manifest=dict(version=version,executable_sha256=sha(exe),documented_flags=sorted(covered),catalog=CATALOG,images=visuals,
                  files={str(p.relative_to(site)):sha(p) for p in [site/'options.html',site/'options.js',*[downloads/n for n in ['cage.xyz','ring.xyz','tiles.json','single-face.json','LTA.cif','zeolite-provenance.md']]]})
    (site/'options-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(f'Built {len(CATALOG)} option entries covering {len(flags)} help flags and {len(visuals)} visual recipes.')


if __name__=='__main__':main()
