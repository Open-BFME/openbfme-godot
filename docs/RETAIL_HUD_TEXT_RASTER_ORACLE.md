# Retail HUD text raster and layout oracle

This oracle seals the BFME2 1.06 Palantir resource, multiplier, and command-point text inputs and the retail layout path without copying retail payloads into the report. It is not a claim of rendered parity: seven font-backend and compositor questions remain explicit capture gates.

## Exact source closure

| Source | Retail identity | SHA-256 |
| --- | --- | --- |
| `Palantir.apt` | `apt/palantir.big`, precedence 51, offset 3,204, 378,173 bytes | `c1f500847f0c77d4c6504edf79113b5723300165bebd42b4dafda479516f5140` |
| `Palantir.const` | `apt/palantir.big`, precedence 51, offset 381,380, 10,260 bytes | `f07e24e3b70e286d491652cc827aef904a2ccabf54107d4f1bfc3030beee8fd9` |
| `Palantir.dat` | `apt/palantir.big`, precedence 51, offset 391,640, 586 bytes | `d8e8964711e4061b0643dd0dd3de1876b7326cee6d60e11214793b5d483f3ae4` |
| `albertusmt.otf` | `_patch103.big`, precedence 0, offset 2,510, 24,712 bytes | `6a1990e17f14ce5be199dde10f56dac3efd66aaa8e91d46119952cf55a9d9ba0` |
| `game.dat` | BFME2 1.06 executable, 10,969,600 bytes | `f008b587570bad693981dc7218588c81d192a1e064b0f7f861539c51156a7640` |

The APT root is authored at 1024x768. Font character 63 is `Albertus MT`, has zero embedded glyphs, and selects the winning `albertusmt.otf`: Albertus MT Regular / `AlbertusMT`, CFF outlines, 1,000 units per em, 298 glyphs, and no bitmap strikes. No Arial, placeholder, or synthetic-glyph fallback is parity-legal.

## Exact Palantir text records

| Character | Runtime value | Bounds | Alignment | Placeholder | Instance path | Placement |
| --- | --- | --- | --- | --- | --- | --- |
| 130 | `$PalantirResources` | `[-2,-2,50.2000008,21.1499996]` | raw 0, right | `999999` | `layer:1:Palantir/102/5/3` | translate `(56.7000006,722.2000244)` |
| 132 | `$PalantirResourceMultiplier` | `[-2,-2,25.5,21.1499996]` | raw 2, left | `x99` | `layer:1:Palantir/102/9/3` | translate `(111.6000023,722.0000244)` |
| 134 | `$PalantirCommandPoints` | `[-2,-2,58.9500008,21.1499996]` | raw 1, center | `999/999` | `layer:1:Palantir/102/13/3` | translate `(141.6000023,722.2000244)` |

All three records request font height 14, opaque RGBA `(0,204,255,255)` / packed ABGR `0xffffcc00`, and are read-only, non-multiline, and non-wrapped. Their text leaves are depth 3 inside wrappers at depths 5, 9, and 13.

## Retail-static findings

- The external-font path keeps bounds, placements, and font height as authored pixel floats. The exact `0.05` twip conversion belongs to the embedded-glyph advance branch, which character 63 cannot use because `glyphCount=0`.
- Bounds are the horizontal-alignment and vertical-centering rectangle. Raw alignments are 0 right, 1 center, and 2 left; center consumes exactly half the remaining width.
- These three strings use `top + (boxHeight - measuredTextHeight) * 0.5`, then the host truncates final x/y toward zero before drawing.
- The source color is transformed on all four ABGR channels with multiplicative and additive components.
- The external draw call receives an integer origin, not the text rectangle as a leaf scissor. Preserve APT display-list depth order.

The sealed executable evidence is:

| Purpose | Virtual-address range | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| APT external text layout | `0x00AE1260..0x00AE1532` | 722 | `7fe24c251afa315e50ace1acb0aa93f42600379c82b197dcf6f4346280587c93` |
| Dynamic text draw dispatch | `0x00AE18A2..0x00AE18EE` | 76 | `26efd3866819c0b68adfccb3e4e688a0d5fcd4702f12991131f8a51305b27f32` |
| Host external string draw | `0x004A9570..0x004A97A9` | 569 | `88c5a87809067576f37b0ec5391638be95a4493e18dc809223a075aae1d2ad80` |
| Host external string creation | `0x004AA966..0x004AACA9` | 835 | `28686cca7802610bac59a6a180b00936cecaaae7980cadd03c5683a28f1a3aed` |
| Host ABGR transform | `0x004A90B0..0x004A9192` | 226 | `4714822691806323c6188c758994c54b74c6334489d077cd496e9f6a6c3330ea` |
| Excluded embedded-glyph advance | `0x00AE19D7..0x00AE1B31` | 346 | `8a8b03103edab42bfa3f9399d51e1397b9c2e9997a0a01f042d00a5082f37c48` |

## Rendered gates still required

Retail-static evidence does not reveal the opaque font backend and final GPU state. A retail capture must still seal:

1. Height-14 device mapping at authored and scaled viewports.
2. Baseline-relative glyph origin and pixel bounds.
3. CFF hinting and antialiasing for digits, `x`, slash, and space.
4. Final alpha blend, color space, and gamma over transparent and textured HUD pixels.
5. Whether a reachable ancestor mask clips these leaves.
6. Complete-frame composite order.
7. The live registered font handle selecting the source-identity winner.

Until those captures pass, `parityReady` remains false.

## Reproduce the payload-free contract

With retail payloads confined to `.private`:

```powershell
$env:PYTHONPATH = 'importer'
python -m openbfme_importer.retail_hud_text_raster_oracle `
  --apt .private/retail-work/cache/effective-assets/Palantir.apt `
  --const .private/retail-work/cache/effective-assets/Palantir.const `
  --dat .private/retail-work/cache/effective-assets/Palantir.dat `
  --otf .private/retail-work/cache/effective-assets/albertusmt.otf `
  --game-dat <BFME2>/game.dat `
  --opensage-root .private/scratch/opensage-hud-semantics `
  --output .private/scratch/hud-text-raster-oracle/contract.json
```

OpenSAGE commit `588ac477367a0022adf29f20a084e8873014e6ce` is observation-only. Its Arial substitution and forced-center behavior are explicitly rejected.
