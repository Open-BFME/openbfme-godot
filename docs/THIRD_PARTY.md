# OpenBFME third-party and content-source ledger

> **Owner:** Release and provenance integration owner
> **Owns:** Third-party source, purpose, license, integration form, pin authority, notices, and donor-use record.
> **Does not own:** Legal advice, retail-content redistribution permission, tool bootstrap implementation, or current selected-pack identity.
> **Last verified commit:** `efe6a6c1f7ab76ae84436faed4e9a02298a4a194`
> **Update trigger:** A third-party source, version, license, integration form, shipped file, or provenance authority changes.
> **Validation:** Private tool manifest/attestation, release provenance manifest, license-notice audit, and code-only export scan.

This ledger records engineering provenance; it is not a legal opinion. Exact executable,
tree and selected-package digests belong in generated tool/provenance manifests or
`STATUS.md`, not copied into this document.

## Content and donor sources

| Source | Use | License/status | Integration and release boundary |
|---|---|---|---|
| `middle-earth-rts` project assets | Repository-authored/legal-safe 3D, UI and audio donor material | Original or AI-generated for that project; provenance must be reviewed per asset | May support legal-safe development/fallback content; cannot prove retail parity or silently fill strict-retail gaps |
| Generated OBJ meshes | Tools-authored stand-in geometry | Repository-authored output; generator/input provenance required | Legal-safe development lane only unless separately approved for release |
| Generated WAV music beds | Adaptive explore/battle/victory stand-ins | Procedural output; generator not yet committed - see *Open provenance items* | Legal-safe development lane only; not retail audiovisual parity evidence |
| Procedural SFX (`tools/gen_sfx.py`) | 51 placeholder combat/UI/ambient sound effects | Repository-authored; synthesised from committed source, reproducible byte-for-byte | Shippable repository-authored lane; stands in for retail audio the player supplies locally |
| User-owned BFME2 1.06 / RotWK 2.01 installation | Private compatibility extraction, conversion and oracle observation | Proprietary retail content; non-redistributable project lane | Inputs and all derived payloads remain below `.private`; never shipped, committed or transferred by a server |

Donor material is accepted only through a bounded provenance review. No donor runtime
types or source are copied merely because a format tool is used for observation.

## Shipped asset provenance (`game/data/base/assets`)

`RELEASE_POLICY.md` requires a provenance record for every shipped file. This section
records the tracked binary asset groups under `game/data/base/assets`, the evidence
established for each, and what remains outstanding. Findings below are engineering
forensics on the artifacts themselves; they are not a legal opinion.

| Group | Files | Size | Status |
|---|---|---|---|
| `audio/sfx/*.wav` | 51 | 4.7 MB | **Regenerated.** Repository-authored, reproducible from `tools/gen_sfx.py` |
| `audio/music/*.wav` | 3 | 0.2 MB | Procedural by inspection; generator not committed - open item |
| `icons/*.png` | 127 | 189.6 MB | Retail origin ruled out by forensics; positive attestation owed by the owner |
| `models/{units,buildings}/*.glb` | 27 | 210.4 MB | Retail origin ruled out by forensics; positive attestation owed by the owner |
| `models/units/*_texture_0.webp` | 15 | 2.1 MB | Byte-identical duplicates of textures already embedded in the sibling GLBs |
| `models/*/*.obj` | 74 | 0.2 MB | Tools-authored stand-in geometry (existing ledger row) |

### Procedural SFX - repository-authored, reproducible

The 51 files in `game/data/base/assets/audio/sfx` are synthesised entirely from code in
`tools/gen_sfx.py`. Nothing is sampled, recorded, downloaded or model-generated. The
committed generator plus the command below is the provenance record; the digests are the
verification.

Regenerate:

```text
python tools/gen_sfx.py
```

Verify the shipped bytes against a fresh render without writing anything:

```text
python tools/gen_sfx.py --check
```

Output is canonical 44-byte-header RIFF/WAVE, PCM signed 16-bit, mono, 44100 Hz, with no
INFO/LIST metadata chunk. Determinism is a hard property of the generator, not an
observation: the per-sample path uses only IEEE-754 add/subtract/multiply, and every
libm-derived wavetable entry and filter coefficient is quantised to 12 decimals so a
one-ULP platform difference cannot propagate. Randomness is `random.Random` seeded from a
SHA-256 of the effect name and consumed via `getrandbits(32)`. Verified byte-identical
across CPython 3.12.x and 3.13.11 on Windows x64.

**Replacement rationale.** The 51 files previously at these paths were replaced, not
ledgered. Every one carried the FFmpeg writer tag `Lavf62.12.101` from a single batch
transcode with no generator anywhere in the repository, and two files retained upstream
authoring metadata that survived the transcode:

- `bow-release-02.wav` - `ICRD 2011-01-04`, `ITCH "Logic Express"`
- `fire-crackle-02.wav` - `IART "PagDev"`, `ICRD 2021`, `IGNR "SFX"`, `INAM "Feuer"`, `IPRD "Soundeffekt"`

That metadata is positive evidence of third-party human-authored source recordings of
unrecorded licence, acquired outside any recorded admission review. It rules out the
"procedurally generated by this project" reading that the ledger would otherwise have
had to assert. The batch was therefore discarded rather than documented.

| File | SHA-256 | Bytes |
|---|---|---|
| `ambient-crows-01.wav` | `ee6459a352985ab340bfbddfb8a76698b93a6416490aed18ab15bcdb8be2265a` | 264644 |
| `ambient-crows-02.wav` | `ed0ee81394bd13efecc1494c1f106386805f0362d073ac814481c2539c472f8b` | 123524 |
| `ambient-wind-loop-01.wav` | `32fc784125756da8e0d2b09ffc0bd2915951bb5fa072a19b3bb3f6a345cadae0` | 264644 |
| `ambient-wind-loop-02.wav` | `edff758853a1dcd9f605651eeaa8919779022cbd65a1a5ca32d21625b30e92d8` | 264644 |
| `ambient-wind-loop-03.wav` | `159c83d031c8f3a0c1116fae16cd5798c5b7e6c1222d28eb7e874ebfffd1053b` | 264644 |
| `arrow-impact-01.wav` | `8c0c671d2acb7124090ebfc498ba996cd689f94fbb3544c30fae64b6b6bd6cb6` | 15038 |
| `arrow-impact-02.wav` | `d30672438b1039a17f45b0403ba35a133753f7cb2953144c327c10d691a08fa6` | 17684 |
| `arrow-impact-03.wav` | `4d877e458352909b769c741d6a8a5004a6f7cb63d27b0dba234254311198e63c` | 20330 |
| `bow-release-01.wav` | `cbb0b33fa0d713f585e7dc7c8bd64a4d8ef791d551f2d7b7cfe585dbd8d88733` | 22976 |
| `bow-release-02.wav` | `f9d7cc1454888c7adda534c3c2e01fdb0eebe48ab297461835655598852ae8b6` | 19448 |
| `bow-release-03.wav` | `95de4beb7f6a978a6c1ecfb0e848163be0a72a8995361ec0e5dcaadd22359407` | 24740 |
| `building-collapse-01.wav` | `8bce52f9d741604b08e0f41971d97f969d5e56156a94b19f0d63080b84b0a783` | 123524 |
| `building-collapse-02.wav` | `b149a5f0c21ef3e574349a30a1d0e1aa148c096e6bec79a38d1d721a6bb5a183` | 101474 |
| `building-collapse-03.wav` | `beaa3a6d23a4d81a48c53fd6585ccac30f6fbab262d84e85e1fb594aec692fc5` | 149984 |
| `building-place-01.wav` | `d90ee9a6fd01f6570e93e7d1f689d92e6849d428759d98beaf39334428c5cb76` | 45908 |
| `building-place-02.wav` | `abe41650f8d6a6002a7fcb5399844f48d382639afe3cee73ecce7fa62e051e18` | 38852 |
| `building-place-03.wav` | `296935e4b285f02b080c80673282f70cc13568a37c55b1b094aa0d75ce7e69a8` | 54728 |
| `click-ui-01.wav` | `1d0af42a1d826b7d4331e038a7dc0cefad0e8f82aa6ccdd897a84aa1026d998b` | 6660 |
| `click-ui-02.wav` | `9c1f2f32d40476018347d3bb7e1471ea77e2e9ad58ca85fd3efafe7091fd1823` | 5776 |
| `click-ui-03.wav` | `3e00755a7f3e5479afb3892b46655572613658240e914becf3446b196433d0bc` | 7542 |
| `fire-crackle-01.wav` | `23eda8672ae45e686da0d9e4264ade243704c8c89819fd7f8e8122027eb90614` | 247004 |
| `fire-crackle-02.wav` | `918f710a202cfa9a8cc5cd84bcd9bb84844dcfb100e5becfe2a1fdb239152098` | 264644 |
| `heavy-impact-01.wav` | `0cb926ef81ee9c6a07bfddb583e7b25573ed22f07b53957bd59285654303b8b2` | 42380 |
| `heavy-impact-02.wav` | `7f39289ab6bb8663775ba518e9dbabc023981e99b33f1c2201a45e374685fa6f` | 33560 |
| `heavy-impact-03.wav` | `e27fa6619c48cdbd054ab0a650b9f928b65d1c0e757d2ad040d265126544fdf2` | 52964 |
| `horn-gondor-01.wav` | `f13311e79d939b47a704e7c93a69f1f099a9d000fb8dbd7163faab5d75f35ab9` | 211724 |
| `horn-gondor-02.wav` | `ebbebb09bbb4275e07783683eae784ae55fd9d16c54d3727d3921306eca4d951` | 224954 |
| `horn-war-01.wav` | `71c8d108a0399f034942953223fd313001785df99238ef112fd94d97995aef3a` | 238184 |
| `horn-war-02.wav` | `46b77624b042cef7884fc5a675631850308e607c11df538e9dd9eaf3b58b65a7` | 247004 |
| `level-up-chime-01.wav` | `0a45bf115639c9180d3033b4ab00bce394a2298b17ab6ae388a12de4155225b6` | 79424 |
| `level-up-chime-02.wav` | `596e375f18616745c64924cfc6bcd129ba3dab377fbfc4f3e63a4b5372fd3b92` | 75014 |
| `level-up-chime-03.wav` | `ee036963d50cca5ec89274b9afe8cac97331078036b10a39c392163e3776c533` | 83834 |
| `magic-cast-01.wav` | `78770239d87babfca0b22c64b63ae95e80d84fa75c51268b9da2ca78ed911ac8` | 66194 |
| `magic-cast-02.wav` | `b5bc0aedc593f76dd9b80406e7ca1b45915024c71872a46b96be78515de54280` | 54728 |
| `magic-cast-03.wav` | `fd4ea2fa3814d3108be38b535c548dc8eed3434fb17e9a5c6689449e1245655e` | 79424 |
| `monster-roar-01.wav` | `90f8930bbdfd5db1e5067d52fd5b314db3642a34d2adf6ac8224aa8d8d504e69` | 167624 |
| `monster-roar-02.wav` | `80ba6e7658a6cbffea854dbef0c171651a1ce3aa2da4a83c5b4fe0b4d8ef3120` | 123524 |
| `monster-roar-03.wav` | `8716163a41428dd25d1955e234ca0a25766b4089a475a5019cfef6b232bbe308` | 97064 |
| `orc-growl-01.wav` | `c736d3152bb82b75cca7b8f8c51bb2939841ecbb711b2af82febccc84ab61554` | 61784 |
| `orc-growl-02.wav` | `31d344e83dd9f09fae67ad2d73487a214a36a5695808e2664172d7c6d6a935a2` | 49436 |
| `orc-growl-03.wav` | `3c3b948cfb6e30329eb83b1b1a76d39281bd7f1ceb46ac6bce0bb3628dfc32b0` | 75014 |
| `spider-skitter-01.wav` | `ca6b5e90f0980f062fa513947a3adeb9f37894fa5d2ccdca6c9baaf38bcc6bdd` | 21212 |
| `spider-skitter-02.wav` | `d089bdda16bc1d711c4a32d03eb621745c658f75dd01eb51ca870e8098e92c9a` | 30032 |
| `spider-skitter-03.wav` | `68a3b42a084f6d249316f4c6d92a954a72187d815259e26d6cc0f4b2da1cfe5f` | 15920 |
| `sword-hit-01.wav` | `0fcd31d36201ba8a6ee8ba90c2646449467ba57b2a8ea06c32c201f4c4339f50` | 37970 |
| `sword-hit-02.wav` | `e876a5e2b09b68d7961d0c20886898e7c297c5d4a9c467c93653be374968490d` | 44144 |
| `sword-hit-03.wav` | `5790ac83f12857c0e4cf0e6130c14eb5c6db994f86cfda781dec4a5ce68c6f36` | 49436 |
| `sword-hit-04.wav` | `e02555d23fed820f7276d749801cec52a2b4573558236297a51187d9223d7795` | 31796 |
| `trample-hooves-01.wav` | `52fcf84b1ffb7855cdbb951a2ab7a470194417cd6e36cefbfca07f8194afa857` | 33560 |
| `trample-hooves-02.wav` | `c3212b0dc1dabc03b07127d91e627ac2b7a3b0e080161c0e4b370a01b9de91ba` | 22976 |
| `trample-hooves-03.wav` | `e0229256c7d8e34dbd7dd7f1cd43b5781d0f9e9fe0ea70c0128cb3a09e0ad565` | 44144 |

### GLB models - retail origin ruled out, source attestation outstanding

27 tracked GLBs (15 rigged units, 12 static buildings, 210.4 MB). Forensics on the
artifacts:

- Every file declares `asset.generator = "glTF-Transform v4.4.0"`; textures are carried
  as `EXT_texture_webp`. Retail BFME2 art is W3D and never passes through glTF.
- All 15 unit files share a byte-identical 26-node skeleton - `Hips`, `Spine`, `Spine01`,
  `Spine02`, `neck`, `Head`, `headfront`, `head_end`, `Left/Right{Shoulder,Arm,ForeArm,Hand}`,
  `Left/Right{UpLeg,Leg,Foot,ToeBase}` - with the mesh named `char1` and the material
  named `Material_1`. Retail W3D hierarchies are per-unit and use `B_`-prefixed bone
  names.
- All 15 carry exactly five clips with identical names, including the auto-rig marker
  `Armature|clip0|baselayer`, then `idle`, `walk`, `attack`, `death`. Retail units carry
  30+ named animations.
- All 12 building files are single unnamed-material meshes named `geometry_0`.

This is the output signature of a commercial text-to-3D and auto-rig service, matching
`legal_assets.md` Lane B ("Meshy / Blender / Blockbench 3D"). The artifacts therefore rule
out retail derivation, but they do not by themselves name the service, the account, the
prompts, or the output licence in force at generation time. **The owner must supply that
attestation** before these files can be treated as release-approved; see
*Open provenance items*.

### UI icons - retail origin ruled out, source attestation outstanding

127 tracked PNGs (189.6 MB), all 1024x1024, 8 bits per channel. Forensics:

- 115 of 127 have no alpha channel at all, and the 12 RGBA files are fully opaque at the
  corners. Retail cameo art is alpha-cut DDS packed into small atlases.
- Three distinct writer signatures are present (73 files with `gAMA`/`pHYs`/`sRGB`, 42
  bare RGB, 12 bare RGBA), consistent with several generation batches. No `tEXt`,
  `iTXt` or `zTXt` chunk survives in any file, so none carries an authoring tool string.
- A 64x64 downscale/upscale round trip leaves a greyscale residual RMS of 9-18 across the
  set. Genuine detail exists well above 64 px, so these are not upscales of retail
  atlas cells.
- Visual inspection shows uniform painterly digital illustration with baked-in square
  frames and vignettes, in a style with no relationship to 2006 pre-rendered cameos.

Consistent with `legal_assets.md` Lane B ("Grok Imagine 2D"). As with the GLBs, the
evidence is negative (it excludes retail) rather than positive (it does not name a
generator). **Owner attestation outstanding.**

### Duplicate WEBP textures

Each of the 15 `models/units/*_texture_0.webp` files is byte-identical to the texture
already embedded in the sibling `.glb`, verified by comparison of the extracted
`bufferView` payload. They share the GLBs' provenance and add 2.1 MB of pure duplication.
No runtime reference resolves to them: `content_db.gd` tries `.glb` before `.webp`, so the
GLB always wins. They are removal candidates pending owner confirmation.

## Open provenance items

Items below block treating the affected files as release-approved. Each needs an owner
decision; none can be resolved from the artifacts alone.

1. **GLB source attestation.** Which service produced the 27 GLBs, under which account and
   terms, and does that service's licence permit redistribution in a public repository?
   Forensics prove they are not retail-derived; they cannot prove what the output licence
   is.
2. **Icon source attestation.** Same question for the 127 PNGs. If they came from Grok
   Imagine as `legal_assets.md` Lane B implies, the applicable terms of service at
   generation time need to be recorded here.
3. **Music bed generator.** `audio/music/{explore,battle,victory}.wav` are 22050 Hz mono
   PCM with smooth zero-origin sine ramps and no metadata chunk, which is consistent with
   the "procedural output" ledger row above, but no generator is committed anywhere in
   `tools/` or `importer/`. The claim is currently unverifiable in the same way the SFX
   claim was. They should be regenerated from committed source on the `tools/gen_sfx.py`
   pattern.
4. **Icon resolution.** The icons are consumed only as `Button.icon` with
   `icon_max_width` of 48-56 px in `retail_hud.gd`. At 1024x1024 they are roughly 20x
   oversampled in each dimension. Re-encoding at 256 px would take the group from
   189.6 MB to about 14.9 MB, and 128 px to about 4.2 MB, both with headroom over the
   largest display size. This is a presentation change and is not applied here.
5. **Duplicate WEBP removal.** 2.1 MB of unreferenced byte-identical texture copies.

## Importer toolchain

| Source | Use | License | Integration form and pin authority |
|---|---|---|---|
| [Blender](https://www.blender.org/) | External headless W3D-to-GLB process | GPL-3.0-or-later | Portable private tool cache; exact version, executable and tree identity are attested in the generated tool manifest; not a runtime dependency |
| [OpenSAGE BlenderPlugin](https://github.com/OpenSAGE/OpenSAGE.BlenderPlugin) | External W3D reader inside Blender | LGPL-3.0 | Pinned external plugin source in the private tool cache; no plugin code copied into the runtime or content pack |
| [OpenSAGE](https://github.com/OpenSAGE/OpenSAGE) | Format research for the independent bounded SAGE map cook | GPL-3.0 plus repository additional terms | Pinned research/tool source; no OpenSAGE map-reader source or type is copied into this repository or runtime |
| [blender-addon-updater](https://github.com/OpenSAGE/blender-addon-updater) | Dependency of the external Blender plugin | GPL-3.0 | Pinned external tool-cache dependency only |
| [FFmpeg](https://ffmpeg.org/) | Legacy WAVE to deterministic PCM16 WAV | The selected build is GPLv3 | Private external process; exact build/executable identity is recorded in the generated tool manifest; not a runtime dependency |
| [Pillow](https://python-pillow.github.io/) | Deterministic DDS/TGA conversion and atlas cropping | MIT-CMU | Pinned private Python-environment dependency; no Pillow code copied into a content pack |
| [fontTools](https://github.com/fonttools/fonttools) | TTF/OTF table and checksum validation | MIT | Pinned private Python-environment dependency; no fontTools code copied into a content pack |
| [defusedxml](https://github.com/tiran/defusedxml) | External-resolution-free XML validation | PSFL | Pinned private Python-environment dependency; no defusedxml code copied into a content pack |

Importer tools are process-bound dependencies. They are not linked into, serialized
into, or required by the OpenBFME runtime. Absolute tool paths remain in the private
external-tool manifest and never enter canonical pack provenance or a public artifact.

## Admission requirements

Before adding or updating a third party, record:

- canonical project and source URL;
- exact purpose and whether it is build-time, runtime, research-only or content;
- license and any additional terms;
- selected pin in the generated provenance authority;
- files or outputs that enter the repository or release;
- required attribution/notices/source-offer obligations; and
- removal/replacement impact.

The release owner must verify those facts against the selected source. Unknown or
ambiguous rights block distribution even when local private use continues.

## Release boundary

The public code-only export includes only approved repository-authored and licensed
third-party files. It excludes the user-owned retail installation, extracted entries,
decoded media, converted assets, private packs, oracle captures and private tool caches.
See `RELEASE_POLICY.md` for the complete containment and packaging gate.
