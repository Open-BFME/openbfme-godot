# Player house-colour rendering

Ordinary player colours now use the same per-texel recolouring path as custom
heroes. Live animated models, static draw modules (including building floors)
and map previews cache model variants by palette. House textures stored as
compiled JPEG plus PNG alpha are decoded through the existing packed-image
loader; ordinary model texture lookup precedence is unchanged. Changing a drawable's owner
colour switches its variant; a custom hero palette keeps precedence. The
`house_colors` option still disables this presentation feature.

Donor evidence from Open-BFME-2:

- `W3DModelDrawRva000B8FA1.cpp`, BFME2 VA 0x4B8FA1: read the controlling
  player's colour and pass kind 1, that colour and two zero colours to the
  render object's house-colour setter.
- `MeshClassRecolorPrototype.cpp`, BFME2 VA 0x54B8C0: a mesh part whose name
  begins with HOUSECOLOR, case-insensitive, receives primary-colour ambient
  and diffuse materials. The part is the text after the first dot, or the
  whole name when there is no dot. Our channel unpacking uses the existing
  target ARGB convention rather than the donor's address-derived ABI casts.
- `WW3D2/bfme2_mesh_material_pass.cpp` (MeshClass::RecolorHouseColor; an attempt then), BFME2 VA 0x54C470: recoloured textures
  are identified by texture and palette, and classic meshes use a second
  texture stage. This is an attempted reconstruction, not additional proof
  of every effect permutation.
- `ShaderClassApply.cpp:1425-1436` maps the house-colour detail operation (13) to
  texture-alpha blending. `MeshMatDescFXShaderConversion.cpp` maps detail mode 13 to
  HouseColorEnable. The RotWK effect path is recovered from `defaultw3d.fxo` (below).

The shared recolour routine was already ported from RotWK 0x531C77 (BFME2
0x5321A7): kind 1 multiplies the house texture's red byte by each colour
channel, shifts each product right by eight, and retains alpha. Kinds 2 and 3
also accumulate the green/blue channel contributions, with saturation. Baking
runs before texture filtering, using the existing mipmap behavior. Ordinary
player colours previously multiplied the base texture by the team colour under
the house texture's alpha, ignoring its RGB shading.

## The combine (recovered from RotWK's game.dat and defaultw3d.fxo)

The recoloured texture's alpha is the team region (the retail `HC_*.tga` files
carry a near-binary region mask in alpha and the shading to tint in red; the
recolour keeps alpha, RW 0x531C77). Retail mixes the recoloured texel in after
the diffuse light, so the team colour region is not lit (it does not darken in
shade). Which formula runs depends on how retail draws the surface:

- **Converted to DefaultW3D.fx** (the surface is single-pass, single-material,
  with a supported blend, no DetailAlpha and a supported mapper:
  `w3dfxconvert.h`; at the High and UltraHigh static levels, whose gamelod.ini
  sets ShaderMaterialReplacement; technique `Default`, `PS_Array`): the effect
  sets `HouseColorEnable`, `Texture_1` becomes the recoloured copy of `Texture_0`
  and the pixel shader index adds 144 (`NumTextures` 1 becomes 2,
  `SecondaryTextureBlendMode` becomes 3; the mesh's own second texture is
  dropped). The shader (`PS_Array[155]` and its shadow / shroud / fog variants)
  computes `2 * (lerp(t0 * diffuse light, hc * 0.5, hc.a) + specular * alpha) * cloud`.
- **Fixed function** (every other single-texture mesh): the recoloured texture
  replaces stage 1 and DetailColor becomes 13: BLENDTEXTUREALPHA of it over the
  diffuse-lit base, `lerp(2 * t0 * diffuse, hc, hc.a)`, alpha unchanged. The
  specular is not part of the term the region replaces: with the shader's secondary
  gradient set (`D3DRS_SPECULARENABLE`, RW 0x537AE5) it is added after the blend
  twice, `cur.a * spec` by stage 2 (`MODULATEALPHA_ADDCOLOR(CURRENT, SPECULAR)`,
  RW 0x53764F-0x5376A4, applied when the device has more than 2 stages, RW
  0x537973) and `spec` by the specular add; without the secondary gradient there is
  none (S-6023).

The two agree where the surface has no specular: the converted shader's
`hc * 0.5 * 2` is `hc`. Ordinary player colours (kind 1) and heroes' colour sets
(kind 3) use the baked texture; the map builder's per-instance colour recolours
the filtered texel as kind 1 (`(R * colour) >> 8`), which is an approximation
(retail recolours before filtering). Facts are pinned in
`tools/render/test_house_color_binary_facts.py`. The cloud factor is not applied
(S-390).

Remaining limits are explicit under S-119 / S-022: where the owner's colour comes
from for a map that names none, whether the unmatched Grid-family mapper class
changes a surface's class (S-6020), and the options' ShaderLOD not reaching the
renderer (S-3402). This is not a claim of pixel-identical retail rendering.

Validation includes `test_cah2.cpp` (palette precedence, packed black, name
classification and texel arithmetic), the shader-text suite, and
`godot/tests/player_house_color_test.gd`. The latter starts a seeded match,
checks distinct unit/building variants for two players, and reads back a real
unit's green-only baked house texture to verify its RGB channels.
