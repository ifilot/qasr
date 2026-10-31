# Standalone QASR handbook

QASR means **Quick Atomic Structure Renderer**; the current executable is `qasr`.
Open `index.html` directly or serve this directory using a static HTTP server.
HTML, CSS, JavaScript, images and downloads are local; there are no runtime CDN,
font, package or backend requirements. The command builder, search and lightbox
also work from `file://` URLs. Configuration, batch, movie, options and release guides
are included as standalone HTML pages. The options guide covers polygon files,
periodic offsets and geometry limits; the movie guide covers builds and playback.
Release notes summarize compatibility changes and validation.

```sh
python3 -m http.server 8000 --directory docs/website
# Open http://localhost:8000
```

The handbook documents QASR **v0.6.0**, including polygon faces, atom-colored bonds, cell-line styles and
optional FFmpeg movies, alongside all existing input formats and drawing styles. Its
`qasr-source.tar.gz` download builds `qasr`. The separately labeled immutable
v0.3.0 archive, `vasp-render-source.tar.gz`, builds the provisional old command;
it is preserved unchanged for historical use. Source downloads omit the website
(`--without-website`) to avoid recursive archives; full source packaging includes
it. Licensing, tests and source/input provenance accompany both forms.

To regenerate source downloads, all 26 rendered images, the options guide and the example kit:

```sh
python3 tools/package_source.py --without-website --output artifacts/website-source
python3 tools/build_documentation.py /path/to/build-release/qasr \
  --source-archive artifacts/website-source/qasr-0.6.0.tar.gz
python3 tools/build_configuration_documentation.py /path/to/build-release/qasr \
  --source-archive artifacts/website-source/qasr-0.6.0.tar.gz
python3 tools/build_options_documentation.py /path/to/build-release/qasr
python3 tools/check_documentation.py /path/to/build-release/qasr
```

The original v0.3.0 archive and `legacy-v0.3.0-source-manifest.json` must remain
unchanged. Current manifests record executable, command, input/image and source
hashes. Builders disable implicit user preferences for reproducibility.
All six ORTEP/BALSAC style and tone examples are included in image regeneration
and provenance checks, alongside the core examples and atom-highlight images.
The options builder checks the current help flags against its catalogue, renders
six visual recipes and includes their inputs/settings in the example kit. It
must run after both other builders so the kit and download hashes stay current.
Keep source guides and their HTML copies consistent, then regenerate manifests after
editing pages referenced by those manifests.

Publishing uses `.github/workflows/pages.yml` on `master`, with GitHub Pages
configured to use **GitHub Actions**. CI validates the standalone assets before
upload/deployment. No publishing branch is needed. The site is
`https://ifilot.github.io/qasr/`.

```sh
gh workflow run pages.yml --ref master
```

Offline website ZIPs, screenshots, print previews, test data and validation logs
belong in `artifacts/`, not in this source directory.
