# Lane HUD — "the hero selection on the bottom left isn't fitted to the graphics"

See _common.md. Owner report + screenshot: at 1920x1080 the bottom-left HUD shows only the palantir dish frame; the command buttons (fortress hero page: portraits) are laid out in a bare circle to the right of the dish with NO control-bar artwork behind them; the "Level: 1" text sits in an empty ring; the resource strip floats; buttons overlap the dish rim. Retail's control bar (controlbarscheme.ini, Palantir.apt / InGameHeroSelect / command button grid) is a full-width bar with the dish on the left, the command buttons in an authored grid on the bar art, and the selected object's portrait + level + curved health arc inside the dish.

Prior lanes Q37/Q38/Q39 (orchestration\queue.md rows 54-56) laid out the HUD from Palantir.apt via tools\dump_apt_placements.py + game\src\retail_slice\retail_hud_stage.gd, but the screenshot proves the shipped result does not render the bar art / grid. Code: game\src\retail_slice\retail_hud.gd (~6000 lines), retail_hud_stage.gd, retail_hud_apt_runtime.gd, retail_hud_wnd_runtime.gd, retail_palantir_frame.gd.

Tasks:
1. Reproduce: headless HUD runner with a 1920x1080 viewport, capture the HUD to PNG (workspace\logs\hud-owner\), confirm the missing bar art + circular button layout. Find WHY: is the bar art texture missing from the selected pack (then say which pack and what the importer must cook — a scratch cook outside the sealed pack is fine), is the stage mapping wrong at 16:9, is the radial ring being used instead of the authored grid?
2. Read the retail files: controlbarscheme.ini (bar art positions), the APT placement dump, command button grid positions; cite file:line.
3. Fix: bar art rendered full-width, scaled from the 1024x768 stage to the window with retail's aspect handling (bar anchored to the bottom, not stretched vertically), command buttons in the authored grid on the bar, palantir dish shows the selected object's portrait + level + curved health arc; the world radial (Q39) stays but must not replace the bar grid. Hero roster bar (Q38) at its authored anchor.
4. Failing-first test in the HUD runner asserting (a) the bar art node is visible and spans the window width, (b) command buttons lie inside the authored grid rect, (c) no button overlaps the dish rect.

Gates: HUD runner (124/0), radial runner (48/0), boot runner — state before/after, attach the after PNG.
