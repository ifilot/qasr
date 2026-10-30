[![Compilation status](https://github.com/ifilot/qasr/actions/workflows/image-libs.yml/badge.svg?branch=master)](https://github.com/ifilot/qasr/actions/workflows/image-libs.yml)
[![Language: C11](https://img.shields.io/badge/language-C11-blue.svg)](CMakeLists.txt)
[![Version: 0.5.0](https://img.shields.io/badge/version-0.5.0-blue.svg)](https://github.com/ifilot/qasr/tree/v0.5.0)

# QASR: Quick Atomic Structure Renderer

QASR turns atomic structures into images from the command line, without a graphical interface.

- Read VASP, XYZ/Extended XYZ, Gaussian and AMS geometry files.
- Render molecules, crystals and slabs with configurable views, colors and drawing styles.
- Save PNG images, or enable optional JPEG and lossless WebP output.
- Automate repeated renders with JSON settings and batch jobs.

See the [user handbook](https://ifilot.github.io/qasr/) for detailed usage,
[supported input formats](docs/input-formats.md) and [drawing styles](docs/render-styles.md).
See the [v0.5.0 release notes](docs/release-v0.5.0.md) for changes and limitations.

## Examples

Rendered with QASR. Click an image to view it at full size.

| Illustrated cobalt surface | Palladium on ceria | Periodic surroundings |
| :---: | :---: | :---: |
| [![Illustrated top view of a cobalt surface](docs/website/assets/co-illustrated.png)](docs/website/assets/co-illustrated.png) | [![Palladium and adsorbates on a ceria surface](docs/website/assets/pd-ceria.png)](docs/website/assets/pd-ceria.png) | [![Colored cobalt unit cell surrounded by greyscale periodic copies](docs/website/assets/co-periodic-greyscale.png)](docs/website/assets/co-periodic-greyscale.png) |
| **ORTEP spherical style** | **BALSAC-inspired style** | **Dualtone dithering** |
| [![Cobalt slab drawn as white spheres with black great-circle sections](docs/website/assets/co-ortep.png)](docs/website/assets/co-ortep.png) | [![Cobalt slab drawn with smoothly shaded spheres and black silhouettes](docs/website/assets/co-balsac.png)](docs/website/assets/co-balsac.png) | [![Cobalt slab with navy and gold ordered-dither shading](docs/website/assets/co-balsac-dualtone.png)](docs/website/assets/co-balsac-dualtone.png) |

Find rendering recipes in the [handbook gallery](https://ifilot.github.io/qasr/#gallery)
and [style guide](docs/render-styles.md).

## Installation and compilation

On Debian or Ubuntu, install the build dependencies:

```sh
sudo apt update
sudo apt install build-essential cmake git pkg-config libpng-dev libdeflate-dev libxml2-dev
```

You need a C11 compiler with OpenMP support and CMake 3.16 or newer.
Clone and compile QASR:

```sh
git clone https://github.com/ifilot/qasr.git
cd qasr
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build --parallel
```

The executable is `build/qasr`. To install it for your user:

```sh
cmake --install build --prefix "$HOME/.local"
```

Add `$HOME/.local/bin` to your `PATH` to run `qasr` from any directory.

For JPEG and WebP output, install the optional libraries and rebuild:

```sh
sudo apt install libjpeg-dev libwebp-dev
cmake -S . -B build -DENABLE_JPEG=ON -DENABLE_WEBP=ON
cmake --build build --parallel
```

To run the test suite, install Python 3, configure with `-DBUILD_TESTING=ON`,
rebuild, and run `ctest --test-dir build --output-on-failure`.

## Minimal working example

From the repository directory, render the included sample structure:

```sh
./build/qasr tests/fixtures/POSCAR.skew -o structure.png
```

This creates `structure.png` in the current directory. Replace the sample path
with your own structure file to render it. Run `./build/qasr --help` for options,
or follow the [user handbook](https://ifilot.github.io/qasr/) for more examples.

## License

QASR is licensed under the [GNU General Public License v3.0 only](LICENSE).
See [third-party notices](THIRD_PARTY_NOTICES.md) for dependency licenses.
