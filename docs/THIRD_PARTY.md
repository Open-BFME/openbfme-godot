# Third-party / content sources

| Source | Use | Notes |
|---|---|---|
| middle-earth-rts project assets (icons, GLB, SFX) | Default pack 3D/UI/audio | Original/AI-generated for that project |
| Generated OBJ meshes | Units/buildings without GLB | Tools-authored stand-in geometry |
| Generated WAV music beds | explore/battle/victory | Procedural tones for adaptive music states |
| BFME2 retail install | Optional private extract only | Not required for default pack load |
| [Blender 4.2.0](https://download.blender.org/release/Blender4.2/) | External headless W3D-to-GLB process | GPL-3.0-or-later; portable user-cache tool; executable SHA-256 `80FB653019A0AFB3BDA0947EC74E84DC0A94D0D388F9B3849433C0E1A4EFDABE`; full portable-tree SHA-256 `81E0CFB0D56FF5E33C2C562B13CC88257B9B34E072EFA7AE054A6C87F13F2AA4` |
| [OpenSAGE BlenderPlugin](https://github.com/OpenSAGE/OpenSAGE.BlenderPlugin) | External W3D reader inside Blender | LGPL-3.0; commit `2de84023cb632a79a853b2a52f97c8002ed85142`; no donor code copied into runtime/repo |
| [OpenSAGE](https://github.com/OpenSAGE/OpenSAGE/tree/588ac477367a0022adf29f20a084e8873014e6ce) map reader | Format research for an independent bounded SAGE map cook | GPL-3.0 plus repository additional terms; commit `588ac477367a0022adf29f20a084e8873014e6ce`; no OpenSAGE map-reader source copied into this repository or runtime |
| [blender-addon-updater](https://github.com/OpenSAGE/blender-addon-updater) | Required plugin submodule | GPL-3.0; commit `981aa2984117a1c686b7fa40d086794ce1c7665e`; external tool cache only |
| FFmpeg 8.1.1 essentials build | Legacy WAVE to deterministic PCM16 WAV | GPLv3 build; external tool cache; executable SHA-256 `228D7A8556258DE907FDB55F36850078EBC7680B84EC30D84EA02E99BEC1D1EB` |
| [Pillow](https://python-pillow.github.io/) 12.2.0 | Deterministic DDS/TGA to PNG conversion/cropping | MIT-CMU; Python environment dependency; no Pillow code copied |

Default `data/base` pack loads without `F:\BFME2`.

Importer tools are process-bound dependencies only. They are not linked into,
serialized into, or required by the Open BFME runtime. Absolute tool paths appear only
in the external tool manifest, never in canonical pack provenance.
