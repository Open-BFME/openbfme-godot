extends SceneTree

const Registry := preload("res://src/script/handlers/_registry.gd")
const Dispatch := preload("res://src/script/script_dispatch.gd")
const CoreHandlers := preload("res://src/script/script_handlers_core.gd")
const Env := preload("res://src/script/script_env.gd")
const Vocabulary := preload("res://src/script/script_vocabulary.gd")
const ParamTypes := preload("res://src/script/script_param_types.gd")

const EXPECTED_CHECKS := 10

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var dispatch := Dispatch.new()
	CoreHandlers.register_all(dispatch)
	var registration := Registry.register_all(dispatch)
	var world := HeroWorld.new()
	var env := Env.new()

	_check("registry_is_clean", (registration["errors"] as Array).is_empty())
	_check("package_is_loaded", (registration["packages"] as Array).has("WP08-hero-objectives"))
	_check("condition_is_coverage_counted", dispatch.implemented_conditions().has("ANY_HERO_REACHED_RANK"))

	world.progression_facet.answer = SageWorldQuery.hit(true)
	var yes := dispatch.call_by_name(
		Vocabulary.Kind.CONDITION,
		"ANY_HERO_REACHED_RANK",
		[_player("PlayerHuman"), _integer(2), _integer(7)],
		env,
		world,
		"fixture"
	)
	_check("true_query_dispatches", int(yes["status"]) == Dispatch.Status.OK)
	_check("true_query_answer_is_preserved", bool(yes["result"]))
	_check(
		"all_arguments_are_forwarded_positionally",
		world.progression_facet.calls == [["PlayerHuman", 2, 7]]
	)

	world.progression_facet.answer = SageWorldQuery.hit(false)
	var no := dispatch.call_by_name(
		Vocabulary.Kind.CONDITION,
		"ANY_HERO_REACHED_RANK",
		[_player("PlayerHuman"), _integer(1), _integer(10)],
		env,
		world,
		"fixture"
	)
	_check("false_query_dispatches", int(no["status"]) == Dispatch.Status.OK)
	_check("false_query_answer_is_preserved", not bool(no["result"]))

	world.progression_facet.answer = SageWorldQuery.refused(
		"progression.any_hero_reached_rank", "unknown authored hero rank"
	)
	var refused := dispatch.call_by_name(
		Vocabulary.Kind.CONDITION,
		"ANY_HERO_REACHED_RANK",
		[_player("PlayerHuman"), _integer(1), _integer(10)],
		env,
		world,
		"fixture"
	)
	_check("world_refusal_is_not_false", int(refused["status"]) == Dispatch.Status.WORLD_REFUSED)
	_check(
		"world_refusal_names_the_progression_query",
		"\n".join(dispatch.gaps.to_lines()).contains("progression.any_hero_reached_rank")
	)

	world.progression_facet.world = null
	world.progression_facet = null
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("HANDLERS_WP08 FAIL liveness: ran %d expected %d" % [ran, EXPECTED_CHECKS])
	print("HANDLERS_WP08_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _player(value: String) -> Dictionary:
	return {
		"argumentType": ParamTypes.ARGUMENT_PLAYER,
		"integer": 777,
		"real": 0.0,
		"text": value,
	}


func _integer(value: int) -> Dictionary:
	return {
		"argumentType": ParamTypes.ARGUMENT_INTEGER,
		"integer": value,
		"real": 0.0,
		"text": "DECOY",
	}


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		printerr("FAIL %s" % label)


class HeroProgression:
	extends SageScriptWorld.Progression

	var calls: Array = []
	var answer: SageWorldQuery = SageWorldQuery.hit(false)

	func any_hero_reached_rank(
		player: String, hero_count: int, rank: int
	) -> SageWorldQuery:
		calls.append([player, hero_count, rank])
		return answer


class HeroWorld:
	extends SageScriptWorld

	var progression_facet := HeroProgression.new()

	func _init() -> void:
		progression_facet.world = self

	func progression() -> SageScriptWorld.Progression:
		return progression_facet
