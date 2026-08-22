# Lane RADAR — "the radar blends into the terrain"

See _common.md. Owner report: in the screenshot the minimap (bottom-left dish) is a near-uniform tan parchment; the map's terrain/features are barely distinguishable — the owner says the radar "blends into the terrain". Retail's radar shows a clearly readable terrain bitmap (map texture colours with visible roads/cliffs/water) inside the palantir dish, with the parchment/frame art only as border.

Prior work: orchestration\reports\report-radar.md (Q64, merged 561612d5) claims "terrain bitmap matches authored evidence" — treat that claim as UNPROVEN against this screenshot. Code: game\src\retail_slice\retail_minimap.gd, retail_palantir_frame.gd, retail_shroud_overlay.gd, retail_hud.gd (radar section). Retail captures referenced in report-radar.md (Fords 500s/510s).

Tasks:
1. Diagnose WHY the terrain reads as flat parchment. Candidates: parchment/shroud overlay drawn on top of (or blended with) the terrain bitmap with the wrong order/alpha; the radar bitmap built from a low-contrast source (e.g. base texture average) instead of retail's per-cell terrain texture colour with cliff/water/road classes; a shroud mask covering explored area; a modulate applied to the whole dish. Measure it: dump the composed radar texture to a PNG from a headless runner and compare mean/contrast against a retail capture of the same map.
2. Look at the retail files: how retail builds the radar image (terrain texture colours per cell, water colours, cliffs darkened, shroud as dark overlay NOT parchment). Cite file:line from the retail INI/APT/art.
3. Fix so the radar clearly shows terrain and the parchment art is only the frame. Keep the Q64 pins green or re-pin them ONLY with a capture that proves the new composition (attach the PNG under workspace\logs\radar-owner\).
4. Failing-first runner check (radar runner in game\tests\) asserting the composed radar has terrain contrast above a measured retail threshold and that the shroud layer does not tint explored cells.

Gates: radar runner (16/0), HUD runner (124/0), castle live boot runner (8/0) — state before/after.
