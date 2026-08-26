extends "res://src/retail_slice/retail_sim_subsystem.gd"
## Fog of war carved out of retail_slice_sim.gd (drawer 19): shroud grid stepping, clearing radii, vision-driven fog refresh.
## State stays on the sim; the sim keeps one-line delegates under the original names.



func fog_of_war():
	## The retail shroud grid for this match. Always returns a model - a match
	## with fog off gets one whose `enabled` is false and which no tick ever
	## stamps, so a presentation query is a cheap "everything is visible" rather
	## than a null check at every call site.
	if sim._fog_of_war == null:
		sim._fog_of_war = sim.FogOfWarScript.new()
		sim._fog_of_war.enabled = sim.fog_of_war_enabled
		var scale = float(sim._rules.get("source_map_transform_scale", 0.0))
		if scale <= 0.0:
			# No map transform in the rules (bare harness sims). One sim unit per
			# source unit is the only honest fallback; the cell then spans
			# PartitionCellSize directly.
			scale = 1.0
		if sim.playable_outline.size() >= 3:
			var bounds_min = sim.playable_outline[0]
			var bounds_max = sim.playable_outline[0]
			for point in sim.playable_outline:
				bounds_min = Vector2(minf(bounds_min.x, point.x), minf(bounds_min.y, point.y))
				bounds_max = Vector2(maxf(bounds_max.x, point.x), maxf(bounds_max.y, point.y))
			# One shroud cell of margin so a unit nudged onto the border by
			# eviction still has a cell of its own.
			var margin = Vector2.ONE * sim.FogOfWarScript.cell_size_for_scale(scale)
			sim._fog_of_war.configure(bounds_min - margin, bounds_max + margin, scale)
		else:
			sim._fog_of_war.configure_default(scale)
	return sim._fog_of_war


func _shroud_clearing_radius(row: Dictionary) -> float:
	## Retail authors TWO independent ranges and this is the deshroud one.
	##   gamedata.ini:38-64 - SHROUD_CLEAR_* and VISION_* are separate macro
	##   families, and the shipped objects disagree constantly: MenFortressCitadel
	##   is VisionRange 400 / ShroudClearingRange 800, GondorSentryTower is
	##   600 / 500. Deriving one from the other would be wrong in both directions.
	## Falls back to vision_range only when the pack carries no deshroud value,
	## which is every unit compiled before this lane's importer change rides a
	## republish - named as a live gap in the report rather than hidden here.
	var shroud_range := float(row.get("shroud_clearing_range", 0.0))
	if shroud_range > 0.0:
		return shroud_range
	return float(row.get("vision_range", 0.0))


func source_transform_scale() -> float:
	## Source (retail) units to sim units for the loaded map. 1.0 when no map
	## transform is configured, which is what a bare harness sim gets.
	var scale = float(sim._rules.get("source_map_transform_scale", 0.0))
	return scale if scale > 0.0 else 1.0


func refresh_fog_of_war() -> void:
	## Stamp the shroud grid once, outside the tick. The presentation calls this
	## when a match is bound so the first frame already shows the player's own
	## base cleared; without it a match opens on a fully black screen that pops
	## clear one tick later.
	_step_shroud_grid()


func _step_shroud_grid() -> void:
	## The retail look pass: rebuild every team's clear set from scratch from the
	## authoritative entity and structure rows. A full rebuild rather than
	## retail's incremental refcount because the result is identical and a
	## rebuild cannot accumulate error across 3000 ticks; the one behavioural
	## difference is UnlookPersistDuration (gamedata.ini:9024, retail 1), the
	## delay before a vacated cell falls back to fog, which this model does not
	## implement. Named in the report.
	##
	## Iteration order follows sim.entity_ids()/sim.structure_ids(), the same sorted
	## order every other step uses, so two peers stamp in the same order.
	if not sim.fog_of_war_enabled:
		return
	var fog = fog_of_war()
	fog.begin_look_pass()
	for eid in sim.entity_ids():
		if not sim.entities.has(eid):
			continue
		var row: Dictionary = sim.entities[eid]
		if int(row.get("health", 0)) <= 0:
			continue
		var team := int(row.get("team", -1))
		if team < 0:
			continue
		var radius := _shroud_clearing_radius(row)
		if radius <= 0.0:
			continue
		# The entity id IS the looker key, which is what makes the pass
		# incremental: a battalion that has not left its shroud cell since last
		# tick costs one dictionary lookup here and touches no cell at all.
		fog.add_look(team, row.get("position", Vector2.ZERO), radius, eid)
	for sid in sim.structure_ids():
		if not sim.structures.has(sid):
			continue
		var srow: Dictionary = sim.structures[sid]
		if int(srow.get("health", 0)) <= 0:
			continue
		var steam := int(srow.get("team", -1))
		if steam < 0:
			continue
		var sradius := _structure_shroud_clearing_radius(srow)
		if sradius <= 0.0:
			continue
		# Structure ids and entity ids share a numbering space in places, so the
		# structure key is negated to keep the two looker sets disjoint.
		fog.add_look(steam, srow.get("position", Vector2.ZERO), sradius, -sid)
	fog.commit_look_pass()


func _structure_shroud_clearing_radius(srow: Dictionary) -> float:
	## THE BUG THIS EXISTS TO FIX. Structure rows are built at eight separate
	## sites in this file and NOT ONE of them ever wrote `vision_range`,
	## `shroud_clearing_range` or `footprint_radius`, so the generic
	## `_shroud_clearing_radius` read 0 from every building ever placed and the
	## structure half of the look pass was dead code. The owner's first fog-on
	## playtest found it immediately: "I built a fortress and it gives no fog
	## visibility."
	##
	## Rather than teach eight construction sites the same field, the radius is
	## resolved from the row's kind against the team's compiled build rules -
	## which is where the manifest already puts every other authored number - so
	## a map-seeded fortress, a porter-built farm, an expansion pad tower, an
	## unpacked base and a summoned Lone Tower all get their vision by the same
	## path, the moment they exist.
	var direct := float(srow.get("shroud_clearing_range", 0.0))
	if direct > 0.0:
		return direct
	var rule: Dictionary = sim.structure_build_rules_for_team(
		int(srow.get("team", -1))
	).get(String(srow.get("structure_kind", "")), {}) as Dictionary
	var source := float(rule.get("shroud_clearing_range_source", 0.0))
	if source <= 0.0:
		return 0.0
	return source * source_transform_scale()


func _step_fog_from_vision() -> void:
	## FoW consumer: living sim.entities reveal fog for their team using vision_range.
	sim._ensure_parity()
	for eid in sim.entity_ids():
		if not sim.entities.has(eid):
			continue
		var row: Dictionary = sim.entities[eid]
		if int(row.get("health", 0)) <= 0:
			continue
		var team := int(row.get("team", -1))
		if team < 0:
			continue
		var vision := float(row.get("vision_range", 0.0))
		if vision <= 0.0:
			continue
		sim.parity.fog_reveal(team, row.get("position", Vector2.ZERO), vision, false)


