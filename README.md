# gfx-research-shadow-auto-bias

[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C.svg)](https://en.cppreference.com/w/cpp/20)
[![OpenGL 4.6](https://img.shields.io/badge/OpenGL-4.6-5586A4.svg)](https://www.opengl.org/)
[![CMake](https://img.shields.io/badge/CMake-3.25%2B-064F8C.svg)](https://cmake.org/)

A terrain-shadow experiment that measures false shadows and lost contacts for depth bias with uniform and NDC-warped raster shadow maps.

[Method](#method) · [Build](#build) · [Controls](#controls) · [Experiments](#experiments) · [References](#references)

![Interactive raster shadow study](docs/assets/control-panel.png)

## Method

For normalized light-plane coordinate $p$, warp strength $\alpha$ and map width $N$, the local world-space texel width is approximately $2D/(Nw'(p))$, where $D$ is the light-plane half-width and

```math
w(p)=\frac{(1+\alpha)p}{1+\alpha|p|},\qquad
w'(p)=\frac{1+\alpha}{(1+\alpha |p|)^2}.
```

Let $\mathbf h$ contain the receiver's half-footprint from inverse-warped neighboring texels, $\mathbf g_r$ and $\mathbf g_c$ be receiver and caster gradients, $\mathbf e_r$ be the receiver-gradient error estimate, and $B_N$ be the numerical and quantization allowance.

```math
\begin{aligned}
B_F &= 0.9 (|\mathbf g_r|+\mathbf e_r)\cdot\mathbf h,\quad
B_\Delta=0.9|\mathbf g_r-\mathbf g_c|\cdot\mathbf h,\\
B_{\rm full}&=s(B_N+B_F+B_\Delta),\\
B_{\rm selective}&=s\left(B_N+B_F+B_\Delta\mathbf 1[B_\Delta\le B_F/2]\right).
\end{aligned}
```

Per-tap depth extrapolation predicts the receiver plane at each stored depth sample. The selective variant omits the mismatch allowance where a large gradient difference may signal a real occlusion boundary.

## Build

Requirements are CMake 3.25+, a C++20 compiler, Ninja, Python 3 with Jinja2, and a desktop OpenGL 4.6 Core driver. The initial CMake configuration downloads the pinned gfx-research-base revision and its dependencies. A local checkout can be supplied with `-DGFX_RESEARCH_BASE_LOCAL_PATH=../gfx-research-base`.

```powershell
python -m pip install jinja2
cmake -S . -B out/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/build --parallel
ctest --test-dir out/build --output-on-failure
```

On Linux, run the same build commands and omit `.exe` when launching an executable.

## Controls

Launch `shadow-auto-bias.exe` for the interactive terrain, split-screen method comparison and test previews.

**Tests** starts either of the ray-sampled publication suites or the raster smoke, quick and publication profiles. The raster profiles use the live raster map and a double-precision BVH oracle queried from the same camera rays. Each GUI raster run writes to a new folder under `results/raster`.

## Experiments

```powershell
.\out\build\shadow-auto-bias.exe --raster-study=smoke --results results/raster-smoke
python tools/analyze_raster.py results/raster-smoke
.\out\build\shadow-auto-bias.exe --raster-study=paper --results results/raster-paper
python tools/analyze_raster.py results/raster-paper
```

Use `quick` for 512² maps and two camera views. The paper profile covers four terrains, 1024² and 2048² maps, NDC 0 and 8, two sun elevations, and one tap or 3 × 3 PCF. The ramp calibrates constant, slope-scale and receiver-plane controls as well as the adaptive modes. Hills, terraces and the ridge are held out. For PCF, the error rates use a 0.5 mask threshold.

| Output | Contents |
|---|---|
| `raster_quality.csv` | Raw class counts and pixels excluded during camera-ray matching |
| `raster_calibration.csv` | Ramp calibration by balanced false- and missed-shadow error |
| `raster_held_out.csv` | False-shadow, missed-shadow and missed-contact rates for frozen parameters |
| `raster_timing.csv` | GPU elapsed time for the visibility pass on a prepared raster map |

The calibrated variants can move false-shadow and missed-shadow rates in opposite directions. Report both rates and the contact subset.

## References

1. Timo Aila and Samuli Laine. [Alias-Free Shadow Maps](https://doi.org/10.2312/EGWR/EGSR04/161-166). Eurographics Symposium on Rendering, 2004.
2. Lance Williams. [Casting Curved Shadows on Curved Surfaces](https://doi.org/10.1145/965139.807402). SIGGRAPH, 1978.
