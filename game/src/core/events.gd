extends Node
## Global signal bus. Gameplay never connects view→sim directly.

signal content_reloaded
signal match_started
signal match_ended(winner_side: int)
signal selection_changed(ids: Array)
signal resources_changed(side: int, amount: float)
signal toast(message: String)
signal entity_spawned(kind: StringName, id: int)
signal entity_died(kind: StringName, id: int)
signal building_completed(id: int)
signal ability_used(battalion_id: int, ability_id: StringName)
signal fog_dirty
