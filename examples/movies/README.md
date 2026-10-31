# Camera and reaction movies

These examples require a QASR build with `ENABLE_FFMPEG=ON`.

```sh
build-movies/qasr --job examples/movies/orbit-job.json
build-movies/qasr --job examples/movies/reaction-job.json
```

Each movie is written beside its job file. Override the destination with `-o`
when you prefer another directory. See [the movie guide](../../docs/movies.md)
for the command-line controls, optional dependencies and playback rules.

The orbit uses the complete LTA cage and oxygen-inclusive polygon boundaries
from [the zeolite examples](../zeolite/README.md), with their source attribution.

`reaction.xyz` is an illustrative H + H2 exchange path with 15 synthetic source
geometries. It has no calculated energies, forces or physical time scale.
`reaction.json` assigns three colors by stable source atom index so the
exchanging atoms are easy to follow. Three inserted geometries per interval
give 57 frames, played at 24 fps with one fixed camera.
