# Implementation details

Auto evaluates the receiver plane at each sampled texel center and adds a nonnegative tolerance. The full method uses the caster gradient packed in the center texel. The shadow-map builder stores an octahedral SNORM16 plane normal in one 32-bit integer per texel and writes the sentinel value `0x7fff7fff` for non-terrain casters.

| Component | Controlled experiment |
|---|---|
| Light projection | Orthographic directional light, half-width 128 m, depth range 512 m, centered separable warp |
| Geometry | Triangles from the exact input PGM samples and embedded box geometry |
| Stored depth | First geometric hit along the inverse-warped texel-center ray, rounded to D24 and represented as float |
| Receiver depth | Direct light-space position from a camera ray, rounded to float32 with a depth-space conversion allowance |
| Receiver plane | Quantized octahedral SNORM16 face normal with a propagated gradient error |
| Caster plane | Terrain-only packed light-space face plane, point-read at the center texel |
| Visibility | One tap in the paper, optional equal-weight 3 × 3 PCF in the program |
| Reference | Double-precision first positive ray hit toward the sun, skipping the receiver triangle and distances of 1 µm or less |
| Evaluation | Visible receivers with a light-facing cosine of at least 0.05, depth in (0,1), and max absolute unwarped NDC below 0.995 |

The slope baseline applies the stored caster slope in warped texel-depth units and models the constant-plus-slope policy in the comparison stage. Direct texel-center rays isolate the bias comparison from rasterization under the warp. Hardware raster bias and PCSS remain possible comparison cases. The archived profiles use fixed camera views.

## OpenGL application

The perspective camera rasterizes the terrain into receiver textures each frame. The light pass rasterizes a bounding rectangle for each warped triangle. It inverse-maps the center of each texel in that rectangle, tests coverage against the unwarped triangle, and interpolates depth. This keeps curved edges of warped triangles from being approximated by straight chords. Camera motion reuses the light map. A change to the terrain, light direction, map size or warp strength rebuilds it.

The live app, analytical OpenGL viewer and optional WebGL viewer use the same comparison shader in `shaders/shadow.frag`. The analytical viewers consume the ray-sampled map and receiver data. The live app supplies its rasterized camera and light targets. Both desktop views read caster planes as `R32UI`. The live display stores the shadow mask and diagnostic bias channel in an `RG8` texture.

The timing mode warms each arm with 40 draws, measures batches of 20 draws, and alternates order over 24 pairs. Draw queries cover the visibility shader. Display, framebuffer readback, and scene and map preparation occur outside the measured batches.

At matched parameters the extra gradient term can only turn a shadow into light or preserve the classification. The analysis verifies the resulting inequalities for false-shadow and missed-shadow counts per configuration and region.

## Reproduce the figures

With the pilot and production-size profiles in `runs/`, use

```bash
python -m pip install numpy Pillow matplotlib
python tools/analyze.py runs/pilot --production runs/production --out runs/analysis
python tools/figures.py runs/pilot runs/analysis --paper ../paper_shadow_auto_bias_en
```

For an archived comparison from this package, use `results/pilot` and `results/analysis` in the last command. The manuscript source can be built with `latexmk -pdf main.tex`.

