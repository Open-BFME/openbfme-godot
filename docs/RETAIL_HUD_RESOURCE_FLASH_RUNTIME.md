# Retail HUD resource-flash runtime

The bounded HUD converter and Godot runtime execute the exact BFME2 1.06
Palantir resource-flash entry without adding a general ActionScript VM.

The only public runtime input is `PlayCommandPointEffect()`. It rewinds the one
placed `CommandPointsFlash` instance (`palantir:309`) to label `_go`, frame 8,
then executes the byte-identity-bound `palantir:332504` entry. That 26-byte
program first plays the current timeline and then exposes one exact audio-event
intent for `Gui_PalantirResourceBarFlash` through `FSCommand:PlaySound`.
Retriggering rewinds the same visual instance and emits another intent; it does
not allocate a parallel visual effect.

The runtime deliberately does not auto-trigger from resources or command-point
values. The two stripped native counter aliases that reach
`PlayCommandPointEffect()` have not been named dynamically. It also does not
invent coalescing, overlap, or voice-stealing behavior for repeated audio
requests. Those unknowns replace the former generic unsupported-action blocker
with two narrow capture blockers:

- `resource-flash-native-trigger-capture-not-passed`
- `resource-flash-mixer-overlap-capture-not-passed`

Static identities retained in the generated contract include:

- entry script SHA-256
  `0b966556e6fc10d1eaa5c129999f31e185b634425298b7bdaf21b6dd26aeb999`
- trigger-body SHA-256
  `a5b9a91b9ad21d12bced1a7d9f94c803d2abbb5fe542646356fdc90663f47788`
- 58-frame timeline SHA-256
  `f2254f867b5f59070284fd2f028d5f4e4d787f09af9f59220491559053b069d6`
- placement-record SHA-256
  `6673eea4c330f20d073788d1f1bc36f50ba4b456a73a7ff1e40477da6b93c527`
- audio leaf SHA-256
  `f2d3aff531ecfd3616069d53551823f92aee92f009382d3bf39d4ec8e2eca350`

The conversion changes the production inventory from 60 to 61 supported
ActionScripts and from 14 to 13 unsupported ActionScripts. Because one generic
blocker is replaced by two evidence gates, the production blocker total changes
from 22 to 23. This is a more precise fail-closed contract, not a parity claim.

Verification uses a freshly generated deterministic A/B private bundle, the
focused converter tests, the legal-safe Godot runner, the private-contract
runner, and a headless Godot editor compile. Private payload stays under
`.private/scratch/hud-resource-flash-runtime`.
