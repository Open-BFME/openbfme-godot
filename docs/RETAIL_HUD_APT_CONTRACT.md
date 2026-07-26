# Retail Men HUD source contract

The private Men/Fords HUD profile has two deliberately separate resources:

- `men-hud-apt-runtime-bundle` selects the exact 261-source APT, TGA, RU,
  DAT, CONST, and WND closure, including all five unconditional InitialSetup
  movie loads, for conversion into the Palantir runtime contract and atlases.
- `men-hud-font-albertus-mt` copies the exact retail Albertus MT winner to
  `assets/ui/palantir/fonts/albertusmt-6a1990e17f14.otf`.

The font winner is `albertusmt.otf` from `_patch103.big`, precedence `0`, entry
offset `2510`, with 24,712 bytes and SHA-256
`6a1990e17f14ce5be199dde10f56dac3efd66aaa8e91d46119952cf55a9d9ba0`.
The profile planner checks all of those fields against the effective-assets
manifest, rereads and hashes the private file, and requires the catalog winner
to agree before it emits the resource.

Palantir font character `63` names `Albertus MT` and has no embedded glyph
payload. The HUD plan therefore binds `palantir:63` to this exact font resource;
no operating-system font, generated glyph set, or substitute is permitted.
Runtime `FontFile` loading and dynamic text-value providers are separate gates
and remain fail-closed until their own implementation and rendered proof pass.

The deterministic HUD fragment contains 2 resources and selects 262 sources:
261 in the runtime bundle and the separate Albertus font source. The four newly
sealed movie archives contribute exactly 72 files and 3,405,888 bytes; the
runtime bundle is 10,700,284 bytes with source aggregate
`f62347fb78065726715618ed9c73f152c678fec5646ddf7b0855825d1cb23599`.
Two private planner runs produced identical files: plan SHA-256
`b2478347d3e0dd9fe634649ad86abb4e17e4c017df14e2894152ffc4e141bc2f`,
profile SHA-256
`1cc78a8b3a2c08b655862387e569301ec592908379d66f543b8f49919fd84914`,
and canonical plan aggregate SHA-256
`d8850c6033b8ae3041e044246ab216550b6eabb1d8cce0397006f936066c36c4`.
The `<BFME2>` import plan resolves all 374 completion resources with zero
missing required inputs. The completion profile remains 374 resources / 2,532
unique retail files and now has SHA-256
`cc5af254e0787cf135bd1cf8574b94dd19991741a6eda6ccc346aa304b78c588`.
This is source/converter-closure proof, not a claim that the runtime renderer
callbacks or external-movie target attachment are complete.
