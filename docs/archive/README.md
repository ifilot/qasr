# Historical documentation

These records preserve completed plans, earlier release scope and measurements
from their original environments. Their future-tense proposals, old executable
names and environment limitations are historical context. For current behavior,
use the [documentation index](../README.md), [architecture](../design.md),
[performance guide](../performance.md) and [latest release](../release-v0.6.0.md).

## Design and review records

| Record | Original scope |
| --- | --- |
| [Design history](design-history.md) | Original specification and accumulated implementation milestones, including superseded clipping rules |
| [Completed configuration/worker plan](next-iteration.md) | Design rationale completed in v0.4.0 |
| [Architecture and contract polish](code-polish.md) | Dated feature-complete cleanup and parity review before v0.6.0 |

## Validation and measurements

| Record | Original scope |
| --- | --- |
| [Initial rendering performance](performance-v0.1-v0.2.md) | Workstation baselines, renderer/encoder/thread selection and initial lab targets |
| [Rendering/annotation validation](m6-validation.md) | Early operating scope, resources and deployment |
| [XML validation](m7-xml.md) | Initial streaming-reader contract, fixtures and workstation measurements |
| [Periodic-context validation](periodic-context-validation.md) | Initial expanded-cell tests and measurements |
| [Configuration validation](m9-validation.md) | Initial layered-config, selectors and automation acceptance |
| [Worker/threading validation](m10-m11-validation.md) | Initial persistent-worker and OpenMP investigation |
| [Input-reader expansion](input-validation.md) | Initial generalized-reader review, including the constrained PR environment |
| [Drawing-style validation](render-style-validation.md) | Initial ORTEP/BALSAC/tone tests and the original build limitations |

Current optimization results remain linked from [performance](../performance.md).
The machine-readable `../performance-targets.json` remains at its existing path
because the lab acceptance tool reads it; its historical scope is explained by
the current performance guide.

## Earlier releases

- [v0.5.0](releases/release-v0.5.0.md): generalized inputs, optional image codecs
  and ORTEP/BALSAC styles.
- [v0.4.0](releases/release-v0.4.0.md): QASR naming, configuration, worker and
  threading investigation.
- [v0.3.0](releases/release-v0.3.0.md): XML, periodic context and the handbook.
- [v0.2.0](releases/release-v0.2.0.md): rendering/encoding experiments and hardening.
- [v0.1.0](releases/release-v0.1.0.md): original reference renderer.

The immutable legacy source download is retained byte-for-byte. Moving these
Markdown records does not alter historical tags or archived source contents.
