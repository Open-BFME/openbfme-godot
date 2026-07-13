class_name Stage2Construction
extends RefCounted
## Monotonic construction HP ramp that never erases damage already taken.

static func advance(building: Stage2Types.Building, definition: Stage2Types.BuildingDefinition, tick_index: int) -> void:
	if building.completed or building.destroyed or building.maximum_health <= 0:
		return
	var old_cap := building.construction_health_cap
	building.progress_ticks += 1
	building.construction_health_cap = maxi(1, building.maximum_health * building.progress_ticks / building.construction_ticks)
	building.health = mini(building.construction_health_cap, building.health + building.construction_health_cap - old_cap)
	if building.progress_ticks >= building.construction_ticks:
		building.completed = true
		building.construction_health_cap = building.maximum_health
		building.health = mini(building.maximum_health, building.health)
	if building.completed and definition.income_interval_ticks > 0:
		building.next_income_tick = tick_index + definition.income_interval_ticks

static func apply_damage(building: Stage2Types.Building, amount: int) -> bool:
	if amount <= 0 or not building.is_alive():
		return false
	var applied := mini(amount, building.health)
	building.damage_taken += applied
	building.health = maxi(0, building.health - amount)
	if building.health == 0:
		building.destroyed = true
	return building.destroyed
