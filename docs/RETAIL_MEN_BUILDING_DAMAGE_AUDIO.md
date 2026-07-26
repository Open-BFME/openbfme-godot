# Retail Men building-lifecycle audio closure

`retail_men_damage_audio.py` seals the nine audio identifiers carried by the
five schema-v1 Men building-lifecycle contracts. It is an evidence and profile
planning pass only: it does not cook media, publish a pack, or claim SAGE mixer
parity. The completion composer now requires its sealed contract and performs
the narrow profile integration.

## Result

All 9 identifiers have one exact, case-matching `AudioEvent` definition and all
79 referenced media leaves resolve uniquely. The definitions come from the
effective `ini.big` winner at precedence 91. Seven structure Foley events and
the two voice events have no parent syntax, no competing effective definition,
and no lower-precedence catalog copy of their defining INI file.

| Identifier | Effective source and half-open file byte span | Samples | Exact source controls |
|---|---|---:|---|
| `BuildingBigConstructionLoop` | `data/ini/soundeffects.ini` `[235602,236334)` | 22 body + 5 attack + 3 decay | volume 80; pitch shift -25..-15; limit 3; priority low; loop; world/shrouded/everyone |
| `BuildingSink` | `data/ini/soundeffects.ini` `[516294,516577)` | 3 | volume 110; volume shift -10; pitch shift -3..3; limit 3; priority low; world/shrouded/everyone |
| `GondorArcheryRangeArrowQuiver` | `data/ini/soundeffects.ini` `[220374,220782)` | 10 | volume 27; volume shift -10; pitch shift -10..10; range 100..300; limit 3; priority lowest |
| `GondorArcheryRangeBows` | `data/ini/soundeffects.ini` `[221181,221504)` | 6 | volume 27; volume shift -10; pitch shift -5..5; range 100..300; limit 3; priority lowest |
| `GondorArcheryRangeDrawBow` | `data/ini/soundeffects.ini` `[220784,221179)` | 10 | volume 36; two authored `VolumeShift = -10` rows; pitch shift -5..5; range 100..300; limit 3; priority lowest |
| `GondorArcheryRangeHits1` | `data/ini/soundeffects.ini` `[221506,221806)` | 4 | volume 23; pitch shift -5..5; range 100..300; play percent 35; limit 3; priority lowest |
| `GondorArcheryRangeHits2` | `data/ini/soundeffects.ini` `[221808,222108)` | 4 | volume 23; pitch shift -5..5; range 100..300; play percent 35; limit 3; priority lowest |
| `GondorArcheryRangeVoiceFire` | `data/ini/voice.ini` `[500006,500268)` | 3 | volume 54; range 100..300; play percent 6; priority lowest; world/shrouded/everyone/voice |
| `GondorArcheryRangeVoiceAim` | `data/ini/voice.ini` `[500270,500627)` | 9 | volume 54; range 100..300; play percent 6; priority lowest; world/shrouded/everyone/voice |

No event authors `Pitch` or `Delay`. Every event authors `SubmixSlider =
SoundFX`. The private contract retains every parameter in source order instead
of collapsing duplicates, plus the exact block/file hashes, inclusive source
lines, archive-relative byte spans, archive offset, precedence, every sample
filename, media hash, size, and archive provenance.

## Exact profile delta

The existing registry already contains all nine event rows and 71 of the 79
unique samples. The only missing leaves are the attack/decay envelope of
`BuildingBigConstructionLoop`:

- attack: `CUBuild_consL1a`, `CUBuild_consL1b`, `CUBuild_consL1c`,
  `CUBuild_consL1d`, `CUBuild_consL2w`;
- decay: `CUBuild_consL3a`, `CUBuild_consL3b`, `CUBuild_consL3c`.

Those eight WAVs total 426,762 source bytes. None was owned by the 348-resource
pre-integration profile or named in `data/audio_events.json.samples`. The
contract therefore proposes exactly one eight-pattern PCM-audio resource under
`assets/audio/men-building-lifecycle/` and eight sample-registry merge rows.
`retail_fords_completion_profile.py` validates the sealed aggregate,
9-definition/79-leaf closure, precedence evidence, exact resource, and exact
registry delta before integrating it. It never duplicates the 71 owned leaves
or the nine existing event rows.

## Semantics boundary

This pass proves authored fields and media membership, not how `game.dat`
evaluates them. A parity runtime still needs an oracle for random-pool selection
and seed, duplicate-field evaluation, pitch/volume units, range attenuation,
world and shroud audience gating, priority/limit/play-percent arbitration,
attack-body-decay loop stitching and stop timing, and submix/mastering behavior.
The duplicate `VolumeShift` in `GondorArcheryRangeDrawBow` is intentionally
retained twice; choosing first-wins, last-wins, or cumulative behavior would be
a guess.

## Reproduce

```powershell
$env:PYTHONPATH = 'importer'
.private\retail-work\tools\python-3.12-env\Scripts\python.exe `
  -m openbfme_importer.retail_men_damage_audio `
  --effective-assets-root .private\retail-work\cache\effective-assets `
  --manifest .private\retail-work\cache\effective-assets\.openbfme\manifest.json `
  --catalog .private\retail-work\catalog\bfme2.json `
  --complete-profile .private\scratch\men-damage-audio\preintegration-profile.json `
  --output .private\scratch\men-damage-audio\contract-a.json

<HOME>\AppData\Local\Programs\Python\Python312\python.exe `
  -m pytest importer\tests\test_retail_men_damage_audio.py -q
<HOME>\AppData\Local\Programs\Python\Python312\python.exe `
  -m ruff check importer\openbfme_importer\retail_men_damage_audio.py `
  importer\tests\test_retail_men_damage_audio.py
```

Two independently emitted private contracts must be byte-identical before the
fragment is considered for integration. The sealed pre-integration profile is
SHA-256 `ea1b56ca9906d7cfa63f4c8949f236dd039b72ca1b03d6b624113944be19f91b`;
the contract aggregate is
`148e8089f3754899bb4a933fff61a4bdd5693320a8e6cfb15b6258dceebf206e`.
