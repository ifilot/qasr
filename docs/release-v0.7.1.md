# QASR v0.7.1 release — 2026-11-17

QASR v0.7.1 is a patch release that adds fixed camera framing, so that separate
renders can share one scale and position.

## Fixed framing

By default the camera fits each scene to its own projected bounds. Two optional
`camera` keys override this:

- `extent_A`: the number of ångströms spanned by the shorter side of the image
  area inside the margins. `0` (the default) restores automatic fitting.
- `center_A`: the world point (Å) placed at the image centre. `null` (the
  default) restores the centre of the projected scene bounds.

Either key can be used alone. Content outside the frame is clipped. Both keys
are part of dumped recipes, can be inherited from user preferences and are
honoured by trajectory and orbit movies. See
[Molecular orbital surfaces](molecular-orbitals.md#comparable-panels).

## Validation

- `tests/test_configuration.py` checks the resulting camera scale and centre,
  each key on its own, round-tripping through `--dump-config`, resetting an
  inherited value and rejecting invalid values.
- The complete CTest suite passes.
