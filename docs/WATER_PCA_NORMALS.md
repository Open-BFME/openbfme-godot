# Standing-water normal reconstruction

The fixed-input GPU regression is `godot/tests/water_normal_test.gd`. Run it
with a graphical Godot process: `godot --rendering-method gl_compatibility --path godot --script
res://tests/water_normal_test.gd`. It needs no retail data. It probes `n` in
the production shader by replacing only its final color/alpha writes.

The mapping was checked against the locally decoded retail `watershader.fxo`
using `tools/render/d3d9_disasm.py` (pixel blobs 0 and 3; bump/specular blobs
8/9). No retail bytes are included here. The BFME2 coefficient loader retains
mean plus three X/Y/Z basis groups; these two pixel programs consume only the
mean and the first two groups, in constant registers c0 through c6. The third
stored group must not be substituted for texture 3.

For PCA (blob 0), texture 1 sampled at the two UVs sums into basis 0..3,
minus 1. Texture 2 at those UVs sums into basis 4..7, minus 1. PCA_U (blob 3)
also adds texture 3 at UV0 to basis 0..3 and at UV1 to basis 4..7, subtracting
another 0.5 from each group. This is why texture 3 contributes to both dot
product groups rather than getting a separate coefficient group.

Both programs reconstruct `R = c0.xyz + (dot(c1,A), dot(c2,A), dot(c3,A)) +
(dot(c4,B), dot(c5,B), dot(c6,B))`. They normalize `4R + geometric_normal - 2`
with the effect's 1e-5 epsilon. PCA_U then blends toward (0,0,1) by
`saturate(noise.r * noise.a * 0.25)`, without renormalizing.

The synthetic fixture uses mean (0.5,0.375,0.25), distinct coefficient rows,
and constant RGBA textures (0.125,0.25,0.375,0.5), (0.625,0.75,0.875,1), and
(0.25,0.375,0.625,0.75). Instruction-trace intermediate results are:

| Technique | A | B | R | Normal before normalization |
| --- | --- | --- | --- | --- |
| PCA | (-.75,-.5,-.25,0) | (.25,.5,.75,1) | (.96875,.46875,.6875) | (1.875,-.125,1.75) |
| PCA_U | (-1,-.625,-.125,.25) | (0,.375,.875,1.25) | (1.09375,.3125,.75) | (2.375,-.75,2) |

The expected final normals are (0.73018858,-0.04867924,0.68150934) and
(0.69705699,-0.22012326,0.64949536); the latter includes the 0.0625 noise blend.
Unused coefficient rows contain large sentinel values to catch accidental
consumption. BumpSpecular uses bump RGB (.25,.75,.5) and scale .5, giving
(-.5,.5,1) before normalization and
(-0.40824829,0.40824829,0.81649658) afterward. The compatibility renderer writes the encoded normal directly to the framebuffer,
without the production color-output conversion. Dyadic inputs avoid texture
quantization changing the reference arithmetic. The GPU comparison allows
0.025 distance for framebuffer quantization; coefficient-group substitutions,
missing texture-3 contributions and missing noise blending exceed that bound.
