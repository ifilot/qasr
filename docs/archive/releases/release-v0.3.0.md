# v0.3.0 release — 2026-10-29

> Historical record. Commands, limits and validation results describe the original
> development/release context. Use the [current documentation index](../../README.md)
> for supported behavior and setup.

v0.3.0 closes M7, M8 and the standalone handbook checkpoint. It includes:

- Optional streaming libxml2 vasprun.xml input, complete-document validation,
  ionic-frame/finalpos selection and pinned VASP relaxation/MD/static fixtures.
- Centered xy periodic context with a full-color primary cell, primary outline
  and muted/greyscale image atoms; skewed-cell bonds and original alpha/radii.
- An offline-ready handbook with 12 program-generated images, verified inputs,
  source download, command builder, references and GitHub Actions Pages hosting.

Existing renderer/AA/PNG/thread defaults remain the validated M4–M6 selections.
All 20 default reference images remain pixel-identical in the fixed environment.
Full default Release and all-feature leak-enabled address/undefined-behavior
checks cover 89/93 test groups across 8/10 CTest entries. Deployment acceptance
covers private-prefix installation, installed XML/annotations, package hashes,
offline source rebuild/tests and a serial/libpng/XML-disabled fallback.

```sh
cmake -S . -B /tmp/qasr-v0.3.0-release -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/qasr-v0.3.0-release -j 10
ctest --test-dir /tmp/qasr-v0.3.0-release --output-on-failure
python3 tools/validate_deployment.py /tmp/qasr-v0.3.0-release \
  --output artifacts/v0.3.0/deployment/report.json
```

Release/ASAN logs, generated archives and the audit are retained locally under
`artifacts/v0.3.0/`. Images, download checksums, command provenance and source
licensing accompany the handbook. The website's source download is built with
`tools/package_source.py --without-website` to avoid embedding the website into
its own source archive. Full release source packaging still includes the site.

WSL is the user-approved deployment/performance environment. Shared system
runtimes are required. Broad OUTCAR/real-MD/version support, layered transparent
geometry and universal cross-host timing/pixel guarantees are not claimed.

The user authorized this release commit/push. M9 user defaults, typed atom
selectors and the machine contract follow this checkpoint as separate work.
The optional M10 batch/MCP work remains conditional on a repeated-input or
integration workflow; a network server is not a prerequisite.
