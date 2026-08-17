@echo off
setlocal
set "ROOT=%~dp0"
call "%ROOT%tools\resolve-godot.bat"
if errorlevel 1 exit /b 1
REM Private retail packs + reviewed ranger overlay (required for Men rangers).
if not defined OPENBFME_CONTENT set "OPENBFME_CONTENT=%ROOT%workspace\content-packs"
if not defined OPENBFME_REVIEWED_RANGER_OVERLAY_SHA256 set "OPENBFME_REVIEWED_RANGER_OVERLAY_SHA256=3e6399441fdfec38009ba2465e9249d57acb961934907c5839f5744be48df116"
REM The War of the Ring opponent's retail preference weights. No converter emits
REM this as a bundle yet (see the OPENBFME_LIVING_WORLD_AI_TEMPLATE row in
REM tools/wotr-data-staging.ps1), so the workspace copy is pointed at directly.
REM Without it the AI still takes its turn - it just ranks regions by this
REM project's own rules instead of by retail's BonusPreference numbers.
set "OPENBFME_WOTR_AI_TEMPLATE_DEFAULT=%ROOT%workspace\retail-work\editions\rotwk\cache\layered-effective-assets\data\ini\livingworldaitemplate.ini"
if not defined OPENBFME_LIVING_WORLD_AI_TEMPLATE if exist "%OPENBFME_WOTR_AI_TEMPLATE_DEFAULT%" set "OPENBFME_LIVING_WORLD_AI_TEMPLATE=%OPENBFME_WOTR_AI_TEMPLATE_DEFAULT%"

REM WAR OF THE RING'S CONVERTED BUNDLES.
REM
REM WHY THESE ARE HERE. A BUILT bundle carries these beside its own content
REM packs and the loaders find them with no environment set - that is what
REM tools\Probe-WotrBundles.ps1 proves. Running FROM SOURCE is different: the
REM workspace's content-packs directory holds the faction packs only, and the
REM living-world bundles live under workspace\retail-work, where nothing on the
REM pack-relative search path reaches them.
REM
REM WHAT THEIR ABSENCE COSTS, which is why this is not cosmetic: the strategic
REM screen still opens, but it falls back to drawn chrome - an empty palantir
REM ring, no tray cards, no tab captions - and every one of those absences is
REM reported honestly on the diagnostics panel (F1). A capture taken without
REM OPENBFME_STRATEGIC_UI photographs that fallback, which has already been
REM mistaken once for a regression in the HUD.
REM
REM Each line is skipped if the operator already set it, and skipped if the
REM directory is absent, so a checkout without the private workspace still runs.
set "OPENBFME_WOTR_WORK=%ROOT%workspace\retail-work"
if not defined OPENBFME_LIVING_WORLD_DOC if exist "%OPENBFME_WOTR_WORK%\reports\rotwk-living-world.json" set "OPENBFME_LIVING_WORLD_DOC=%OPENBFME_WOTR_WORK%\reports\rotwk-living-world.json"
if not defined OPENBFME_LIVING_MAP if exist "%OPENBFME_WOTR_WORK%\livingmap" set "OPENBFME_LIVING_MAP=%OPENBFME_WOTR_WORK%\livingmap"
if not defined OPENBFME_LIVING_MAP_REGIONS if exist "%OPENBFME_WOTR_WORK%\livingmap-regions" set "OPENBFME_LIVING_MAP_REGIONS=%OPENBFME_WOTR_WORK%\livingmap-regions"
if not defined OPENBFME_LIVING_WORLD_MARKERS if exist "%OPENBFME_WOTR_WORK%\livingworld-markers" set "OPENBFME_LIVING_WORLD_MARKERS=%OPENBFME_WOTR_WORK%\livingworld-markers"
if not defined OPENBFME_LIVING_WORLD_REGION_IMAGES if exist "%OPENBFME_WOTR_WORK%\livingworld-region-images" set "OPENBFME_LIVING_WORLD_REGION_IMAGES=%OPENBFME_WOTR_WORK%\livingworld-region-images"
if not defined OPENBFME_LIVING_WORLD_AUTORESOLVE if exist "%OPENBFME_WOTR_WORK%\livingworld-autoresolve" set "OPENBFME_LIVING_WORLD_AUTORESOLVE=%OPENBFME_WOTR_WORK%\livingworld-autoresolve"
if not defined OPENBFME_STRATEGIC_UI if exist "%OPENBFME_WOTR_WORK%\strategic-ui" set "OPENBFME_STRATEGIC_UI=%OPENBFME_WOTR_WORK%\strategic-ui"

"%OPENBFME_GODOT%" --path "%ROOT%game"
exit /b %ERRORLEVEL%
