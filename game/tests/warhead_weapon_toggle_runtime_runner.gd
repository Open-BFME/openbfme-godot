extends SceneTree
## ability:Command_ToggleHaldirWeapon ability:Command_ToggleTreebeardRockThrow
##
## Two skirmish heroes shipped a weapon-toggle button that did nothing. Their
## toggled weapons author no DamageNugget of their own: HaldirBow and
## RohanEntRockThrow put the damage on the Weapon their ProjectileNugget names
## as WarheadTemplateName, and the converter stopped at the launcher. Treebeard
## also fires a second projectile purely to spawn a shroud revealer, whose
## warhead retail authors as a completely empty Weapon so the engine does not
## assert -- that no-op used to look like an unresolved gap too.
##
## This runner reads the authored chain out of retail weapon.ini/gamedata.ini,
## then drives the real adapter and sim to prove the toggle swaps combat to the
## authored magnitudes and back.

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const INI_ROOT := "res://../workspace/retail-work/editions/rotwk/cache/effective-assets/data/ini/"
## BFME2 1.06 weapon.ini authors the two-component trebuchet warhead this
## runner checks; RotWK's effective weapon.ini authors one. Point
## OPENBFME_BFME2_EXTRACT at an extracted BFME2 data root (run_tests.bat does
## when workspace holds one). Without it the check is printed as SKIP, never
## counted as a pass.
const BFME2_EXTRACT_ENV := "OPENBFME_BFME2_EXTRACT"
const SOURCE_SCALE := 0.1
const WATCHDOG_FRAMES := 1800

var passed := 0
var failed := 0
var _frames := 0
var _reported := false


func _initialize() -> void:
	call_deferred("_run")


func _process(_delta: float) -> bool:
	_frames += 1
	if _frames > WATCHDOG_FRAMES and not _reported:
		failed += 1
		push_error("WARHEAD_WEAPON_TOGGLE_FAIL watchdog: the runner aborted before reporting")
		_report()
	return false


func _run() -> void:
	var weapons := _read(INI_ROOT + "weapon.ini")
	var gamedata := _read(INI_ROOT + "gamedata.ini")
	if weapons.is_empty() or gamedata.is_empty():
		failed += 1
		push_error("WARHEAD_WEAPON_TOGGLE_FAIL oracle ini files are unreadable")
		_report()
		return

	# --- Haldir: one ProjectileNugget, one warhead ---------------------------
	var bow := _block(weapons, "Weapon HaldirBow")
	_check("oracle_reads_haldir_bow", not bow.is_empty())
	_check("oracle_bow_authors_no_damage_nugget_of_its_own", not bow.contains("DamageNugget"))
	var bow_warheads := _warheads(bow)
	_check("oracle_bow_names_one_warhead", bow_warheads == ["HaldirBowWarhead"])
	var bow_warhead := _block(weapons, "Weapon HaldirBowWarhead")
	_check("oracle_bow_warhead_carries_the_damage", _field(bow_warhead, "Damage") == "HALDIR_BOW_DAMAGE")
	var bow_damage := _define(gamedata, "HALDIR_BOW_DAMAGE")
	var bow_range := _define(gamedata, "HALDIR_BOW_RANGE")
	_check("oracle_resolves_haldir_bow_damage", bow_damage > 0.0)
	_check("oracle_resolves_haldir_bow_range", bow_range == 400.0)
	var sword := _block(weapons, "Weapon HaldirSword")
	var sword_damage := _define(gamedata, "HALDIR_SWORD_DAMAGE")
	_check("oracle_reads_the_default_sword", _field(sword, "Damage") == "HALDIR_SWORD_DAMAGE" and sword_damage > 0.0)
	_check("bow_and_sword_are_distinguishable", not is_equal_approx(bow_damage, sword_damage))

	# --- Treebeard: two ProjectileNuggets, one of them an authored no-op -----
	var rock := _block(weapons, "Weapon RohanEntRockThrow")
	_check("oracle_reads_treebeard_rock_throw", not rock.is_empty())
	_check("oracle_rock_authors_no_damage_nugget_of_its_own", not rock.contains("DamageNugget"))
	var rock_warheads := _warheads(rock)
	_check("oracle_rock_names_two_warheads", rock_warheads.size() == 2)
	_check("oracle_rock_names_the_dummy_revealer_warhead", rock_warheads.has("ShipRevealBombardWarhead_Dummy"))
	var dummy := _block(weapons, "Weapon ShipRevealBombardWarhead_Dummy")
	_check("oracle_dummy_warhead_is_authored_empty", dummy.strip_edges().is_empty())
	var rock_warhead := _block(weapons, "Weapon RohanTreeBeardRockFromThinAirWarhead")
	var rock_nuggets := _damage_expressions(rock_warhead)
	_check("oracle_rock_warhead_authors_two_damage_nuggets", rock_nuggets.size() == 2)
	var rock_unit_damage := _define(gamedata, "ROHAN_TREEBEARD_ROCK_DAMAGE")
	_check("oracle_both_nuggets_name_the_same_constant", rock_nuggets == ["ROHAN_TREEBEARD_ROCK_DAMAGE", "ROHAN_TREEBEARD_ROCK_DAMAGE"] and rock_unit_damage > 0.0)
	var rock_damage := rock_unit_damage * 2.0
	var rock_range := _define(gamedata, "ROHAN_TREEBEARD_ROCK_RANGE")
	_check("oracle_resolves_rock_range", rock_range > 0.0)

	# --- Gondor trebuchet: the compiler-facing warhead component contract -----
	var bfme2_root := OS.get_environment(BFME2_EXTRACT_ENV)
	if bfme2_root == "":
		print("WARHEAD_WEAPON_TOGGLE SKIP compiled_trebuchet_warhead_keeps_two_radius_and_taper_components reason=%s unset" % BFME2_EXTRACT_ENV)
	else:
		var trebuchet_warhead := _block(_read(bfme2_root.path_join("data/ini/weapon.ini")), "Weapon GondorTrebuchetRockWarhead")
		var trebuchet_radii := _numeric_fields(trebuchet_warhead, "Radius")
		var trebuchet_tapers := _numeric_fields(trebuchet_warhead, "DamageTaperOff")
		_check(
			"compiled_trebuchet_warhead_keeps_two_radius_and_taper_components radii=%s tapers=%s" % [str(trebuchet_radii), str(trebuchet_tapers)],
			trebuchet_radii == [20.0, 100.0] and trebuchet_tapers == [50.0]
		)

	# --- adapter: the compiled mode carries the warhead damage ---------------
	var haldir := Adapter.normalized_unit_rule(
		_simulation("bfme2.object.elven-haldir", "HaldirSword", sword_damage, 20.0, "HaldirBow", bow_damage, bow_range),
		SOURCE_SCALE
	)
	if haldir.is_empty():
		failed += 1
		push_error("WARHEAD_WEAPON_TOGGLE_FAIL the adapter refused Haldir's toggled mode")
		_report()
		return
	var haldir_mode: Dictionary = (haldir.get("weapon_modes", {}) as Dictionary).get("weaponset_toggle_1", {}) as Dictionary
	_check("adapter_publishes_haldir_bow_mode", String(haldir_mode.get("name", "")) == "HaldirBow")
	_check("adapter_mode_damage_is_the_warhead_damage", int(haldir_mode.get("member_damage", 0)) == int(bow_damage))
	_check("adapter_mode_range_is_the_authored_bow_range", is_equal_approx(float(haldir_mode.get("attack_range_source", 0.0)), bow_range))
	_check("adapter_mode_range_is_map_scaled", is_equal_approx(float(haldir_mode.get("attack_range", 0.0)), bow_range * SOURCE_SCALE))

	var treebeard := Adapter.normalized_unit_rule(
		_simulation("bfme2.object.rohan-treebeard", "RohanTreeBeardPunch", 300.0, 20.0, "RohanEntRockThrow", rock_damage, rock_range),
		SOURCE_SCALE
	)
	var rock_mode: Dictionary = (treebeard.get("weapon_modes", {}) as Dictionary).get("weaponset_toggle_1", {}) as Dictionary
	_check("adapter_publishes_treebeard_rock_mode", String(rock_mode.get("name", "")) == "RohanEntRockThrow")
	_check("adapter_rock_damage_sums_both_authored_nuggets", int(rock_mode.get("member_damage", 0)) == int(rock_damage))
	_check("adapter_rock_range_is_authored", is_equal_approx(float(rock_mode.get("attack_range_source", 0.0)), rock_range))

	# --- sim: the toggle actually swaps combat ------------------------------
	var rule := _ability_rule("Command_ToggleHaldirWeapon")
	var sim: RetailSliceSim = _fixture(haldir, rule)
	var hero := sim.entities[1] as Dictionary
	_check("hero_starts_on_the_default_sword", int(hero.get("member_damage", 0)) == int(sword_damage))

	var engage: Dictionary = sim.cast_ability(1, "Command_ToggleHaldirWeapon", Vector2.ZERO, 0)
	_check("toggle_engages", bool(engage.get("ok", false)) and bool(engage.get("engaged", false)))
	_check("engaged_mode_is_the_compiled_toggle", String(engage.get("mode", "")) == "weaponset_toggle_1")
	_check("hero_damage_swaps_to_the_warhead_damage", int(hero.get("member_damage", 0)) == int(bow_damage))
	_check("hero_range_swaps_to_the_bow_range", is_equal_approx(float(hero.get("attack_range_source", 0.0)), bow_range))
	_check("engaged_toggle_pins_the_mode_at_any_distance", sim._weapon_mode_for_distance(hero, 0.5) == "weaponset_toggle_1")

	var release: Dictionary = sim.cast_ability(1, "Command_ToggleHaldirWeapon", Vector2.ZERO, 0)
	_check("toggle_releases", bool(release.get("ok", false)) and not bool(release.get("engaged", true)))
	_check("hero_damage_returns_to_the_sword", int(hero.get("member_damage", 0)) == int(sword_damage))

	var ent_sim: RetailSliceSim = _fixture(treebeard, _ability_rule("Command_ToggleTreebeardRockThrow"))
	var ent := ent_sim.entities[1] as Dictionary
	ent_sim.cast_ability(1, "Command_ToggleTreebeardRockThrow", Vector2.ZERO, 0)
	_check("treebeard_swaps_to_the_summed_rock_damage", int(ent.get("member_damage", 0)) == int(rock_damage))
	_check("treebeard_swaps_to_the_authored_rock_range", is_equal_approx(float(ent.get("attack_range_source", 0.0)), rock_range))

	# Fail-closed: a unit with no compiled mode refuses rather than half-swaps.
	var bare: RetailSliceSim = _fixture(
		Adapter.normalized_unit_rule(
			_simulation("bfme2.object.bare", "HaldirSword", sword_damage, 20.0, "", 0.0, 0.0),
			SOURCE_SCALE
		),
		rule
	)
	var refused: Dictionary = bare.cast_ability(1, "Command_ToggleHaldirWeapon", Vector2.ZERO, 0)
	_check("missing_mode_fails_closed", not bool(refused.get("ok", true)) and String(refused.get("reason", "")).begins_with("toggle-mode-unavailable"))
	_report()


# ---------------------------------------------------------------------------


func _simulation(
	unit_type: String,
	default_weapon: String,
	default_damage: float,
	default_range: float,
	toggle_weapon: String,
	toggle_damage: float,
	toggle_range: float
) -> Dictionary:
	var row := {
		"unit_type": unit_type,
		"source_object_id": unit_type,
		"category": "hero",
		"member_count": 1,
		"member_health": 4000,
		"speed_source": 50.0,
		"vision_range_source": 200.0,
		"movement": {
			"acceleration": 20.0,
			"braking": 20.0,
			"turnRateDegreesPerSecond": 180.0,
		},
		"combat": {
			"attackRange": default_range,
			"delayBetweenShotsMs": 1000.0,
			"preAttackDelayMs": 0.0,
			"firingDurationMs": 0.0,
			"damage": int(default_damage),
			"weaponId": default_weapon,
			"weaponSlot": "PRIMARY",
		},
		"formation": {"positions": [{"x": 0.0, "y": 0.0}]},
	}
	if toggle_weapon != "":
		row["weapon_modes_source"] = {
			"weaponset_toggle_1": {
				"weaponId": toggle_weapon,
				"weaponSlot": "PRIMARY",
				"attackRange": toggle_range,
				"delayBetweenShotsMs": 2000.0,
				"preAttackDelayMs": 800.0,
				"firingDurationMs": 500.0,
				"damage": int(toggle_damage),
			},
		}
	return row


func _ability_rule(ability_id: String) -> Dictionary:
	var rules := Adapter.ability_rules({
		"registration": {
			"abilities": [{
				"id": ability_id,
				"slot": 2,
				"targeting": "self",
				"specialPowerId": "",
				"cooldownMs": 0,
				"button": {
					"commandId": ability_id,
					"iconIds": ["UCElven_Bow"],
					"labelIds": ["CONTROLBAR:ToggleElvenWarriorWeapons"],
					"options": ["TOGGLE_IMAGE_ON_WEAPONSET"],
				},
				"effect": {
					"kind": "weapon-toggle",
					"toggleMode": "weaponset_toggle_1",
					"toggledWeaponId": "HaldirBow",
					"sourceIni": "data/ini/commandbutton.ini",
				},
				"implementation": {"status": "implemented", "reason": "", "limitations": []},
				"levelGate": {"requiredLevel": 1, "upgradeIds": []},
			}],
			"stringBindings": {},
		},
	})
	return rules[0] if rules.size() == 1 else {}


func _fixture(unit_rule: Dictionary, rule: Dictionary) -> RetailSliceSim:
	var unit_rules := {}
	for object_id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]:
		unit_rules[object_id] = unit_rule
	var sim: RetailSliceSim = Sim.new()
	sim.setup({}, {"unit_rules": unit_rules, "source_map_transform_scale": SOURCE_SCALE})
	sim.ai_enabled = false
	sim.base_loop_enabled = false
	sim.entities.clear()
	sim.structures.clear()
	sim._unit_ability_rules[Sim.SOLDIER_HORDE_ID] = sim._scaled_ability_rules([rule], SOURCE_SCALE)
	sim._add_battalion(1, 0, Vector2.ZERO, "Fixture", Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, 0, unit_rule)
	sim._attach_hero_ability_state(sim.entities[1] as Dictionary)
	return sim


func _read(path: String) -> String:
	return FileAccess.get_file_as_string(ProjectSettings.globalize_path(path))


func _block(text: String, header: String) -> String:
	## Body of a top-level `Weapon <id>` block; nested nugget Ends are indented,
	## the block's own End is not.
	var collecting := false
	var body := ""
	for line in text.split("\n"):
		var raw := String(line).replace("\r", "")
		var trimmed := raw.strip_edges()
		if not collecting:
			if _normalized(_strip_comment(trimmed)) == _normalized(header):
				collecting = true
				body = ""
			continue
		if raw.strip_edges(false, true) == "End":
			return body
		body += trimmed + "\n"
	return body


func _warheads(block: String) -> Array:
	var output: Array = []
	for line in block.split("\n"):
		var trimmed := _normalized(_strip_comment(String(line)))
		if trimmed.begins_with("WarheadTemplateName ="):
			output.append(trimmed.substr("WarheadTemplateName =".length()).strip_edges())
	return output


func _damage_expressions(block: String) -> Array:
	var output: Array = []
	for line in block.split("\n"):
		var trimmed := _normalized(_strip_comment(String(line)))
		if trimmed.begins_with("Damage ="):
			output.append(trimmed.substr("Damage =".length()).strip_edges())
	return output


func _numeric_fields(block: String, wanted_key: String) -> Array:
	## The importer pytest owns the compiled component assertion. This runner
	## independently binds those values to the real retail INI bytes; an absent
	## first DamageTaperOff is the SAGE default zero asserted by the pytest.
	var output: Array = []
	for line in block.split("\n"):
		var trimmed := _normalized(_strip_comment(String(line)))
		if trimmed.begins_with(wanted_key + " ="):
			output.append(trimmed.substr((wanted_key + " =").length()).strip_edges().to_float())
	return output


func _define(text: String, name: String) -> float:
	for line in text.split("\n"):
		var trimmed := _normalized(_strip_comment(String(line)))
		if not trimmed.begins_with("#define " + name + " "):
			continue
		return trimmed.substr(("#define " + name + " ").length()).strip_edges().to_float()
	return -1.0


func _field(block: String, key: String) -> String:
	for line in block.split("\n"):
		var trimmed := _normalized(String(line))
		if trimmed.begins_with(key + " ="):
			return _strip_comment(trimmed.substr((key + " =").length()).strip_edges())
	return ""


func _strip_comment(value: String) -> String:
	var text := value
	for marker in [";", "//"]:
		var index := text.find(marker)
		if index >= 0:
			text = text.substr(0, index)
	return text.strip_edges()


func _normalized(value: String) -> String:
	var text := value.replace("\t", " ")
	while text.contains("  "):
		text = text.replace("  ", " ")
	return text.strip_edges()


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("WARHEAD_WEAPON_TOGGLE_FAIL %s" % label)


func _report() -> void:
	if _reported:
		return
	_reported = true
	var ran := passed + failed
	if ran < 28:
		failed += 1
		push_error("WARHEAD_WEAPON_TOGGLE_FAIL liveness ran=%d expected>=28" % ran)
	print("WARHEAD_WEAPON_TOGGLE_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
