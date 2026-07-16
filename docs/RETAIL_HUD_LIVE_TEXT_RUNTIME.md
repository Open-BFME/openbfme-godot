# Retail Palantir live-text runtime

The bounded HUD converter and Godot runtime now execute the three BFME2 1.06 Palantir live-text bindings with the exact copied Albertus MT retail font. This is a usable private implementation, not a rendered-parity claim: one mandatory blocker retains all seven opaque font-backend/GPU capture gates.

## Exact live values

| APT variable | Runtime inputs | Exact formatting |
| --- | --- | --- |
| `$PalantirResources` | `resources` | decimal integer; one space when negative |
| `$PalantirResourceMultiplier` | `resourceMultiplier` | `x%g`; one space when exactly `1.0` |
| `$PalantirCommandPoints` | current and cap | `current/cap`; current only when cap is negative; one space when current is negative |

`RetailHud.set_resources()` forwards resources, current command points, cap, and an explicit normal-slice multiplier of `1.0`. Placeholders `999999`, `x99`, and `999/999` are never runtime fallbacks. Missing targets and non-finite multipliers fail closed.

The byte-identity-bound initialize programs are:

- `palantir:clip-event:375628` → `$PalantirResources`
- `palantir:clip-event:375640` → `$PalantirResourceMultiplier`
- `palantir:clip-event:375652` → `$PalantirCommandPoints`

Together with the two existing typed initialize programs, this produces 5 supported clip programs out of 6 and 27 executable initialize events out of 28. The one remaining event is the exact unload lifecycle blocker.

## Exact font containment

The converter resolves `albertusmt.otf` beside the sealed 261-source APT tree without adding it to that already-sealed source aggregate. It verifies 24,712 bytes and SHA-256 `6a1990e17f14ce5be199dde10f56dac3efd66aaa8e91d46119952cf55a9d9ba0`, then copies it to:

`assets/ui/palantir/fonts/albertusmt-6a1990e17f14.otf`

Godot resolves that contained path through the mounted pack, verifies the same length and SHA-256, and loads it with `FontFile.load_dynamic_font`. The contract additionally pins Albertus MT Regular / `AlbertusMT`, CFF, 1,000 units per em, 298 glyphs, and zero bitmap strikes. There is no Arial, placeholder, or synthetic-glyph fallback.

## Layout and display order

The converter assigns one traversal-derived `displayOrder` across both triangles and text leaves. The runtime validates uniqueness, merges the inventories, and renders in that order. For the selected retail frame the exact traversal produces 31 items: triangle orders 0–27 followed by resource, multiplier, and command-point text orders 28, 29, and 30. This ordering follows the selected APT display-list depths; it is not a runtime append policy.

The runtime preserves the statically proven authored bounds, transforms, color, alignment mapping (`0=right`, `1=center`, `2=left`), vertical-center calculation, and integer truncation. Font size, glyph baseline, hinting, final blend, ancestor clipping, final composite pixels, and live backend font winner still require rendered evidence.

## Mandatory capture blocker

Exactly one `text-rendered-parity-capture-not-passed` blocker remains. It contains all seven required retail-vs-Godot gates:

1. Font-size device mapping.
2. Baseline and glyph origin.
3. Antialiasing and CFF hinting.
4. Final color, alpha blend, and gamma.
5. Ancestor clipping.
6. Final composite order pixels.
7. Runtime font winner.

The production contract therefore has 25 blockers and keeps `parityReady=false`. Removing, duplicating, weakening, or marking this blocker passed is rejected by the runtime.

## Verified result

- Source closure: 261 files, 10,700,284 bytes, unchanged aggregate.
- Output closure: 26 files: 24 atlases, one exact OTF, and one scene contract.
- Contract aggregate: `b5b3760858a51cb478ce4f9f26d68e4c71fcc24d66ea74c99e37245d93b1b794`.
- Serialized contract SHA-256: `3666f4732f2e2a211e5ee4cc7975399469573011c2ebfb7660755ecf98969ecd`.
- A/B bundle identity SHA-256: `478d5e6282650c46fc820e8490e7c19636fe8b4ad4e838caef5e710e27f2b52e`.
- Converter tests: 20 passed; Ruff clean.
- Godot legal runtime: 43/43.
- Godot private runtime: 70/70.
- Four-unit HUD forwarding: 61/61.
- Godot 4.7 editor compile: clean.

