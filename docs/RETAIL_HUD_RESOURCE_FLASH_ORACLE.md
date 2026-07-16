# Retail HUD resource-flash oracle

This oracle closes the static semantics of the small blocked Palantir script
`palantir:332504` without adding a generic ActionScript VM or choosing a sound
by resemblance.

The only typed input is the zero-argument Palantir API call
`PlayCommandPointEffect()`. Its exact retail body calls
`CommandPointsFlash.gotoAndPlay("_go")`. `CommandPointsFlash` is the single
character-309 instance placed at depth 148. Its 58-frame timeline is stopped
at frame 0 (`_stop`), enters at frame 8 (`_go`), and returns through frame 57
with `goto _stop; play`. Entry-to-return spans 49 authored 33-ms frame
intervals; this is source timing metadata, not a claim about runtime wall-clock
timing. Replaying during that sequence rewinds the same visual instance; it
does not create a parallel effect.

The exact 26-byte frame-8 script first executes `play`, then calls
`_root.PlaySound("Gui_PalantirResourceBarFlash")`. The root bridge requires
the initialized Palantir state and emits the exact `FSCommand:PlaySound`
command with that event ID. BFME2 1.06 `game.dat` registers the handler at
`0x00812A51`. Its hash-pinned 162 bytes look up the event through audio-service
vslot `0x12C`, construct playback request mode `2`, and submit the request
through vslot `0x64`. The native zero-semantic-argument APT stub is independently
hash-pinned at `0x007FE9BB`; its two callers are exact, while the stripped
semantic names of the counters that lead to those calls remain unresolved.

The retail event has one exact leaf:

- `Gui_PalantirResourceBarFlash`
- `Sounds = UCommandPoints`
- volume 50, type `ui player`, `SoundFX` submix, reverb level 0
- `data/audio/sounds/ucommandpoints.wav`
- SHA-256
  `f2d3aff531ecfd3616069d53551823f92aee92f009382d3bf39d4ec8e2eca350`
- stereo 44.1 kHz, 4-bit IMA ADPCM with 2,048-byte blocks, 93,951 decoded
  sample frames, 2.130408 seconds

Every retrigger re-enters frame 8 and therefore submits one new native audio
request. The handler has no already-playing suppression branch. The only
remaining dynamic question is below that handler: whether the audio service
or mixer simultaneously mixes, coalesces, or voice-steals two requests for the
same event. The private contract retains a minimal two-trigger breakpoint
trace at `0x00812A51` and `0x00812AC5`; it does not invent an overlap policy.

Generate two payload-free contracts and verify them:

```powershell
$env:PYTHONPATH = 'importer'
python -m openbfme_importer.retail_hud_resource_flash_oracle `
  --scene-contract .private/scratch/hud-apt-clip-actions/bundle-a/data/ui/palantir/scene-contract.json `
  --effective-assets .private/retail-work/cache/effective-assets `
  --manifest .private/retail-work/cache/effective-assets/.openbfme/manifest.json `
  --game-dat F:/BFME2/game.dat `
  --output .private/scratch/hud-resource-flash-oracle/contract-a.json
python -m pytest importer/tests/test_retail_hud_resource_flash_oracle.py -q
python -m ruff check importer/openbfme_importer/retail_hud_resource_flash_oracle.py importer/tests/test_retail_hud_resource_flash_oracle.py
```

This is an oracle only. It does not change the converter, Godot runtime,
profiles, builds, or retail payload closure.
