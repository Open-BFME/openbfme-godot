class_name Stage2Production
extends RefCounted
## Population accounting helpers shared by queue validation and tests.

static func used_population(world: Stage1World, economy: Stage2Types.TeamEconomy, horde_population: Dictionary) -> int:
	var result := 0
	for horde in world.hordes:
		if horde.team == economy.team and horde.is_alive():
			result += int(horde_population.get(horde.id, 1))
	return result

static func reserved_population(economy: Stage2Types.TeamEconomy, buildings: Array[Stage2Types.Building]) -> int:
	var result := 0
	for building in buildings:
		if building.team != economy.team or building.destroyed:
			continue
		for job in building.jobs:
			result += job.population_reserved
	return result

static func can_reserve(world: Stage1World, economy: Stage2Types.TeamEconomy, buildings: Array[Stage2Types.Building], horde_population: Dictionary, amount: int) -> bool:
	return used_population(world, economy, horde_population) + reserved_population(economy, buildings) + amount <= economy.population_cap
