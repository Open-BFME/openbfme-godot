extends SceneTree
## Ships path on cooked water. Land units keep the land grid.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")

const EXPECTED_CHECKS := 8
const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
var _watchdog := RunnerWatchdogScript.new()
var passed := 0
var failed := 0


func _initialize() -> void:
	_watchdog.start(self, "NAVAL_WATER_ROUTE")
	call_deferred("_run")


func _run() -> void:
	_test_naval_row_classification()
	_test_land_row_is_not_naval()
	_test_assign_route_uses_water_for_ships()
	_test_naval_row_never_borrows_the_land_grid()
	_finish()


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("NAVAL_WATER_ROUTE PASS %s" % label)
	else:
		failed += 1
		printerr("NAVAL_WATER_ROUTE FAIL %s%s" % [label, "" if detail == "" else " (%s)" % detail])


func _test_naval_row_classification() -> void:
	var sim = SimScript.new()
	_check("category_naval_is_naval", bool(sim._is_naval_row({"category": "naval"})))
	_check("kindof_ship_is_naval", bool(sim._is_naval_row({"kind_of": ["SHIP", "SELECTABLE"]})))


func _test_land_row_is_not_naval() -> void:
	var sim = SimScript.new()
	_check("infantry_is_not_naval", not bool(sim._is_naval_row({"category": "infantry", "kind_of": ["INFANTRY"]})))


func _test_assign_route_uses_water_for_ships() -> void:
	var sim = SimScript.new()
	var provider := _WaterProvider.new()
	sim.route_provider = provider
	var ship := {
		"position": Vector2.ZERO,
		"category": "naval",
		"kind_of": ["SHIP"],
		"flying": false,
	}
	var ok := bool(sim._assign_route(ship, Vector2(20, 0)))
	_check("ship_uses_water_query", ok and provider.water_queries == 1 and provider.land_queries == 0, "water=%d land=%d" % [provider.water_queries, provider.land_queries])
	var soldier := {
		"position": Vector2.ZERO,
		"category": "infantry",
		"kind_of": ["INFANTRY"],
		"flying": false,
	}
	sim.parity = _PassParity.new()
	var land_ok := bool(sim._assign_route(soldier, Vector2(20, 0)))
	_check("infantry_uses_land_query", land_ok and provider.land_queries == 1, "land=%d" % provider.land_queries)


func _test_naval_row_never_borrows_the_land_grid() -> void:
	## A ship whose route provider cannot answer for water has no navigable
	## domain. It must be refused by name; borrowing query_route would path a
	## hull across land and report success.
	var ship := {
		"position": Vector2.ZERO,
		"category": "naval",
		"kind_of": ["SHIP"],
		"flying": false,
	}
	var land_only = SimScript.new()
	var provider := _LandOnlyProvider.new()
	land_only.route_provider = provider
	var borrowed := bool(land_only._assign_route(ship.duplicate(true), Vector2(20, 0)))
	_check(
		"land_only_provider_refuses_ship_by_name",
		not borrowed and String(land_only.last_route_rejection) == "water-navigation-unavailable",
		"assigned=%s reason=%s" % [borrowed, land_only.last_route_rejection],
	)
	_check(
		"land_only_provider_is_never_queried_for_a_ship",
		provider.land_queries == 0,
		"land=%d" % provider.land_queries,
	)

	var no_provider = SimScript.new()
	no_provider.route_provider = null
	var fabricated := bool(no_provider._assign_route(ship.duplicate(true), Vector2(20, 0)))
	_check(
		"absent_provider_refuses_ship_instead_of_fabricating_a_route",
		not fabricated and String(no_provider.last_route_rejection) == "water-navigation-unavailable",
		"assigned=%s reason=%s" % [fabricated, no_provider.last_route_rejection],
	)


func _finish() -> void:
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		printerr("NAVAL_WATER_ROUTE FAIL expected_checks passed=%d failed=%d expected=%d" % [passed, failed - 1, EXPECTED_CHECKS])
	print("NAVAL_WATER_ROUTE_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)


class _WaterProvider:
	var water_queries := 0
	var land_queries := 0

	func query_route(_from: Vector2, to: Vector2) -> Dictionary:
		land_queries += 1
		return {"valid": true, "reason": "", "points": [to], "cells": [], "ford_name": ""}

	func query_water_route(_from: Vector2, to: Vector2) -> Dictionary:
		water_queries += 1
		return {"valid": true, "reason": "", "points": [to], "cells": [], "surface": "water"}


class _PassParity:
	func can_path_between(_from: Vector2, _to: Vector2) -> bool:
		return true


class _LandOnlyProvider:
	## Deliberately has no query_water_route: the shape a ship must refuse.
	var land_queries := 0

	func query_route(_from: Vector2, to: Vector2) -> Dictionary:
		land_queries += 1
		return {"valid": true, "reason": "", "points": [to], "cells": [], "ford_name": ""}
