# Build and release proof

OpenBFME currently ships proof-stage source, not a public retail-content build. The
Stage 10 gate is the authoritative local hardening command:

```bat
run_stage10_tests.bat
```

It checks the pinned toolchain, every earlier deterministic and visual stage gate, the
legacy skirmish regression suite, both boot paths, the accelerated 30-minute simulation
soak, and the Lane A export scan. A successful run ends with `STAGE10_GATE PASS`.

## Toolchain

- Godot 4.7 stable, selected with `OPENBFME_GODOT` when it is not in the documented
  local location.
- .NET SDK 10.0.100, pinned by `global.json`.
- Python 3.12.10 for deterministic data generation utilities; private retail builds
  additionally bind the venv launcher, base interpreter DLL, and bounded standard
  library/runtime tree rather than trusting the version string alone.
- For private retail import only: portable Blender 4.2.0, the pinned OpenSAGE W3D
  plugin, FFmpeg 8.1.1, and Pillow. `openbfme-import bootstrap-tools` provisions or
  verifies the external pins; none is a runtime dependency.

Run `run_doctor.bat` for exact detected versions and executable paths.

## Optional MCP operator tooling

The per-user Codex configuration has optional `godot`, `blender`, `ludo`, and
`elevenlabs` MCP servers. They help inspect the running editor and create reviewed
content, but they are not build dependencies or sources of truth. They must not bypass
`run_retail_pipeline_tests.bat`, `run_stage10_tests.bat`, provenance checks, or the
external-retail-content boundary.

- Godot MCP uses `Coding-Solo/godot-mcp` commit
  `1209744fad78f3998f98c7394fd0f6ef50da5281` with a locally hardened lock
  (`@modelcontextprotocol/sdk` 1.26.0 and Axios 1.16.0).
- Blender MCP uses `ahujasid/blender-mcp` add-on commit
  `6e99eb5a442b83766a5796975ec7bb5bfc791341` and the pinned 1.6.4 server package;
  telemetry and optional third-party asset services stay disabled.
- ElevenLabs MCP uses official commit
  `afc22357432db9e8b33991a83d41906001f6d759` and package 0.11.0. Its base path is
  external to the checkout, and setup verification must not generate billable audio.
- Ludo uses `https://mcp.ludo.ai/mcp`; it is an optional design/reference aid only.

Secrets live in the Windows user environment as `LUDO_API_KEY` and
`ELEVENLABS_API_KEY`; never copy their values into this repository or `config.toml`.
All four servers default to prompt-before-tool-use. Restart Codex after changing MCP
configuration so the desktop app inherits the user-scoped credentials.

## Rebuild repository-authored content

The legal-safe proof bundle in `content/openbfme-test` is maintained directly and
validated by the cumulative gates. The older default skirmish pack can be regenerated
from repository-authored definitions with:

```bat
python tools\build_base_content.py
```

The optional `tools\port_mert_config.py` path consumes only the committed
`tools\mert_export\full_config.json` snapshot. Both utilities resolve paths relative to
the checkout and do not require the original author's desktop layout.

After any content rebuild, run `run_stage10_tests.bat`; do not distribute a build that
fails provenance, path-containment, or export scanning.

## Stage 11-15 focused checkpoints

Stages 11-15 are focused private-retail implementation checks, not a second release
train and not evidence that the vertical slice is complete.

| Stage | Focused evidence surface |
|---|---|
| 11-12 | `game/tests/stage11_12_runner.gd` checks control groups, pending route/destination/order state, transactional rejection, arrival cleanup, and deterministic signatures. |
| 13 | `run_importer_tests.bat` covers fail-closed helper/ambiguous-box filtering and mandatory right-hand weapon/left-hand shield evidence; the private playable check consumes the sanitized capability report. |
| 14-15 simulation | `game/tests/stage14_15_sim_runner.gd` checks the symmetric five-role base authority, one-Soldier economy/production contract, source-timed attack impact, shared enemy queue behavior, and Fortress outcomes. |
| 15 menu/settings | `game/tests/stage15_menu_runner.gd` checks the uncluttered page flow and persistent music, voice/SFX, and mute controls without requiring retail content. |
| 15 integration | `run_retail_slice.bat --test` checks the mounted private Soldier/Fords scene, fallback structures, HUD/order feedback, production, combat, and outcome presentation together. |

The focused Godot runners can be invoked with the same Godot 4.7 executable selected by
`run_doctor.bat`, for example:

```bat
"%OPENBFME_GODOT%" --headless --path game --script res://tests/stage11_12_runner.gd
"%OPENBFME_GODOT%" --headless --path game --script res://tests/stage14_15_sim_runner.gd
"%OPENBFME_GODOT%" --headless --path game --script res://tests/stage15_menu_runner.gd
```

These runners do not replace the handoff order: run the smallest focused check first,
then `run_retail_pipeline_tests.bat`, then `run_stage10_tests.bat`. Godot terrain-layer
rendering, remaining Fords object bindings, playable Archer/Tower Guard/Knight
integration, five completed retail building lifecycles, placement/construction, full building-aware navigation, the complete
Men AI/economy/production loop, and oracle coverage remain release blockers. The retail
profile therefore remains `vertical_slice_complete: false`.

## Private launch

`run_stage10.bat` opens the full stage menu. Stages 5 through 9 also have direct launch
scripts for focused inspection. Retail BFME2 data and locally converted retail data
must stay outside this repository and outside any exported build.

## Private retail-content gate

With a user-owned BFME II 1.06 install at `F:\BFME2`:

```bat
run_importer.bat F:\BFME2
run_importer_tests.bat
run_retail_pack_tests.bat
run_retail_slice.bat --test
run_retail_pipeline_tests.bat
```

The importer publishes an immutable directory named by its canonical bundle SHA-256.
The runtime selection contains only a contained relative path. The retail pack gate
loads all four scoped unit GLBs, portraits, object/horde buttons, and production
buttons, the Soldier's 23-clip closure, four core
clips for each additional unit, five intact structure GLBs, exact terrain layers and
66 converted terrain materials, the first exact `PTGrass15` prop binding, UI textures,
PCM voices, and MP3 music without Godot import metadata or retail-source access. The
playable gate also consumes the stricter
Soldier equipment proof, derives attack timing from imported rules, and verifies that
the cooked map facts drive bounded terrain/water presentation, exact starts, named-ford
named-ford routing, generic placement markers, and the source-coordinate minimap. Its integrated
base loop uses five pre-placed structure roles per team; incomplete lifecycle visuals may be
repository-authored procedural/legal-safe fallbacks, and only Gondor Soldiers can be
trained. `run_retail_pipeline_tests.bat` is the authoritative private-content gate; it
proves repeat-build identity before the runtime, legacy, and export checks. Launch the
playable private battle with `run_retail_slice.bat`. This content must never be part of
a public/export artifact assembly.
