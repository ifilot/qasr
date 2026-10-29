# Standalone vasp-render handbook

Open `index.html` directly in a browser, or serve this directory with any static
HTTP server. HTML, CSS, JavaScript, images and downloads are local; no external
fonts, CDNs, package install, build step or backend is required. Search, examples,
the command builder and image lightbox also work with `file://` URLs. External
links are optional project-source links, not runtime dependencies.

```sh
python3 -m http.server 8000 --directory docs/website
# Open http://localhost:8000
```

The handbook covers the v0.2.0 release plus M7 XML and M8 periodic-context
development. Availability is explained on the page. The included development
source download is the previously validated M8 source archive, renamed for the
handbook; its bytes and SHA-256 are unchanged. It is not a newly tagged release.
The nested source snapshot deliberately predates the website so it does not
recursively package the website into its own download.

The illustration manifest records the commands, original input hashes, PNG
hashes, build capabilities and executable SHA-256. All 12 examples are program
output. The source/input downloads contain project license and provenance.

To regenerate example images and the example kit using a current Release build:

```sh
python3 tools/build_documentation.py /path/to/build-release/vasp-render
python3 tools/check_documentation.py /path/to/build-release/vasp-render
```

To intentionally replace the downloadable source snapshot, pass a validated
source archive made before embedding these documentation assets:

```sh
python3 tools/build_documentation.py /path/to/build-release/vasp-render \
  --source-archive artifacts/m8/deployment/archives/vasp-render-0.2.0.tar.gz
```

Publishing uses GitHub Actions from `master`, through
`.github/workflows/pages.yml`. The workflow validates and uploads this directory
directly, then deploys it with the Pages artifact actions. No publishing branch
is needed. GitHub Pages serves the site at
`https://ifilot.github.io/qasr/`. Local website archives, browser
screenshots, the print preview PDF and validation evidence belong in
`artifacts/website-validation/` or `artifacts/`, not in this source directory.

Push changes under `docs/website/` to `master` to publish an update. To rerun
publication without changing files:

```sh
gh workflow run pages.yml --ref master
```

The repository's Pages source must be **GitHub Actions**. The workflow has
contents:read, pages:write and id-token:write permissions and deploys through
the `github-pages` environment. Local source/example validation is supplemented
by an executable-free asset/link/download check in CI.
