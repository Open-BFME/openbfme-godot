class_name Stage2Placement
extends RefCounted
## Deterministic cell placement and rally-spawn queries.

static func footprint_cells(position: Vector2i, width_cells: int, height_cells: int) -> Array[Vector2i]:
	var center := Vector2i(position.x / Stage1Grid.CELL_SIZE, position.y / Stage1Grid.CELL_SIZE)
	var first := center - Vector2i(width_cells / 2, height_cells / 2)
	var result: Array[Vector2i] = []
	for y in range(first.y, first.y + height_cells):
		for x in range(first.x, first.x + width_cells):
			result.append(Vector2i(x, y))
	return result

static func can_place(grid: Stage1Grid, position: Vector2i, definition: Stage2Types.BuildingDefinition) -> bool:
	if Stage1Grid.cell_center(grid.to_cell(position)) != position:
		return false
	for cell in footprint_cells(position, definition.width_cells, definition.height_cells):
		if not grid.contains(cell) or grid.is_blocked(cell):
			return false
	return true

static func set_footprint_blocked(grid: Stage1Grid, building: Stage2Types.Building, definition: Stage2Types.BuildingDefinition, blocked: bool) -> void:
	for cell in footprint_cells(building.position, definition.width_cells, definition.height_cells):
		if grid.contains(cell):
			grid.set_blocked(cell, blocked)

static func find_spawn(grid: Stage1Grid, origin_position: Vector2i, maximum_radius_cells: int, include_center: bool = true) -> Vector2i:
	var origin := grid.to_cell(origin_position)
	var queue: Array[Vector2i] = [origin]
	var distances: Array[int] = [0]
	var visited: Dictionary = {origin: true}
	var index := 0
	while index < queue.size():
		var cell := queue[index]
		var distance := distances[index]
		index += 1
		if (include_center or distance > 0) and not grid.is_blocked(cell):
			return Stage1Grid.cell_center(cell)
		if distance >= maximum_radius_cells:
			continue
		for offset in Stage1Grid.NEIGHBORS:
			var neighbor := cell + offset
			if not grid.contains(neighbor) or visited.has(neighbor):
				continue
			visited[neighbor] = true
			queue.append(neighbor)
			distances.append(distance + 1)
	return Vector2i(-1, -1)
