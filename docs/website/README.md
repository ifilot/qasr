# Standalone QASR handbook

QASR means **Quick Atomic Structure Renderer**; the current executable is `qasr`.
Open `index.html` directly or serve this directory using a static HTTP server.
HTML, CSS, JavaScript, images and downloads are local; there are no runtime CDN,
font, package or backend requirements. The command builder, search and lightbox
also work from `file://` URLs. Configuration, batch and synthetic-workload guides
are included as standalone HTML pages.

```sh
python3 -m http.server 8000 --directory docs/website
# Open http://localhost:8000
```

The handbook documents QASR **v0.5.0**, including generalized inputs, optional JPEG/WebP output and ORTEP/BALSAC styles. Its
`qasr-source.tar.gz` download builds `qasr`. The separately labeled immutable
v0.3.0 archive, `vasp-render-source.tar.gz`, builds the provisional old command;
it is preserved unchanged for historical use. Source downloads omit the website
(`--without-website`) to avoid recursive archives; full source packaging includes
it. Licensing, tests and source/input provenance accompany both forms.

To regenerate source downloads, 14 actual rendered images and the example kit:

```sh
python3 tools/package_source.py --without-website --output artifacts/website-source
python3 tools/build_documentation.py /path/to/build-release/qasr \
  --source-archive artifacts/website-source/qasr-0.5.0.tar.gz
python3 tools/build_configuration_documentation.py /path/to/build-release/qasr \
  --source-archive artifacts/website-source/qasr-0.5.0.tar.gz
python3 tools/check_documentation.py /path/to/build-release/qasr
```

The original v0.3.0 archive and `legacy-v0.3.0-source-manifest.json` must remain
unchanged. Current manifests record executable, command, input/image and source
hashes. Builders disable implicit user preferences for reproducibility. Keep
source guides and their HTML copies consistent, then regenerate manifests after
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
