# War of the Ring map presentation

Edition scope: shared BFME2 / RotWK presentation and data parsing. RotWK 2.01
map rendering was exercised; BFME2 1.06's presentation INIs were parsed with
its own mounted archives. BFME1 retains its recording-parser boundary and
S-5017; this does not add a BFME1 War of the Ring mode. No simulation rules,
strategic messages or save layouts change.

## Implemented

- Preserve effect names, geometry, opacity, LoadInShell and authored control
  points. Intensity and transform curves are separate. Unknown effect names
  fail with S-1321; transform curves are retained and reported as unported
  by the renderer. The map uses its campaign's named effect manager.
- Play selection and hover intensity curves; show territory geometry when
  ownership becomes unified. An unchanged state refresh neither restarts an
  effect nor retains another material. The setup preview respects LoadInShell.
- Draw icon models and banners through the full W3D material/house-colour
  path. UseHouseColor uses LivingWorldBannerColor. Local/allied visibility,
  selection/highlight predicates, sub-object filters, movement/rally helpers
  and building-production predicates reach the renderer. Node transforms and
  inherited visibility reach the native instance; freeing a node removes it.
- Draw the decorative LivingWorldAnimObject models and model animations,
  including their starting frame fractions. This restores the tower/pupil
  models and birds without enabling the separately commented EyeTower block.
- Play map-entry, army-selection, move-planning and ambient sounds through
  GameAudio. Positions stay in SAGE space. Leaving for a battle or the shell
  removes ambient handles. Unsupported zoom-conditioned sounds and
  NO_KILL/FADE_IN flags are reported by name.
- Reuse movement-line materials and avoid rebuilding unchanged line geometry.
  Keep unchanged building/plot nodes across strategic refreshes: native instance
  IDs are not reused, so recreating them each lockstep frame would retain
  tombstones throughout a long war. Appearance transitions replace only the
  affected icon.

## Source evidence

The installed RotWK image is subject to S-001; these data tables and code
addresses are in its original image sections. The clean BFME2 1.06 image
matches the repository's required MD5. No extracted data is committed.

| Data / behaviour | RotWK | BFME2 1.06 |
|---|---|---|
| SplineEffect fields | RW 0xC78E28 | BFME2 0x00C62148; `SplineEffectParseINIBlock.cpp` |
| Intensity / transform point parsers | RW 0x909FA5 / 0x909F2D; tables 0xC7AB18 / 0xC7AA78 | `Common` spline control-point readers |
| Named effect keys | RW 0xC4C7C0..0xC4C874 | `Rva003EE746ColorProvider.cpp`, dynamic effect-table initialization |
| Effect defaults | opacity 1, LoadInShell true | `Rva004E3CD0Ctor.cpp`, 0x004E3CD0 |
| Ambient sound fields | RW 0xC4DDD0; flags 0xDA6958 | BFME2 0x00C37938, `LivingWorldSound.cpp` |
| Decorative object fields | RW 0xC4E170 | BFME2 0x00C37CD8 |
| Icon fields / visibility | RW 0xC84998, 0xC84A58, predicate 0x5C4B26 | `LivingWorldIconSubObjectCtor.cpp`, `LivingWorldIconPredicate.cpp` |

The field tables establish the data contract. The existing W3DInstancer is
the rendering implementation. Linear intensity interpolation, territory
completion triggering, the icon colour source and rally-point placement are
presentation inferences, not a claim of byte-for-byte retail behaviour.

## Remaining S-1335 boundaries

The exact livingworld.txt camera/interpolation, spline easing and client-frame
quantization, transformed effect curves, conquered-region flare triggering,
interactive EyeTower scripting, animated-object mutation/xfer, shadows and
icon fades still need retail reader traces. IMPASSABLE and border-cloud layers
remain hidden; WaterShader.FX remains S-035's approximation. Existing lighting,
text-layer opacity and the solid home ring remain presentation inferences.

## Validation

The focused native checks load both editions' original presentation INIs,
check separate curves and all newly retained fields, reject unknown fields,
and preserve BFME1's recording boundary. The headless Godot regression checks
effect playback/material reuse, native instance transform/visibility/removal
and ambient handle cleanup. A fixed-state RotWK GPU comparison uses identical
seed, armies and camera; both builds report strategic hash 1227550449.
Captures belong in the pull request attachments, never this repository.
