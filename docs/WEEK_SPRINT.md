# Week sprint — Men/Fords 1:1 graphical playable slice

## Path locks (active)

| Worker | Lock | Goal |
|---|---|---|
| W1 Terrain | `game/src/retail_slice/retail_fords_battlefield.gd`, `retail_map_data.gd` | Cooked terrain textures + blend/cliff in Godot |
| W2 Units/structures runtime | `game/src/retail_slice/retail_battalion.gd`, `retail_structure.gd`, `retail_slice_sim.gd`, `retail_vertical_slice.gd` | Mount all 4 units + 5 structure GLBs; kill procedural fallback when pack has assets |
| W3 Props pipeline | `importer/`, `importer/profiles/`, tools import profile only | Convert top Fords prop types by placement count into pack |
| W4 UI/audio wire | `game/src/retail_slice/retail_hud.gd`, `retail_slice_audio.gd`, `retail_minimap.gd`, `retail_palantir_frame.gd` | Bind men-leaves UI/audio into slice HUD |

## Day order
1. W1+W2 first (visible fidelity)
2. W3 after pack profile stable
3. W4 in parallel once pack UI leaves path known
4. Integration owner: play loop (train 4, AI, victory) after mounts exist

## Acceptance north star
`run_retail_slice.bat --test` plus human visual check: Fords looks retail, four unit types visible, five retail buildings, no masonry kit when GLBs present.
