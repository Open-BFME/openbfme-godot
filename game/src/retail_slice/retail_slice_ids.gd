extends RefCounted
## The handful of slice content ids and document bounds that surfaces OUTSIDE the
## tactical slice have to be able to name.
##
## WHY THIS FILE EXISTS - a measured boot cost, not a taxonomy exercise.
## `main_menu.gd` needs six values from `retail_vertical_slice.gd`: the soldier /
## horde object ids, the entry map id, the five-maps pack id, and the two bounded
## read limits. Reaching them through `RetailVerticalSlice` meant the menu had to
## COMPILE the slice, and the slice's preload chain is 57 files / ~60,400 lines -
## the whole simulation, HUD, renderer batcher and script executor - which is
## roughly two thirds of everything the shell was compiling before it could draw a
## button. Six string constants were dragging in the entire game.
##
## So the constants live here, in a leaf with no preloads of its own, and
## `retail_vertical_slice.gd` derives its own public constants FROM this file.
## There is still exactly ONE definition of each value: every existing
## `RetailVerticalSlice.SOLDIER_OBJECT_ID` reader keeps working and keeps reading
## the same bytes. This is a re-home, not a second copy - a second copy of an
## object id is precisely how two surfaces come to disagree about which pack can
## host a match.
##
## RULE FOR THIS FILE: it must never `preload` anything and must never grow
## behaviour. The moment it does, it stops being reachable for free and the boot
## cost comes straight back.
##
## Reached by `preload`, deliberately NOT by `class_name` - same reason
## `src/core/boot_profile.gd` documents: a non-editor run resolves global class
## names from `.godot/global_script_class_cache.cfg`, which only the editor
## writes, so a fresh checkout would fail to resolve the name at all.

## The Men vertical slice's battalion member and its horde container. The menu's
## pack gate asks ContentDB for both before it will admit a launch.
const SOLDIER_OBJECT_ID := "bfme2.object.gondor-fighter"
const SOLDIER_HORDE_ID := "bfme2.object.gondor-fighter-horde"
## The entry map the slice boots from the selected faction pack's files.entryMap.
const MAP_ID := "bfme2.map.fords-of-isen-ii"
## The five-maps supplement pack. The slice resolves non-default slice maps from
## this pack's catalog when ContentDB has not registered it yet (the integration
## step registers it in selection.json as a supplemental pack).
const FIVE_MAPS_PACK_ID := "bfme2-five-maps-106-private"
## Bounded-read guardrails for pack documents. Every JSON read of a pack-supplied
## catalog or map document is capped, so a malformed or hostile pack cannot make
## the shell allocate without limit.
const MAP_CATALOG_MAX_BYTES := 1024 * 1024
const MAP_DOCUMENT_MAX_BYTES := 2 * 1024 * 1024
