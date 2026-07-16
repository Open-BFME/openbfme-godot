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
| Generated WAV music beds | Adaptive explore/battle/victory stand-ins | Procedural output; generator/input provenance required | Legal-safe development lane only; not retail audiovisual parity evidence |
| User-owned BFME2 1.06 installation | Private compatibility extraction, conversion and oracle observation | Proprietary retail content; non-redistributable project lane | Inputs and all derived payloads remain below `.private`; never shipped, committed or transferred by a server |

Donor material is accepted only through a bounded provenance review. No donor runtime
types or source are copied merely because a format tool is used for observation.

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
