# Godot runtime agent rules

This subtree owns presentation, input, UI, audio, and the current retail-slice
integration. Follow the root `AGENTS.md`, `docs/ARCHITECTURE.md`, and
`docs/MILESTONE_CURRENT.md`.

- Runtime code consumes converted OpenBFME packs only. Never parse BIG, W3D,
  retail INI, retail map containers, Blender, or OpenSAGE types during play.
- Private parity must use the strict selected pack and fail closed. Do not add
  silent generic/procedural fallbacks to required paths.
- Godot presentation must not become the owner of health, resources, combat,
  production, AI, victory, deterministic RNG, or networking truth.
- Selection, camera, control groups, cursor, UI layout, and immediate order
  feedback are client-local.
- Use the packet's focused Godot runner. Workers do not run the final M2 gate.
- Preserve existing user changes and stop if the packet overlaps a dirty file.
