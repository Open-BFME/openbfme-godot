extends SceneTree

const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const Audio = preload("res://src/retail_slice/retail_slice_audio.gd")
var passed := 0
var failed := 0


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "PLAYABLE_UNIT_RUNTIME_CONSUMER_RUNNER")
	call_deferred("_run")


func _run() -> void:
	var content_db = root.get_node_or_null("ContentDB")
	if content_db == null:
		_fail("ContentDB autoload is missing")
		_finish()
		return
	var pack_root := ProjectSettings.globalize_path("user://playable-unit-runtime-consumer-fixture")
	_build_fixture(pack_root)
	content_db.playable_unit_runtimes.clear()
	if not content_db.pack_roots.has(pack_root):
		content_db.pack_roots.append(pack_root)
	var declared := {"playableUnit.fixturemonster": "data/playable-units/fixturemonster.json"}
	_check(content_db._load_playable_unit_runtimes(pack_root, declared), "generic declaration loads")
	var all_runtimes: Dictionary = content_db.get_playable_unit_runtimes()
	_check(all_runtimes.size() == 1 and all_runtimes.has("FixtureMonster"), "runtime indexes by source object id")
	var document: Dictionary = content_db.get_playable_unit_runtime("FixtureMonster")
	_check(String(document.get("_pack_file_key", "")) == "playableUnit.fixturemonster", "pack declaration identity retained")
	var missing_packaged_clip := document.duplicate(true)
	var missing_clip_visual := (
		(missing_packaged_clip.get("registration", {}) as Dictionary).get("visual", {})
		as Dictionary
	)
	(
		(missing_clip_visual.get("authoredStateLabels", []) as Array)[0]
		as Dictionary
	)["linkedAnimations"] = [{"identifier": "fixture_missing", "conditions": ["USER_9"]}]
	_check(
		not content_db._validate_playable_unit_runtime(pack_root, missing_packaged_clip),
		"state label cannot name a packaged clip absent from the document closure"
	)
	var unsealed_packaged_clip := document.duplicate(true)
	(unsealed_packaged_clip.get("resourceIds", []) as Array).erase("fixture-model")
	_check(
		not content_db._validate_playable_unit_runtime(pack_root, unsealed_packaged_clip),
		"state label cannot rely on a component resource absent from resourceIds"
	)
	var cross_object_packaged_clip := document.duplicate(true)
	var cross_object_visual := (
		(cross_object_packaged_clip.get("registration", {}) as Dictionary).get("visual", {})
		as Dictionary
	)
	(
		(cross_object_visual.get("authoredAnimationStates", []) as Array)[0]
		as Dictionary
	)["ownerObjectId"] = "OtherObject"
	_check(
		not content_db._validate_playable_unit_runtime(pack_root, cross_object_packaged_clip),
		"state label cannot cross-link another object's packaged animation"
	)
	var wrong_leaf_packaged_clip := document.duplicate(true)
	var wrong_leaf_visual := (
		(wrong_leaf_packaged_clip.get("registration", {}) as Dictionary).get("visual", {})
		as Dictionary
	)
	(
		(wrong_leaf_visual.get("authoredAnimationStates", []) as Array)[0]
		as Dictionary
	)["sourceW3d"] = "art/w3d/different_clip.w3d"
	_check(
		not content_db._validate_playable_unit_runtime(pack_root, wrong_leaf_packaged_clip),
		"hierarchical state label rejects a packaged animation with the wrong source leaf"
	)
	var fractional_animation_property := document.duplicate(true)
	var fractional_idle := (
		(
			((fractional_animation_property.get("registration", {}) as Dictionary)
			.get("visual", {}) as Dictionary).get("coreAnimations", {}) as Dictionary
		).get("idle", [])[0] as Dictionary
	)
	fractional_idle["AnimationBlendTime"] = 10.5
	((fractional_idle.get("authoredProperties", []) as Array)[0] as Dictionary)["value"] = 10.5
	_check(
		not content_db._validate_playable_unit_runtime(pack_root, fractional_animation_property),
		"fractional JSON animation properties fail closed"
	)
	var projected_member: Dictionary = content_db.get_bundle_object("bfme2.object.fixture-monster")
	_check(String(projected_member.get("sourceObjectId", "")) == "FixtureMonster", "runtime projects a generic bundle member")
	_check(String((projected_member.get("presentation", {}) as Dictionary).get("model", "")) == "assets/models/fixture.glb", "runtime projects the converted model")
	_check(not content_db.get_animation_capability(String(projected_member.get("animationCapabilityId", ""))).is_empty(), "runtime projects core animation capability")
	var hud := Adapter.hud_spec(document)
	_check(String(hud.get("unit_id", "")) == "bfme2.object.fixture-monster", "HUD unit id is descriptor-driven")
	_check(String(hud.get("image_id", "")) == "BIFixtureMonster", "HUD image is descriptor-driven")
	_check(int(hud.get("slot", 0)) == 4, "retail command slot retained")
	var hud_runtime = load("res://src/retail_slice/retail_hud.gd").new()
	_check(hud_runtime.enable_playable_unit_content({"FixtureMonster": document}) == "", "generic HUD registration succeeds")
	hud_runtime.build()
	var generic_button: Button = hud_runtime.train_buttons.get("bfme2.object.fixture-monster")
	_check(generic_button != null, "generic HUD builds imported train command")
	_check(generic_button != null and int(generic_button.get_meta("retail_command_slot", 0)) == 4, "generic HUD retains imported command slot")
	hud_runtime.free()
	var simulation := Adapter.simulation_rule(document)
	_check(String(simulation.get("display_name", "")) == "Fixture Monster", "simulation display name retained")
	_check(int(simulation.get("default_cost", -1)) == 700, "simulation build cost retained")
	_check(int(simulation.get("default_build_ticks", -1)) == 450, "simulation build time becomes deterministic ticks")
	_check(int(simulation.get("member_count", -1)) == 1, "single-object composition retained")
	_check(
		simulation.get("auto_acquire_enemies_when_idle", {}) == {
			"enabled": true,
			"attackBuildings": false,
			"whileStealthed": true,
		},
		"simulation retains exact idle-acquisition flags"
	)
	_check(
		int(simulation.get("mood_attack_check_rate_ms", 0)) == 500,
		"simulation retains authored mood-check milliseconds"
	)
	var status_document := document.duplicate(true)
	var status_registration := status_document.get("registration", {}) as Dictionary
	var status_source := (
		status_registration.get("simulation", {}) as Dictionary
	).duplicate(true)
	status_source["displayNameId"] = status_source.get("displayName", "")
	status_source.erase("displayName")
	status_registration["simulation"] = {
		"status": "ready",
		"resolved": status_source,
	}
	_check(
		not Adapter.simulation_rule(status_document).is_empty(),
		"status descriptor without LockWeaponCreate remains valid"
	)
	_check(
		not simulation.has("highlander_body"),
		"ordinary body keeps optional Highlander policy absent"
	)
	var highlander_document: Dictionary = document.duplicate(true)
	var highlander_registration := (
		highlander_document["registration"] as Dictionary
	)
	var highlander_simulation_row := (
		highlander_registration.get("simulation", {}) as Dictionary
	)
	highlander_simulation_row["highlanderBody"] = true
	var highlander_simulation := Adapter.simulation_rule(highlander_document)
	var highlander_rule := Adapter.normalized_unit_rule(
		highlander_simulation, 0.1
	)
	_check(
		highlander_simulation.get("highlander_body") == true
			and highlander_rule.get("highlander_body") == true,
		"authored Highlander policy survives both runtime adapter stages"
	)
	var producers: Array = simulation.get("producers", [])
	_check(producers.size() == 2 and String((producers[0] as Dictionary).get("producer_source_object_id", "")) == "UniversalMonsterPen", "all producers are descriptor-driven")
	var sim = Sim.new()
	sim._apply_gameplay_rules({
		"enable_base_loop": true,
		"playable_unit_runtimes": {"FixtureMonster": document},
		"producer_kind_by_source_object": {"UniversalMonsterPen": "fixture_monster_pen", "AlternateMonsterPen": "alternate_monster_pen"},
		"unit_rules": {},
		"starting_resources": 1200,
		"source_map_transform_scale": 0.1,
	})
	_check(sim.configuration_error == "", "generic simulation registration succeeds")
	_check(sim.production_rule_ids().has("bfme2.object.fixture-monster"), "generic production roster contains imported unit")
	_check((sim._unit_production_rules["bfme2.object.fixture-monster"] as Dictionary).get("producer_kinds", []) == ["fixture_monster_pen", "alternate_monster_pen"], "all producer routes reach simulation")
	sim.structures[1] = {
		"id": 1, "team": 0, "health": 1000, "construction_progress": 1.0,
		"structure_kind": "fixture_monster_pen",
		"production": ["bfme2.object.fixture-monster"], "queue": [],
		"completed_upgrades": ["Upgrade_MonsterPenLevel2"],
	}
	var queued: Dictionary = sim.queue_unit(0, 1, "bfme2.object.fixture-monster")
	_check(bool(queued.get("ok", false)), "imported unit enters production queue")
	sim.advance(450)
	var spawned := sim.entity(10)
	_check(not spawned.is_empty(), "imported unit completes production and spawns")
	_check(String(spawned.get("object_id", "")) == "bfme2.object.fixture-monster", "spawned identity remains descriptor-driven")
	_check(int(spawned.get("member_count", 0)) == 1 and int(spawned.get("member_damage", 0)) == 200, "spawned simulation rule comes from descriptor")
	_check(
		bool(spawned.get("auto_acquire_enabled", false))
			and not bool(spawned.get("auto_acquire_attack_buildings", true))
			and bool(spawned.get("auto_acquire_while_stealthed", false)),
		"spawned battalion carries normalized idle-acquisition policy"
	)
	_check(
		int(spawned.get("mood_attack_check_rate_ticks", 0)) == 5
			and bool(spawned.get("mood_randomize_next_check", false))
			and not spawned.has("mood_next_check_tick"),
		"spawned battalion carries normalized mood cadence and sourced reset state"
	)
	var locked_document := document.duplicate(true)
	var locked_simulation := (
		(locked_document.get("registration", {}) as Dictionary).get("simulation", {})
		as Dictionary
	)
	var locked_combat := locked_simulation.get("combat", {}) as Dictionary
	locked_combat["weaponId"] = "FixturePrimarySword"
	locked_combat["weaponSlot"] = "PRIMARY"
	locked_simulation["permanentWeaponLocks"] = [{
		"slot": "PRIMARY",
		"state": "LOCKED_PERMANENTLY",
		"module": "LockWeaponCreate",
		"sourceIni": "data/ini/object/fixture/locked.ini",
		"line": 42,
	}]
	var same_slot_profile := locked_combat.duplicate(true)
	same_slot_profile["weaponId"] = "FixturePrimaryBow"
	same_slot_profile["damage"] = 150
	same_slot_profile["weaponSlot"] = "PRIMARY"
	var cross_slot_profile := locked_combat.duplicate(true)
	cross_slot_profile["weaponId"] = "FixtureSecondaryBow"
	cross_slot_profile["damage"] = 175
	cross_slot_profile["weaponSlot"] = "SECONDARY"
	locked_simulation["weaponModes"] = {
		"same_primary": same_slot_profile,
		"cross_secondary": cross_slot_profile,
	}
	var lock_adapter_rule := Adapter.normalized_unit_rule(
		Adapter.simulation_rule(locked_document), 0.1
	)
	_check(
		lock_adapter_rule.get("permanent_weapon_locks", []) == ["primary"],
		"permanent PRIMARY lock survives both runtime adapter stages"
	)
	var malformed_lock_document := locked_document.duplicate(true)
	(
		(
			(malformed_lock_document.get("registration", {}) as Dictionary).get(
				"simulation", {}
			) as Dictionary
		).get("combat", {}) as Dictionary
	).erase("weaponSlot")
	_check(
		Adapter.simulation_rule(malformed_lock_document).is_empty(),
		"permanent lock without its protected weapon slot fails closed"
	)
	var unproven_lock_document := locked_document.duplicate(true)
	var unproven_locks: Array = (
		(
			(unproven_lock_document.get("registration", {}) as Dictionary).get(
				"simulation", {}
			) as Dictionary
		).get("permanentWeaponLocks", []) as Array
	)
	(unproven_locks[0] as Dictionary).erase("sourceIni")
	_check(
		Adapter.simulation_rule(unproven_lock_document).is_empty(),
		"permanent lock without compiler provenance fails closed"
	)
	var lock_sim = Sim.new()
	lock_sim._rules["unit_rules"] = {
		"bfme2.object.fixture-locked": lock_adapter_rule,
	}
	lock_sim._add_battalion(
		99, 0, Vector2.ZERO, "Locked Fixture",
		"bfme2.object.fixture-locked", "bfme2.object.fixture-locked", 0
	)
	var locked_row: Dictionary = lock_sim.entities.get(99, {}) as Dictionary
	_check(
		locked_row.get("permanent_weapon_locks", []) == ["primary"],
		"LockWeaponCreate state is installed at battalion creation"
	)
	var initial_locked_row := locked_row.duplicate(true)
	_check(
		not lock_sim._apply_weapon_mode(locked_row, "missing_mode")
			and locked_row == initial_locked_row,
		"missing WeaponSet mode refuses without releasing the lock"
	)
	_check(
		lock_sim._apply_weapon_mode(locked_row, "default")
			and locked_row.get("permanent_weapon_locks", []) == ["primary"],
		"remaining on the current WeaponSet preserves the permanent lock"
	)
	_check(
		lock_sim._apply_weapon_mode(locked_row, "same_primary")
			and String(locked_row.get("active_weapon_mode", "")) == "same_primary"
			and int(locked_row.get("member_damage", 0)) == 150
			and (locked_row.get("permanent_weapon_locks", []) as Array).is_empty(),
		"same-slot WeaponSet transition implicitly releases the permanent lock"
	)
	locked_row = initial_locked_row.duplicate(true)
	lock_sim.entities[99] = locked_row
	_check(
		lock_sim._apply_weapon_mode(locked_row, "cross_secondary")
			and String(locked_row.get("active_weapon_mode", "")) == "cross_secondary"
			and int(locked_row.get("member_damage", 0)) == 175
			and (locked_row.get("permanent_weapon_locks", []) as Array).is_empty(),
		"cross-slot WeaponSet transition releases the lock before applying"
	)
	var broken_dismount := {
		"mounted": true,
		"weapon_toggle_mode": "mounted",
		"default_weapon_mode": "default",
		"weapon_modes": {"mounted": cross_slot_profile},
		"speed": 80.0,
		"speed_source": 80.0,
		"dismounted_speed": 50.0,
		"dismounted_speed_source": 50.0,
	}
	var broken_dismount_before := broken_dismount.duplicate(true)
	_check(
		not bool(
			lock_sim._apply_ability_mount_toggle(broken_dismount, {}).get(
				"ok", true
			)
		)
			and broken_dismount == broken_dismount_before,
		"failed dismount preflights weapon mode before mutating mount state"
	)
	var absent_document: Dictionary = document.duplicate(true)
	(((absent_document["registration"] as Dictionary)["simulation"]) as Dictionary).erase(
		"moodAttackCheckRate"
	)
	var absent_rule := Adapter.simulation_rule(absent_document)
	_check(
		not absent_rule.has("mood_attack_check_rate_ms"),
		"missing mood cadence remains absent from normalized simulation"
	)
	hud_runtime = load("res://src/retail_slice/retail_hud.gd").new()
	_check(hud_runtime.enable_playable_unit_content({"FixtureMonster": document}, {"UniversalMonsterPen": "fixture_monster_pen", "AlternateMonsterPen": "alternate_monster_pen"}) == "", "multi-producer HUD registration succeeds")
	hud_runtime.build()
	hud_runtime.set_production_state(["bfme2.object.fixture-monster"], true, 0, [], [], [], [], "alternate_monster_pen")
	generic_button = hud_runtime.train_buttons.get("bfme2.object.fixture-monster")
	_check(int(generic_button.get_meta("retail_command_slot", 0)) == 2, "selected producer route changes authored slot")
	_check(generic_button.position == hud_runtime.RETAIL_COMMAND_SLOT_SOURCE[1], "authored slot controls actual command socket")
	_check(generic_button.text == "CONTROLBAR:FixtureMonsterAlternate", "selected producer route changes command text")
	hud_runtime.free()
	_check(content_db.resolve_asset("assets/ui/fixture.png", pack_root) != "", "runtime UI remains pack-contained")
	var audio = Audio.new()
	root.add_child(audio)
	audio.playback_enabled = false
	audio._load_playable_unit_audio_routes()
	audio._bind_roster_voice_routes()
	_check(audio._active_roster_object_ids().has("bfme2.object.fixture-monster"), "generic audio roster is descriptor-driven")
	_check(bool(audio.route_roster_voice("bfme2.object.fixture-monster", "select", 1).get("ok", false)), "generic select voice resolves from contained binding")
	_check(bool(audio.route_roster_voice("bfme2.object.fixture-monster", "attack", 2).get("ok", false)), "generic attack voice resolves from contained binding")
	audio._consume_event({"kind": "production.complete", "sequence": 3, "object_id": "bfme2.object.fixture-monster", "target_id": 44})
	_check(String(audio.last_route_result.get("event_id", "")) == "FixtureMonsterVoiceCreated", "production completion routes object-level created voice")
	audio._consume_event({"kind": "production.queued", "sequence": 4, "unit_type": "bfme2.object.fixture-monster"})
	_check(String(audio.last_route_result.get("event_id", "")) == "FixtureMonsterVoicePurchase", "production queue routes command purchase voice")
	audio._consume_event({"kind": "production.queued", "sequence": 5, "unit_type": "bfme2.object.fixture-monster", "command_id": "Command_ConstructFixtureMonsterAlternate"})
	_check(String(audio.last_route_result.get("reason", "")) == "authored_silent", "alternate producer preserves authored-silent purchase route")
	audio.free()

	var malformed := _fixture_document()
	malformed["objectId"] = "BrokenMonster"
	malformed["registration"] = {}
	_write_json(pack_root.path_join("data/playable-units/broken.json"), malformed)
	var before: Dictionary = content_db.get_playable_unit_runtimes()
	var malformed_loaded: bool = content_db._load_playable_unit_runtimes(pack_root, {
		"playableUnit.second": "data/playable-units/fixturemonster.json",
		"playableUnit.broken": "data/playable-units/broken.json",
	})
	# Invalid documents are skipped with a diagnostic; the well-formed entries
	# of the same delta still load. Nothing malformed enters the registry.
	var after_malformed: Dictionary = content_db.get_playable_unit_runtimes()
	_check(
		malformed_loaded
			and after_malformed.has("FixtureMonster")
			and not after_malformed.has("BrokenMonster"),
		"malformed document is skipped while the valid delta entry loads"
	)
	_check(not after_malformed.has("BrokenMonster") and not before.has("BrokenMonster"), "skipped document leaves no registry trace")
	_check(not content_db.get_bundle_object("bfme2.object.fixture-monster").is_empty(), "valid delta entry keeps its projected bundle member")

	# Retail summons some heroes through a producer's authored construct
	# command instead of a fortress roster slot (Treebeard is trained by the
	# Ent Moot socket). A hero on a command-socket route validates only with
	# the authored INI provenance; every other hero route stays fail-closed.
	var summoned := _fixture_document()
	summoned["objectId"] = "FixtureSummonedHero"
	summoned["category"] = "hero"
	(summoned["registration"] as Dictionary)["production"] = [{
		"producerObjectId": "UniversalMonsterPen",
		"commandSetId": "UniversalMonsterPenCommandSet",
		"commandId": "Command_SummonFixtureHero",
		"surface": "command-socket",
		"slot": 2,
		"prerequisites": [],
		"commandSetTransition": [],
		"source": {
			"producerIni": "data/ini/object/fixture/universalmonsterpen.ini",
			"commandSetIni": "data/ini/commandset.ini",
			"commandButtonIni": "data/ini/commandbutton.ini",
		},
	}]
	_write_json(pack_root.path_join("data/playable-units/summonedhero.json"), summoned)
	_check(content_db._load_playable_unit_runtimes(pack_root, {
		"playableUnit.summonedhero": "data/playable-units/summonedhero.json",
	}), "command-socket hero with authored evidence loads")
	_check(not content_db.get_playable_unit_runtime("FixtureSummonedHero").is_empty(), "command-socket hero runtime is indexed")

	var unproven := summoned.duplicate(true)
	(((unproven["registration"] as Dictionary)["production"] as Array)[0] as Dictionary).erase("source")
	_write_json(pack_root.path_join("data/playable-units/unprovenhero.json"), unproven)
	var before_unproven: Dictionary = content_db.get_playable_unit_runtimes()
	var unproven_loaded: bool = content_db._load_playable_unit_runtimes(pack_root, {
		"playableUnit.unprovenhero": "data/playable-units/unprovenhero.json",
	})
	_check(unproven_loaded and content_db.get_playable_unit_runtimes() == before_unproven, "command-socket hero without authored evidence is skipped atomically")

	var routeless := summoned.duplicate(true)
	(routeless["registration"] as Dictionary)["production"] = []
	_write_json(pack_root.path_join("data/playable-units/routelesshero.json"), routeless)
	var routeless_loaded: bool = content_db._load_playable_unit_runtimes(pack_root, {
		"playableUnit.routelesshero": "data/playable-units/routelesshero.json",
	})
	_check(routeless_loaded and not content_db.get_playable_unit_runtime("FixtureSummonedHero").is_empty() and content_db.get_playable_unit_runtimes() == before_unproven, "hero without any production route is skipped atomically")

	# Non-hero command-socket routes are unchanged: the monster fixture carries
	# no authored provenance block and still loads into the registry.
	var non_hero_routes: Array = (document.get("registration", {}) as Dictionary).get("production", [])
	var non_hero_has_provenance := false
	for route_value in non_hero_routes:
		non_hero_has_provenance = non_hero_has_provenance or (route_value as Dictionary).has("source")
	_check(not non_hero_has_provenance and not document.is_empty(), "non-hero command-socket routes need no authored provenance")

	# The additive hero-ability contract: well-formed converted rows load, a
	# malformed abilities array is rejected atomically, and documents without
	# the key at all keep loading (older packs stay valid).
	var ability_doc := summoned.duplicate(true)
	ability_doc["objectId"] = "FixtureAbilityHero"
	(ability_doc["registration"] as Dictionary)["abilities"] = [{
		"id": "Command_FixtureHeal",
		"slot": 2,
		"specialPowerId": "SpecialAbilityFixtureHeal",
		"cooldownMs": 70000,
		"targeting": "self",
		"button": {"commandId": "Command_FixtureHeal", "iconIds": ["HSFixtureHeal"], "labelIds": ["CONTROLBAR:FixtureHeal"], "tooltipIds": ["CONTROLBAR:ToolTipFixtureHeal"]},
		"effect": {"kind": "heal", "module": "AutoHealBehavior", "amountKind": "flat", "amount": 500.0, "radius": 150.0, "onlyOthers": false},
		"modules": [{"kind": "AutoHealBehavior", "instanceTag": "ModuleTag_Heal", "sourceIni": "data/ini/object/test.ini", "line": 1}],
		"implementation": {"status": "implemented", "reason": "", "limitations": []},
		"sourceIni": "data/ini/commandbutton.ini",
	}]
	_write_json(pack_root.path_join("data/playable-units/abilityhero.json"), ability_doc)
	_check(content_db._load_playable_unit_runtimes(pack_root, {
		"playableUnit.abilityhero": "data/playable-units/abilityhero.json",
	}), "hero with well-formed converted abilities loads")
	_check(not content_db.get_playable_unit_runtime("FixtureAbilityHero").is_empty(), "ability hero runtime is indexed")
	var ability_rules := Adapter.ability_rules(content_db.get_playable_unit_runtime("FixtureAbilityHero"))
	_check(ability_rules.size() == 1, "adapter projects the registration's ability rows")
	_check(int((ability_rules[0] as Dictionary).get("cooldown_ticks", 0)) == 700, "ability cooldown becomes deterministic ticks")
	_check(bool((ability_rules[0] as Dictionary).get("castable", false)), "implemented ability is castable")
	var flagged_doc := ability_doc.duplicate(true)
	var flagged_row := (((flagged_doc["registration"] as Dictionary)["abilities"] as Array)[0] as Dictionary)
	flagged_row["specialPowerContract"] = {
		"flags": ["NEEDS_OBJECT_FILTER"],
		"objectFilter": ["KINGSFAVOR_OBJECTFILTER"],
		"sourceIni": "data/ini/specialpower.ini",
	}
	var flagged_rules := Adapter.ability_rules(flagged_doc)
	_check(flagged_rules.size() == 1 and ((flagged_rules[0] as Dictionary).get("special_power_contract", {}) as Dictionary).get("flags", []) == ["NEEDS_OBJECT_FILTER"], "adapter preserves the exact Kings Favor flag contract")
	flagged_row["specialPowerContract"] = {"flags": ["INVENTED_FLAG"]}
	_check(Adapter.ability_rules(flagged_doc).is_empty(), "adapter refuses unsupported SpecialPower flags")
	flagged_row["specialPowerContract"] = {"flags": ["LIMIT_DISTANCE"]}
	_check(Adapter.ability_rules(flagged_doc).is_empty(), "adapter refuses LIMIT_DISTANCE without MaxCastRange")
	flagged_row["specialPowerContract"] = {"flags": ["PATHABLE_ONLY"]}
	_check(Adapter.ability_rules(flagged_doc).is_empty(), "adapter refuses PATHABLE_ONLY on a non-point ability")
	flagged_row["targeting"] = "point"
	_check(Adapter.ability_rules(flagged_doc).size() == 1, "adapter preserves PATHABLE_ONLY for exact runtime target validation")
	flagged_row["targeting"] = "self"
	flagged_row["specialPowerContract"] = {
		"flags": ["LIMIT_DISTANCE", "NO_FORBIDDEN_OBJECTS"],
		"maxCastRange": 200,
		"forbiddenObjectFilter": ["NO_SUMMON_NEAR_OBJECT_FILTER"],
		"forbiddenObjectRange": 60,
	}
	_check(Adapter.ability_rules(flagged_doc).size() == 1, "adapter admits the complete Oathbreakers placement gate")

	# These graph kinds are emitted by the typed compiler and have independent
	# executable runtime gates. Keep the ContentDB boundary synchronized: stale
	# effect-kind validation previously dropped eight real RotWK heroes even
	# though their packs and consumers were valid.
	for effect_kind in ["activate-module-graph", "weapon-mode-special-power", "dominate-enemy", "stop-special-power", "unleash-special-power"]:
		var graph_doc := ability_doc.duplicate(true)
		var graph_suffix: String = String(effect_kind).replace("-", "_")
		graph_doc["objectId"] = "FixtureGraphHero_%s" % graph_suffix
		var graph_ability := (((graph_doc["registration"] as Dictionary)["abilities"] as Array)[0] as Dictionary)
		(graph_ability["effect"] as Dictionary)["kind"] = effect_kind
		graph_ability["id"] = "Command_%s" % graph_suffix
		_write_json(pack_root.path_join("data/playable-units/graph_%s.json" % graph_suffix), graph_doc)
		_check(content_db._load_playable_unit_runtimes(pack_root, {
			"playableUnit.graph_%s" % graph_suffix: "data/playable-units/graph_%s.json" % graph_suffix,
		}), "typed executable ability graph '%s' passes ContentDB validation" % effect_kind)
		_check(
			not content_db.get_playable_unit_runtime("FixtureGraphHero_%s" % graph_suffix).is_empty(),
			"typed executable ability graph '%s' is indexed" % effect_kind
		)

	var malformed_abilities := ability_doc.duplicate(true)
	malformed_abilities["objectId"] = "FixtureBrokenAbilityHero"
	(malformed_abilities["registration"] as Dictionary)["abilities"] = [{"id": "Command_Broken"}]
	_write_json(pack_root.path_join("data/playable-units/brokenabilityhero.json"), malformed_abilities)
	var before_broken: Dictionary = content_db.get_playable_unit_runtimes()
	var broken_loaded: bool = content_db._load_playable_unit_runtimes(pack_root, {
		"playableUnit.brokenability": "data/playable-units/brokenabilityhero.json",
	})
	_check(broken_loaded and content_db.get_playable_unit_runtimes() == before_broken, "malformed ability rows are skipped atomically")
	_check(Adapter.ability_rules(summoned).is_empty(), "documents without the abilities key project no abilities")

	# The additive experience contract: a well-formed converted chain projects
	# thresholds/awards/per-level effects, malformed chains project nothing,
	# and documents without the key stay valid (older packs keep loading).
	var experience_doc := _fixture_document()
	(experience_doc["registration"] as Dictionary)["experience"] = {
		"status": "compiled",
		"sourceIni": "data/ini/experiencelevels.ini",
		"maxLevel": 3,
		"targetCount": 6,
		"modifierApplication": "cumulative-per-level",
		"levels": [
			{
				"experienceId": "FixtureLevel1", "rank": 1, "requiredExperience": 1, "experienceAward": 3,
				"experienceAwardOwnGuysDie": 2, "line": 2,
				"selectionDecal": {"textureId": "decal_G_level1", "minRadius": 5, "maxRadius": 20, "opacityMin": 0.25, "opacityMax": 1.0, "maxSelectedUnits": 1, "style": "SHADOW_ALPHA_DECAL"},
				"levelUpPresentation": {"informUpdateModule": true, "emotionType": "CHEER", "showLevelUpTint": true, "levelUpTintColorRgb": [255, 128, 0], "levelUpTintPreColorTimeMs": 100, "levelUpTintSustainColorTimeMs": 200, "levelUpTintPostColorTimeMs": 300},
			},
			{
				"experienceId": "FixtureLevel2", "rank": 2, "requiredExperience": 50, "experienceAward": 4, "line": 9,
				"attributeModifiers": [{
					"id": "FixtureBonusRank2",
					"modifiers": [
						{"kind": "HEALTH", "value": 20, "application": "additive"},
						{"kind": "DAMAGE_ADD", "value": 10, "application": "additive"},
					],
					"sourceIni": "data/ini/attributemodifier.ini",
					"category": "LEVEL",
				}],
				"upgrades": ["Upgrade_ObjectLevel2"],
				"selectionDecalTextureId": "decal_G_level2",
			},
			{
				"experienceId": "FixtureLevel3", "rank": 3, "requiredExperience": 100, "experienceAward": 5, "line": 18,
				"attributeModifiers": [{
					"id": "FixtureBonusRank3",
					"modifiers": [{"kind": "HEALTH", "value": 20, "application": "additive"}],
					"sourceIni": "data/ini/attributemodifier.ini",
					"category": "LEVEL",
					"unsupportedModifiers": ["SPEED"],
				}],
			},
		],
	}
	var experience_rule := Adapter.experience_rule(experience_doc)
	_check(int(experience_rule.get("max_level", 0)) == 3, "adapter projects the experience chain level cap")
	var projected_levels: Array = experience_rule.get("levels", [])
	_check(
		projected_levels.size() == 3
			and int((projected_levels[0] as Dictionary).get("required_experience", 0)) == 1
			and int((projected_levels[1] as Dictionary).get("required_experience", 0)) == 50
			and int((projected_levels[2] as Dictionary).get("required_experience", 0)) == 100,
		"adapter projects authored cumulative thresholds"
	)
	_check(
		projected_levels.size() == 3
			and int((projected_levels[0] as Dictionary).get("experience_award", 0)) == 3
			and int((projected_levels[2] as Dictionary).get("experience_award", 0)) == 5,
		"adapter projects authored per-level kill awards"
	)
	_check(
		projected_levels.size() == 3
			and int((projected_levels[1] as Dictionary).get("health_add", 0)) == 20
			and int((projected_levels[1] as Dictionary).get("damage_add", 0)) == 10
			and (projected_levels[1] as Dictionary).get("upgrades", []) == ["Upgrade_ObjectLevel2"]
			and int((projected_levels[0] as Dictionary).get("health_add", 0)) == 0,
		"adapter folds per-level modifiers and preserves level upgrades"
	)
	_check(
		projected_levels.size() == 3
			and Array((projected_levels[2] as Dictionary).get("unsupported_modifiers", [])).has("SPEED")
			and String((projected_levels[1] as Dictionary).get("selection_decal_texture_id", "")) == "decal_G_level2",
		"adapter records unsupported modifiers and the rank decal leaf"
	)
	_check(
		projected_levels.size() == 3
			and int((projected_levels[0] as Dictionary).get("experience_award_own_guys_die", 0)) == 2
			and ((projected_levels[0] as Dictionary).get("selection_decal", {}) as Dictionary).get("style", "") == "SHADOW_ALPHA_DECAL"
			and ((projected_levels[0] as Dictionary).get("level_up_presentation", {}) as Dictionary).get("emotionType", "") == "CHEER",
		"adapter preserves own-team death XP and selection/level-up presentation contracts"
	)
	var malformed_experience := experience_doc.duplicate(true)
	((malformed_experience["registration"] as Dictionary)["experience"] as Dictionary)["maxLevel"] = 4
	_check(Adapter.experience_rule(malformed_experience).is_empty(), "malformed experience chains project nothing")
	_check(Adapter.experience_rule(_fixture_document()).is_empty(), "documents without the experience key project no chain")

	# Sim XP pipeline: member kills pay the victim's authored award at the
	# victim's current level, thresholds level the attacker, per-level effects
	# fold into member stats, and the snapshot carries the live state.
	var victim_doc := experience_doc.duplicate(true)
	victim_doc["objectId"] = "FixtureVictim"
	var xp_sim = Sim.new()
	xp_sim._apply_gameplay_rules({
		"enable_base_loop": true,
		"playable_unit_runtimes": {"FixtureMonster": experience_doc, "FixtureVictim": victim_doc},
		"producer_kind_by_source_object": {"UniversalMonsterPen": "fixture_monster_pen", "AlternateMonsterPen": "alternate_monster_pen"},
		"unit_rules": {},
		"starting_resources": 5000,
		"source_map_transform_scale": 0.1,
		"spawn_initial_battalions": false,
	})
	_check(xp_sim.configuration_error == "", "experience simulation registration succeeds")
	_check(not xp_sim.experience_rule_for_unit("bfme2.object.fixture-monster").is_empty(), "sim registers the converted experience rule")
	xp_sim._add_battalion(10, 0, Vector2.ZERO, "Attacker", "bfme2.object.fixture-monster", "bfme2.object.fixture-monster", 10)
	xp_sim._add_battalion(110, 1, Vector2.ONE, "Victim", "bfme2.object.fixture-monster", "bfme2.object.fixture-monster", 10)
	var attacker_row: Dictionary = xp_sim.entity(10)
	_check(int(attacker_row.get("level", 0)) == 1 and int(attacker_row.get("experience_xp", -1)) == 0, "spawned units enter at rank 1 with an empty XP pool")
	var base_health := int(attacker_row.get("member_maximum_health", 0))
	var base_damage := int(attacker_row.get("member_damage", 0))
	# 49 XP (49 authored-award kills at award 1... use direct award calls for a
	# deterministic boundary) stays below the rank-2 threshold of 50.
	xp_sim._award_experience(attacker_row, 49)
	_check(int(attacker_row.get("level", 0)) == 1 and int(attacker_row.get("experience_xp", 0)) == 49, "XP below the authored threshold does not level")
	xp_sim._award_experience(attacker_row, 1)
	_check(int(attacker_row.get("level", 0)) == 2, "reaching the authored threshold levels the unit")
	_check(
		int(attacker_row.get("member_maximum_health", 0)) == base_health + 20
			and int(attacker_row.get("member_damage", 0)) == base_damage + 10,
		"rank 2 folds the authored HEALTH/DAMAGE_ADD level effects"
	)
	xp_sim._award_experience(attacker_row, 50)
	_check(int(attacker_row.get("level", 0)) == 3 and int(attacker_row.get("member_maximum_health", 0)) == base_health + 40, "level effects accumulate per earned rank")
	xp_sim._award_experience(attacker_row, 500)
	_check(int(attacker_row.get("level", 0)) == 3, "the authored level cap holds")
	# A member kill pays the victim's authored award into the killer's pool.
	var victim_row: Dictionary = xp_sim.entity(110)
	var xp_before := int(attacker_row.get("experience_xp", 0))
	xp_sim._apply_member_damage(10, -1, 110, 999999, "battalion", 0, 0)
	_check(int(attacker_row.get("experience_xp", 0)) == xp_before + 3, "member kill pays the victim's authored rank-1 award")
	_check(int(victim_row.get("health", 0)) == 0, "the kill still lands")
	var snapshot_row: Dictionary = {}
	for row_value in xp_sim.state_snapshot().get("entities", []):
		if int((row_value as Dictionary).get("id", 0)) == 10:
			snapshot_row = row_value
	_check(
		int(snapshot_row.get("level", 0)) == 3 and snapshot_row.has("experience_xp"),
		"the deterministic snapshot carries XP and level state"
	)
	# A compiler-shaped ExperienceLevelCreate contract applies the exact
	# granted row once through normal playable-unit registration.
	var registered_creation_doc := _fixture_document()
	(registered_creation_doc["registration"] as Dictionary)["experience"] = {
		"status": "compiled",
		"sourceIni": "data/ini/experiencelevels.ini",
		"initialRank": 5,
		"maxLevel": 5,
		"targetCount": 1,
		"modifierApplication": "cumulative-per-level",
		"experienceLevelCreate": {
			"rank": 5,
			"mpOnly": false,
			"module": "ExperienceLevelCreate",
			"sourceIni": "data/ini/object/fixture.ini",
			"line": 10,
		},
		"levels": [{
			"experienceId": "FixtureSummonedLevel5",
			"rank": 5,
			"requiredExperience": 1,
			"experienceAwardStatus": "unauthored",
			"line": 20,
			"attributeModifiers": [{
				"id": "FixtureSummonBonusRank5",
				"modifiers": [
					{"kind": "HEALTH", "value": 80, "application": "additive"},
					{"kind": "DAMAGE_ADD", "value": 40, "application": "additive"},
					{"kind": "DAMAGE_MULT", "value": 1.2, "application": "multiplicative"},
					{"kind": "SPELL_DAMAGE", "value": 1.5, "application": "multiplicative"},
				],
				"sourceIni": "data/ini/attributemodifier.ini",
				"category": "LEVEL",
			}],
			"upgrades": [
				"Upgrade_ObjectLevel1", "Upgrade_ObjectLevel2", "Upgrade_ObjectLevel3",
				"Upgrade_ObjectLevel4", "Upgrade_ObjectLevel5",
			],
		}],
	}
	var registered_creation_sim = Sim.new()
	registered_creation_sim._apply_gameplay_rules({
		"enable_base_loop": true,
		"playable_unit_runtimes": {"FixtureMonster": registered_creation_doc},
		"producer_kind_by_source_object": {"UniversalMonsterPen": "fixture_monster_pen", "AlternateMonsterPen": "alternate_monster_pen"},
		"unit_rules": {},
		"starting_resources": 5000,
		"source_map_transform_scale": 0.1,
		"spawn_initial_battalions": false,
	})
	registered_creation_sim._add_battalion(10, 0, Vector2.ZERO, "Summoned", "bfme2.object.fixture-monster", "bfme2.object.fixture-monster", 10)
	var registered_creation_row: Dictionary = registered_creation_sim.entity(10)
	var registered_upgrades: Dictionary = registered_creation_row.get("applied_upgrades", {}) as Dictionary
	_check(
		int(registered_creation_row.get("level", 0)) == 5
			and int(registered_creation_row.get("member_maximum_health", 0)) == base_health + 80
			and int(registered_creation_row.get("member_damage", 0)) == roundi(float(base_damage + 40) * 1.2)
			and float(registered_creation_row.get("spell_damage_multiplier", 0.0)) == 1.5
			and registered_upgrades.size() == 5
			and registered_upgrades.has("Upgrade_ObjectLevel1")
			and registered_upgrades.has("Upgrade_ObjectLevel5"),
		"registered creation rank applies the exact Dwarven-shaped row once"
	)
	registered_creation_sim._add_battalion(20, 1, Vector2.ONE, "Killer", "bfme2.object.fixture-monster", "bfme2.object.fixture-monster", 10)
	var unknown_award_xp := int((registered_creation_sim.entity(20) as Dictionary).get("experience_xp", 0))
	registered_creation_sim._apply_member_damage(20, -1, 10, 999999, "battalion", 0, 0)
	_check(
		int((registered_creation_sim.entity(20) as Dictionary).get("experience_xp", 0)) == unknown_award_xp
			and registered_creation_sim.experience_unauthored_victims().has("bfme2.object.fixture-monster"),
		"an unauthored creation-rank kill award is recorded and never invented"
	)
	# A victim retail never authored a chain for pays the recorded default and
	# the fallback is recorded, never invented.
	var chainless_sim = Sim.new()
	var creation_rank_rule := {
		"horde_id": "bfme2.object.fixture-victim", "member_count": 1, "member_health": 100,
		"member_damage": 5, "speed": 1.0, "speed_source": 10.0, "acceleration": 1.0,
		"acceleration_source": 10.0, "turn_rate_degrees_per_second": 180.0, "braking": 1.0,
		"braking_source": 10.0, "attack_range": 1.0, "attack_range_source": 10.0,
		"minimum_attack_range": 0.0, "minimum_attack_range_source": 0.0, "vision_range": 4.0,
		"vision_range_source": 40.0, "delay_between_shots_ms": 100.0, "pre_attack_delay_ms": 0.0,
		"firing_duration_ms": 100.0, "attack_period_ticks": 1, "pre_attack_ticks": 0,
		"firing_duration_ticks": 1, "clip_size": 0, "clip_reload_time_ms": 0.0,
		"continuous_fire_one": 0, "continuous_fire_coast_ticks": 0,
		"continuous_fire_rate_multiplier": 1.0, "formation_positions": [Vector3.ZERO],
		"creation_experience_rank": 10,
		"creation_experience_effects": {
			"rank": 10,
			"health_add": 40.0,
			"damage_add": 15.0,
			"production_multiplier": 1.0,
			"upgrades": ["Upgrade_ObjectLevel1", "Upgrade_ObjectLevel10"],
		},
		"provenance": {},
	}
	var ordinary_rank_rule := creation_rank_rule.duplicate(true)
	ordinary_rank_rule["horde_id"] = "bfme2.object.fixture-ordinary"
	ordinary_rank_rule.erase("creation_experience_rank")
	chainless_sim._apply_gameplay_rules({
		"enable_base_loop": true,
		"playable_unit_runtimes": {"FixtureMonster": experience_doc},
		"producer_kind_by_source_object": {"UniversalMonsterPen": "fixture_monster_pen", "AlternateMonsterPen": "alternate_monster_pen"},
		"unit_rules": {
			"bfme2.object.fixture-victim": creation_rank_rule,
			"bfme2.object.fixture-ordinary": ordinary_rank_rule,
		},
		"starting_resources": 5000,
		"source_map_transform_scale": 0.1,
		"spawn_initial_battalions": false,
	})
	_check(chainless_sim.configuration_error == "", "chainless-victim simulation registration succeeds")
	chainless_sim._add_battalion(10, 0, Vector2.ZERO, "Attacker", "bfme2.object.fixture-monster", "bfme2.object.fixture-monster", 10)
	chainless_sim._add_battalion(110, 1, Vector2.ONE, "Victim", "bfme2.object.fixture-victim", "bfme2.object.fixture-victim", 0)
	_check(
		int((chainless_sim.entity(110) as Dictionary).get("level", 0)) == 10
			and int((chainless_sim.entity(110) as Dictionary).get("member_maximum_health", 0)) == 140
			and int((chainless_sim.entity(110) as Dictionary).get("member_damage", 0)) == 20
			and ((chainless_sim.entity(110) as Dictionary).get("applied_upgrades", {}) as Dictionary).has("Upgrade_ObjectLevel1")
			and ((chainless_sim.entity(110) as Dictionary).get("applied_upgrades", {}) as Dictionary).has("Upgrade_ObjectLevel10")
			and not (chainless_sim.entity(110) as Dictionary).has("experience_xp"),
		"creation-rank module applies its authored row once without inventing a missing XP chain"
	)
	chainless_sim._add_battalion(120, 1, Vector2(2.0, 2.0), "Ordinary", "bfme2.object.fixture-ordinary", "bfme2.object.fixture-ordinary", 0)
	_check(
		int((chainless_sim.entity(120) as Dictionary).get("level", 1)) == 1
			and not (chainless_sim.entity(120) as Dictionary).has("level")
			and not (chainless_sim.entity(120) as Dictionary).has("experience_xp"),
		"omitting the creation-rank module preserves ordinary rank-one spawn"
	)
	var chainless_xp := int((chainless_sim.entity(10) as Dictionary).get("experience_xp", 0))
	chainless_sim._apply_member_damage(10, -1, 110, 999999, "battalion", 0, 0)
	_check(
		int((chainless_sim.entity(10) as Dictionary).get("experience_xp", 0)) == chainless_xp
			and chainless_sim.experience_unauthored_victims().has("bfme2.object.fixture-victim"),
		"chainless victims pay the recorded default with the fallback recorded"
	)
	_finish()


func _build_fixture(pack_root: String) -> void:
	DirAccess.make_dir_recursive_absolute(pack_root.path_join("data/playable-units"))
	DirAccess.make_dir_recursive_absolute(pack_root.path_join("assets/ui"))
	DirAccess.make_dir_recursive_absolute(pack_root.path_join("assets/audio"))
	DirAccess.make_dir_recursive_absolute(pack_root.path_join("assets/models"))
	_write_bytes(pack_root.path_join("assets/ui/fixture.png"), PackedByteArray([1, 2, 3]))
	_write_bytes(pack_root.path_join("assets/audio/fixture.wav"), _silent_wav())
	_write_bytes(pack_root.path_join("assets/models/fixture.glb"), PackedByteArray([7, 8, 9]))
	_write_json(pack_root.path_join("data/playable-units/fixturemonster.json"), _fixture_document())


func _fixture_document() -> Dictionary:
	return {
		"schema": "openbfme.playable-unit-runtime",
		"schemaVersion": 0,
		"objectId": "FixtureMonster",
		"category": "monster",
		"descriptorSha256": "1".repeat(64),
		"recipeSha256": "2".repeat(64),
		"resourceIds": ["fixture-model", "fixture-ui", "fixture-audio"],
		"registration": {
			"abilities": [{
				"id": "Command_FixtureDisguise",
				"specialPowerId": "SpecialAbilityFixtureDisguise",
				"slot": 2,
				"targeting": "self",
				"button": {},
				"effect": {"kind": "special-disguise"},
				"implementation": {"status": "implemented"},
			}, {
				"id": "Command_FixtureDeploy",
				"specialPowerId": "SpecialAbilityFixtureDeploy",
				"slot": 3,
				"targeting": "self",
				"button": {},
				"effect": {"kind": "toggle-deploy"},
				"implementation": {"status": "implemented"},
			}],
			"production": [{
				"producerObjectId": "UniversalMonsterPen",
				"commandSetId": "UniversalMonsterPenCommandSet",
				"commandId": "Command_ConstructFixtureMonster",
				"surface": "command-socket",
				"slot": 4,
				"prerequisites": ["Upgrade_MonsterPenLevel2"],
				"commandSetTransition": [],
			}, {
				"producerObjectId": "AlternateMonsterPen",
				"commandSetId": "AlternateMonsterPenCommandSet",
				"commandId": "Command_ConstructFixtureMonsterAlternate",
				"surface": "command-socket",
				"slot": 2,
				"prerequisites": [],
				"commandSetTransition": [],
			}],
			"composition": {
				"containerObjectId": "FixtureMonster",
				"primaryMemberObjectId": "FixtureMonster",
				"members": [{"objectId": "FixtureMonster", "count": 1}],
			},
			"gameplay": {},
			"simulation": {
				"displayName": "Fixture Monster",
				"buildCost": 700,
				"buildTimeSeconds": 45.0,
				"commandPoints": 35,
				"memberCount": 1,
				"memberHealth": 2500,
				"speed": 50.0,
				"visionRange": 400.0,
				"combat": {
					"attackRange": 30.0, "minimumAttackRange": 0.0,
					"delayBetweenShotsMs": 1000.0, "preAttackDelayMs": 250.0,
					"firingDurationMs": 250.0, "damage": 200,
				},
				"movement": {"acceleration": 100.0, "braking": 100.0, "turnRateDegreesPerSecond": 360.0},
				"formation": {"memberCount": 1, "positions": [{"x": 0.0, "y": 0.0}]},
				"autoAcquireEnemiesWhenIdle": {
					"enabled": true,
					"attackBuildings": false,
					"whileStealthed": true,
				},
				"moodAttackCheckRate": {"milliseconds": 500},
			},
			"capabilities": [{"id": "move"}, {"id": "attack"}, {"id": "death"}],
			"visual": {
				"components": [{
					"default": true,
					"output": "assets/models/fixture.glb",
					"ownerObjectId": "FixtureMonster",
					"resourceId": "fixture-model",
					"sourceW3d": "art/w3d/fixture.w3d",
				}],
				"coreAnimations": {
					"idle": [{
						"identifier": "fixture_idle",
						"AnimationBlendTime": 10,
						"authoredProperties": [{
							"key": "AnimationBlendTime",
							"value": 10,
							"provenance": {
								"definingObject": "FixtureMonster",
								"inheritanceDistance": 0,
								"line": 80,
								"virtualPath": "data/ini/object/fixture.ini",
							},
						}],
					}],
					"move": [{"identifier": "fixture_move"}],
					"attack": [{"identifier": "fixture_attack"}],
					"death": [{"identifier": "fixture_death"}],
				},
				"authoredAnimationStates": [{
					"conditions": ["USER_9"],
					"identifier": "Fixture_SKL.fixture_extra",
					"modelSourceW3d": "art/w3d/fixture.w3d",
					"ownerObjectId": "FixtureMonster",
					"runtimeSupport": "packaged-unimplemented",
					"sourceW3d": "art/w3d/fixture_extra.w3d",
				}, {
					"conditions": ["USER_10"],
					"identifier": "fixture_missing",
					"modelSourceW3d": "art/w3d/fixture.w3d",
					"ownerObjectId": "FixtureMonster",
					"runtimeExclusionReason": "zero-byte-retail-w3d-placeholder",
					"runtimeSupport": "excluded-zero-byte-placeholder",
					"sourceW3d": "art/w3d/fixture_missing.w3d",
				}],
				"authoredStateLabels": [{
					"StateName": "STATE_Extra.",
					"conditions": ["USER_9"],
					"linkedAnimations": [{"identifier": "Fixture_SKL.fixture_extra", "conditions": ["USER_9"]}],
					"ownerObjectId": "FixtureMonster",
					"provenance": {
						"definingObject": "FixtureMonster",
						"inheritanceDistance": 0,
						"line": 90,
						"virtualPath": "data/ini/object/fixture.ini",
					},
					"runtimeSupport": "packaged-unimplemented",
				}, {
					"StateName": "STATE_Missing",
					"conditions": ["USER_10"],
					"linkedAnimations": [{"identifier": "fixture_missing", "conditions": ["USER_10"]}],
					"ownerObjectId": "FixtureMonster",
					"provenance": {
						"definingObject": "FixtureMonster",
						"inheritanceDistance": 0,
						"line": 91,
						"virtualPath": "data/ini/object/fixture.ini",
					},
					"runtimeSupport": "packaged-unimplemented",
				}],
			},
			"ui": {
				"portraitImageIds": ["UPFixtureMonster"],
				"commands": [{
					"commandId": "Command_ConstructFixtureMonster",
					"fields": {
						"ButtonImage": ["BIFixtureMonster"],
						"TextLabel": ["CONTROLBAR:FixtureMonster"],
						"DescriptLabel": ["CONTROLBAR:ToolTipFixtureMonster"],
					},
					"audioRoutes": [{"field": "UnitSpecificSound", "id": "FixtureMonsterVoicePurchase"}],
				}, {
					"commandId": "Command_ConstructFixtureMonsterAlternate",
					"fields": {
						"ButtonImage": ["BIFixtureMonsterAlternate"],
						"TextLabel": ["CONTROLBAR:FixtureMonsterAlternate"],
						"DescriptLabel": ["CONTROLBAR:ToolTipFixtureMonsterAlternate"],
					},
					"audioRoutes": [{"field": "UnitSpecificSound", "id": "FixtureMonsterSilentPurchase"}],
				}],
			},
			"imageBindings": {
				"BIFixtureMonster": "assets/ui/fixture.png",
				"BIFixtureMonsterAlternate": "assets/ui/fixture.png",
				"UPFixtureMonster": "assets/ui/fixture.png",
			},
			"audioRoutes": {
				"container": {},
				"primaryMember": {
					"VoiceSelect": [{"id": "FixtureMonsterVoiceSelect"}],
					"VoiceMove": [{"id": "FixtureMonsterVoiceMove"}],
					"VoiceAttack": [{"id": "FixtureMonsterVoiceAttack"}],
					"Sound": [{"id": "FixtureMonsterVoiceDie"}],
					"VoiceCreated": [{"id": "FixtureMonsterVoiceCreated"}],
				},
			},
			"audioBindings": {
				"FixtureMonsterVoiceSelect": ["assets/audio/fixture.wav"],
				"FixtureMonsterVoiceMove": ["assets/audio/fixture.wav"],
				"FixtureMonsterVoiceAttack": ["assets/audio/fixture.wav"],
				"FixtureMonsterVoiceDie": ["assets/audio/fixture.wav"],
				"FixtureMonsterVoiceCreated": ["assets/audio/fixture.wav"],
				"FixtureMonsterVoicePurchase": ["assets/audio/fixture.wav"],
				"FixtureMonsterSilentPurchase": [],
			},
			"audioResolution": {
				"FixtureMonsterVoiceSelect": "samples",
				"FixtureMonsterVoiceMove": "samples",
				"FixtureMonsterVoiceAttack": "samples",
				"FixtureMonsterVoiceDie": "samples",
				"FixtureMonsterVoiceCreated": "samples",
				"FixtureMonsterVoicePurchase": "samples",
				"FixtureMonsterSilentPurchase": "authored-silent",
			},
			"unsupportedCapabilities": [],
		},
	}


func _write_json(path: String, value: Dictionary) -> void:
	var file := FileAccess.open(path, FileAccess.WRITE)
	file.store_string(JSON.stringify(value, "  ") + "\n")
	file.close()


func _write_bytes(path: String, value: PackedByteArray) -> void:
	var file := FileAccess.open(path, FileAccess.WRITE)
	file.store_buffer(value)
	file.close()


func _silent_wav() -> PackedByteArray:
	return PackedByteArray([
		82, 73, 70, 70, 38, 0, 0, 0, 87, 65, 86, 69,
		102, 109, 116, 32, 16, 0, 0, 0, 1, 0, 1, 0,
		64, 31, 0, 0, 128, 62, 0, 0, 2, 0, 16, 0,
		100, 97, 116, 97, 2, 0, 0, 0, 0, 0,
	])


func _check(condition: bool, label: String) -> void:
	if condition:
		passed += 1
	else:
		_fail(label)


func _fail(label: String) -> void:
	failed += 1
	push_error("PLAYABLE_UNIT_RUNTIME_CONSUMER_FAIL %s" % label)


func _finish() -> void:
	if failed == 0:
		print("PLAYABLE_UNIT_RUNTIME_CONSUMER_OK passed=%d failed=0" % passed)
		quit(0)
	else:
		print("PLAYABLE_UNIT_RUNTIME_CONSUMER_RESULT passed=%d failed=%d" % [passed, failed])
		quit(1)
