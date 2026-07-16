# Retail Fords ambient-audio profile

`retail_fords_ambient_audio_profile.py` closes the seven ambient sound IDs used
by the retail Fords of Isen II object placement contract. It is a planner: it
reads verified retail data below `.private`, but writes only JSON rules,
logical definitions, parameters, paths, sizes, and hashes. It never copies
audio payloads.

## Exact closure

The sealed BFME2 1.06 closure is:

| Logical ID | Source kind | Body | Attack/decay | Source range | Control |
| --- | --- | ---: | ---: | --- | --- |
| `Amb_BirdsAmonHen1` | `AudioEvent` | 10 | 0 | `AMB_MIN_RANGE`..`AMB_MAX_RANGE` = 300..800 | `loop` |
| `Amb_BirdsAmonHen2` | `AudioEvent` | 10 | 0 | `AMB_MIN_RANGE`..`AMB_MAX_RANGE` = 300..800 | `loop` |
| `Amb_MTBirds1Loop` | `AudioEvent` | 6 | 2 | `AMB_MIN_RANGE`..`AMB_MAX_RANGE` = 300..800 | `loop` |
| `Amb_MTBirds2Loop` | `AudioEvent` | 8 | 2 | `AMB_MIN_RANGE`..`AMB_MAX_RANGE` = 300..800 | `loop` |
| `Amb_CritterDesert1` | `AudioEvent` | 3 | 0 | `AMB_MIN_RANGE`..`AMB_MAX_RANGE` = 300..800 | `loop` |
| `Amb_WaterRiver1Loop` | `AudioEvent` | 13 | 2 | 400..1000 | `loop` |
| `AmbientAmonHenForest1Stream` | `AmbientStream` | 1 | 0 | not authored | not authored |

That is 57 unique leaves: 56 WAV samples and one MP3 stream. There are no
`Multisound` nodes in this particular closure. Every stem must resolve to one
and only one winning effective-asset path. The planner rejects missing or
ambiguous stems, duplicate/cyclic logical definitions, changed placement
counts, changed Godot routing, and catalog misses.

The map-side contract contains 50 placements:

- 16 `Amb_BirdsAmonHen1`
- 11 `Amb_BirdsAmonHen2`
- 3 each of the two Ithilien bird object types
- 6 desert critter emitters
- 10 river emitters
- 1 Amon Hen forest stream emitter

The source object names and logical event names are distinct for the two
Ithilien bird emitters and for the stream. The planner verifies the exact
seven-row mapping already declared by `retail_slice_audio.gd`.

## Profile and runtime handoff

`profileFragment.resources` contains two conversion rules:

- one 56-pattern `audio` rule that cooks PCM WAV leaves under
  `assets/audio/ambient/`;
- one exact `copy` rule for the MP3 ambient stream.

`runtimeAudioRegistryAddition` is a complete, schema-compatible
`openbfme.audio-events` version 1 document for this subset. It retains source
parameter order. Numeric `MinRange` and `MaxRange` macros are resolved to 300
and 800 in the runtime rows, while `sourceDefinitions` preserves the authored
macro tokens and records both authored and resolved range values. Attack and
decay identifiers remain parameters and are also included in the exact sample
closure.

Deduplication is permitted only when the current complete profile already owns
the same virtual source path and therefore the same manifest hash. A matching
hash at a different path is not deduplicated. For the current Men-Fords
profile, all 57 leaves are new.

## Honest runtime boundary

This plan closes extraction, conversion inputs, registry data, source
parameters, and placement routing. It does not claim SAGE playback parity.
The current Godot runtime can instantiate the 3D emitters and select body
samples, but it explicitly reports unsupported SAGE behavior for attack/decay
envelopes, delayed loop scheduling, attenuation curves, priority/limit,
pitch/volume variation, and the ambient stream's distinct semantics. Those
diagnostics must remain until executable behavior is implemented and verified;
the audio payload closure alone is not a 1:1 audio gate.

## Generate and verify

From the repository root:

```powershell
$env:PYTHONPATH = "importer"
python -m openbfme_importer.retail_fords_ambient_audio_profile `
  --effective-assets-root .private/retail-work/cache/effective-assets `
  --manifest .private/retail-work/cache/effective-assets/.openbfme/manifest.json `
  --catalog .private/retail-work/catalog/bfme2.json `
  --map-objects .private/retail-work/packs/bfme2-men-vslice/maps/fords-of-isen-ii/objects.json `
  --complete-profile .private/retail-work/profiles/men-fords-v0-full.generated.json `
  --runtime-audio game/src/retail_slice/retail_slice_audio.gd `
  --output .private/scratch/fords-ambient-audio-profile/plan.json

python -m ruff check `
  importer/openbfme_importer/retail_fords_ambient_audio_profile.py `
  importer/tests/test_retail_fords_ambient_audio_profile.py
python -m unittest importer.tests.test_retail_fords_ambient_audio_profile -v
```

Run the generator twice and compare the complete JSON documents. The focused
private integration test also constructs an `ImportProfile`, resolves it
against the real BFME2 catalog, and requires 57 selected entries with zero
missing required resources.
