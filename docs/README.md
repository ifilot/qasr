# QASR documentation

Start with the [quick start and installation](../README.md), the
[usage skill](../SKILL.md), or the [user handbook](https://ifilot.github.io/qasr/).
The guides below describe the current implementation. Changes since the latest
release are listed under Unreleased in the [changelog](../CHANGELOG.md).

| Task | Guide |
| --- | --- |
| Choose an input format, cell or periodicity | [Input formats](input-formats.md) and [vasprun.xml details](xml-input.md) |
| Choose atom/bond styles and dither tones | [Drawing styles](render-styles.md) |
| Add neighboring cells | [Periodic context](periodic-context.md) |
| Set defaults, atom overrides, JSON recipes or jobs | [Configuration](configuration.md) |
| Supply zeolite faces or colored/thicker bonds | [Polygon faces and bonds](polygon-faces.md) |
| Style cell edges and understand bond boundaries | [Cell lines and endpoint rules](cell-lines.md) |
| Make camera orbits or reaction movies | [Movies](movies.md) |
| Automate repeated renders | [Sequential JSONL worker](batch-worker.md) |

For the complete CLI/JSON catalogue and downloadable examples, use the handbook.
Its [offline copy](website/index.html) is included in full source packages and
installed documentation. The smaller source download offered by the website
omits the website itself; it retains these Markdown guides.

For development and measurements:

- [Architecture](design.md): modules, ownership, data flow and constraints.
- [Performance](performance.md): current benchmark tools, comparison rules and
  links to measured results.
- [Synthetic workloads](synthetic-workloads.md): reproducible input generation,
  geometry oracles and CLI/worker comparisons.
- [Handbook maintenance](website/README.md): regenerate images, downloads and
  provenance, validate the offline site and publish through GitHub Pages.

The latest release record is [v0.6.0](release-v0.6.0.md).
[Earlier releases, completed plans and historical validation](archive/README.md)
are kept separately. Their commands, environment limitations and measurements
describe the original work rather than current setup instructions.

When behavior changes, update the relevant current guide and its handbook/schema
counterpart. Keep dated results labeled with their original scope; link to newer
evidence rather than rewriting old measurements.
