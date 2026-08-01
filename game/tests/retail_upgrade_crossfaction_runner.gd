extends SceneTree

## Cross-faction structure-upgrade-contract SIM proof. Boots the real private Men
## slice once for the base gameplay rules + map, resolves the Elves supplemental
## manifest through the same driver path the N-team runner uses, then constructs a
## Men(team 0) vs Elves(team 1) match via team_faction_manifests. It asserts the
## slice-side fix from this packet: each team's forge registers ITS OWN structure-
## upgrade contracts (no cross-faction collapse or leak), queue_structure_upgrade
## accepts a team's own forge research and rejects the other faction's upgrade id,
## a Men horde that completes the forge research and purchases forged blades shows
## the compiled weapon-swap damage effect, and the whole cross-faction setup is
## twin-run hash-equal. It also asserts the DEFAULT single-faction path still sees
## the same team-0 contracts the global path produced (the unchanged 3CB9CA98
## signature in retail_slice_runner is the byte-identity gate).
##
## Elf COMBAT hordes still need per-faction weapon-mode profiles to field their own
## forged-blades DAMAGE in the sim (a documented follow-up, same gap the N-team
## runner records); this packet proves the per-team CONTRACT registration + queue
## works cross-faction, which is the core coupling that blocked the mixed roster.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")

const FIGHTER := "bfme2.object.gondor-fighter"
const FIGHTER_HORDE := "bfme2.object.gondor-fighter-horde"
const MEN_FB_TECH := "Upgrade_TechnologyGondorForgedBlades"
const ELF_FB_TECH := "Upgrade_TechnologyElvenForgedBlades"
const MEN_FB_OBJECT := "Upgrade_GondorForgedBlades"
const ELF_FORGE_L2 := "Upgrade_EregionForgeLevel2"

var passed := 0
var failed := 0
var slice = null
var base_rules: Dictionary = {}
var map_config: Dictionary = {}
var men_manifest: Dictionary = {}
var elves_manifest: Dictionary = {}


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_UPGRADE_CROSSFACTION_RUNNER")
	OS.set_environment("OPENBFME_STARTER_ARMY", "1")
	call_deferred("_run")


func _run() -> void:
	root.size = Vector2i(1920, 1080)
	var packed: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	_check("scene_parses", packed != null)
	if packed == null:
		_finish()
		return
	slice = packed.instantiate()
	root.add_child(slice)
	await process_frame
	await process_frame
	_check("slice_ready", bool(slice.ready_ok), String(slice.failure_reason))
	if not bool(slice.ready_ok) or slice.source_map_data == null:
		_finish()
		return

	base_rules = slice.gameplay_rules.duplicate(true)
	map_config = slice.source_map_data.simulation_configuration()
	men_manifest = slice.faction_manifest.duplicate(true)
	slice._classify_faction_units("elves")
	elves_manifest = slice._resolve_faction_manifest("elves")
	if elves_manifest.is_empty() or elves_manifest.has("_error"):
		_check("elves_manifest_resolves", false, "elves manifest did not resolve from the loaded supplementals")
		slice.queue_free()
		_finish()
		return
	_check("elves_manifest_resolves", true)

	_run_default_path_unchanged()
	_run_contract_registration()
	_run_queue_accept_reject()
	_run_horde_damage_effect()
	_run_twin_determinism()

	slice.queue_free()
	_finish()


# ---------------------------------------------------------------------------
# Shared construction
# ---------------------------------------------------------------------------

func _cross_faction_rules() -> Dictionary:
	# Minimal cross-faction rules: the Men base_rules already carry the Men combat
	# unit rules (fighter horde) and the map; team_faction_manifests seeds team 1
	# from the Elves manifest so its forge compiles the ELVEN structure-upgrade
	# contracts. spawn_initial_battalions stays off (this proof injects the units
	# and forges it needs directly; it never depends on elf combat runtimes).
	var rules := base_rules.duplicate(true)
	rules["enable_base_loop"] = true
	rules["spawn_initial_battalions"] = false
	rules["team_faction_manifests"] = {0: men_manifest.duplicate(true), 1: elves_manifest.duplicate(true)}
	return rules


func _make_cross_faction_sim():
	var sim = SimScript.new()
	sim.configure_team_roster([
		{"team": 0, "faction": "men", "is_ai": false},
		{"team": 1, "faction": "elves", "is_ai": false},
	])
	sim.setup(map_config.duplicate(true), _cross_faction_rules())
	sim.ai_enabled = false
	return sim


func _add_forge(sim, structure_id: int, team: int, completed: Array = []) -> void:
	sim.structures[structure_id] = {
		"id": structure_id,
		"team": team,
		"kind": "structure",
		"structure_kind": "forge",
		"name": "Forge %d" % structure_id,
		"position": Vector2(float(team) * 6.0, 0.0),
		"rally": Vector2(float(team) * 6.0, 2.0),
		"health": 2000,
		"maximum_health": 2000,
		"construction_progress": 1.0,
		"level": 1,
		"completed_upgrades": completed.duplicate(),
		"upgrade_queue": [],
		"production": [],
		"queue": [],
		"damage_remainders": {},
		"income_per_payout": 0,
	}


# ---------------------------------------------------------------------------
# DEFAULT single-faction path unchanged
# ---------------------------------------------------------------------------

func _run_default_path_unchanged() -> void:
	var d = SimScript.new()
	d.setup(map_config.duplicate(true), base_rules.duplicate(true))
	# The default roster is all one faction, so every team's contract table is the
	# global table the old global path produced: same keys, same size, and the Men
	# forged-blades research present. The unchanged 3CB9CA98 signature in
	# retail_slice_runner is the byte-identity backstop.
	var team0 := d.structure_upgrade_contracts_for_team(0)
	var team1 := d.structure_upgrade_contracts_for_team(1)
	_check(
		"default_team0_matches_global_contracts",
		team0.size() == d._structure_upgrade_contracts.size()
			and team0.has(MEN_FB_TECH)
			and team0.keys() == d._structure_upgrade_contracts.keys(),
		"team0=%d global=%d" % [team0.size(), d._structure_upgrade_contracts.size()]
	)
	_check(
		"default_teams_share_one_faction_table",
		team1.size() == team0.size() and team1.has(MEN_FB_TECH),
		"team0=%d team1=%d" % [team0.size(), team1.size()]
	)
	_check("default_no_configuration_error", String(d.configuration_error) == "", String(d.configuration_error))


# ---------------------------------------------------------------------------
# Per-team contract registration
# ---------------------------------------------------------------------------

func _run_contract_registration() -> void:
	var sim = _make_cross_faction_sim()
	_check("xf_no_configuration_error", String(sim.configuration_error) == "", String(sim.configuration_error))

	var t0 := sim.structure_upgrade_contracts_for_team(0)
	var t1 := sim.structure_upgrade_contracts_for_team(1)

	# Team 0 (Men) registered ITS OWN forged-blades contract and NOT the Elven one.
	_check(
		"team0_registers_men_forged_blades",
		t0.has(MEN_FB_TECH) and not t0.has(ELF_FB_TECH),
		"men=%s elf=%s" % [t0.has(MEN_FB_TECH), t0.has(ELF_FB_TECH)]
	)
	# Team 1 (Elves) registered ITS OWN forged-blades contract and NOT the Men one.
	_check(
		"team1_registers_elven_forged_blades",
		t1.has(ELF_FB_TECH) and not t1.has(MEN_FB_TECH),
		"elf=%s men=%s" % [t1.has(ELF_FB_TECH), t1.has(MEN_FB_TECH)]
	)
	# The two tables are genuinely distinct (no collapse to a single faction).
	_check(
		"cross_faction_tables_are_distinct",
		t0.keys() != t1.keys(),
		"t0=%d t1=%d ids" % [t0.size(), t1.size()]
	)
	# Both forge contracts bind the "forge" structure kind (importer parity).
	_check(
		"forged_blades_bind_forge_kind",
		String((t0[MEN_FB_TECH] as Dictionary).get("structure_kind", "")) == "forge"
			and String((t1[ELF_FB_TECH] as Dictionary).get("structure_kind", "")) == "forge"
	)


# ---------------------------------------------------------------------------
# queue accepts own upgrade, rejects the other faction's
# ---------------------------------------------------------------------------

func _run_queue_accept_reject() -> void:
	var sim = _make_cross_faction_sim()
	sim.team_resources[0] = 100000
	sim.team_resources[1] = 100000
	# Men forge (team 0) and Elf forge (team 1). The Elven forged-blades research is
	# gated by the Eregion forge L2 upgrade, so mark it complete on the elf forge.
	_add_forge(sim, 5000, 0)
	_add_forge(sim, 5001, 1, [ELF_FORGE_L2])

	# Each team's forge ACCEPTS its own faction's forged-blades research.
	var men_ok: Dictionary = sim.queue_structure_upgrade(0, 5000, MEN_FB_TECH)
	_check("team0_forge_accepts_men_research", bool(men_ok.get("ok", false)), str(men_ok))
	var elf_ok: Dictionary = sim.queue_structure_upgrade(1, 5001, ELF_FB_TECH)
	_check("team1_forge_accepts_elven_research", bool(elf_ok.get("ok", false)), str(elf_ok))

	# Each team's forge REJECTS the other faction's upgrade id (its contract table
	# has no such contract, so it is an unsupported upgrade — not a silent success).
	var men_forge_elf: Dictionary = sim.queue_structure_upgrade(0, 5000, ELF_FB_TECH)
	_check(
		"team0_forge_rejects_elven_upgrade",
		not bool(men_forge_elf.get("ok", true)) and String(men_forge_elf.get("reason", "")) == "unsupported-upgrade",
		str(men_forge_elf)
	)
	var elf_forge_men: Dictionary = sim.queue_structure_upgrade(1, 5001, MEN_FB_TECH)
	_check(
		"team1_forge_rejects_men_upgrade",
		not bool(elf_forge_men.get("ok", true)) and String(elf_forge_men.get("reason", "")) == "unsupported-upgrade",
		str(elf_forge_men)
	)


# ---------------------------------------------------------------------------
# A horde that purchased forged blades shows the compiled damage effect
# ---------------------------------------------------------------------------

func _run_horde_damage_effect() -> void:
	# Team 0 (Men) is the loaded combat faction. Complete its forge research end to
	# end (per-team structure upgrade -> team technology), then a Men fighter horde
	# purchases the forged-blades OBJECT upgrade and equips the compiled weapon-swap.
	var sim = _make_cross_faction_sim()
	sim.team_resources[0] = 100000
	_add_forge(sim, 5000, 0)
	sim._add_battalion(700, 0, Vector2(-2.0, 0.0), "Men-Fighter", FIGHTER, FIGHTER_HORDE)
	_check("men_horde_seeded", sim.entities.has(700), "entities=%d" % sim.entities.size())

	# Tier 1: research the forge technology on team 0's forge (its OWN contract).
	var research: Dictionary = sim.queue_structure_upgrade(0, 5000, MEN_FB_TECH)
	_check("team0_research_queued", bool(research.get("ok", false)), str(research))
	var tech_owned := false
	for _tick in range(600):
		sim.tick()
		if (sim.team_upgrades.get(0, {}) as Dictionary).has(MEN_FB_TECH):
			tech_owned = true
			break
	_check("team0_forge_research_completes", tech_owned, str(sim.team_upgrades.get(0, {})))

	# Tier 2: the horde purchases the authored OBJECT upgrade the tech unlocks and
	# equips the compiled weapon-swap effect (the forged-blades DAMAGE).
	var purchase: Dictionary = sim.queue_battalion_upgrade(0, 700, MEN_FB_OBJECT)
	_check("men_horde_purchase_accepted", bool(purchase.get("ok", false)), str(purchase))
	var equipped := false
	for _tick in range(600):
		sim.tick()
		if (sim.entity(700).get("applied_upgrades", {}) as Dictionary).has(MEN_FB_OBJECT):
			equipped = true
			break
	_check("men_horde_equips_forged_blades", equipped, str(sim.entity(700).get("applied_upgrades", {})))
	# The equipped upgrade is a real compiled weapon effect carrying forged-blades
	# damage, not an empty marker: the horde now shows the damage effect.
	var weapon_upgrades: Dictionary = sim._unit_weapon_upgrades.get(FIGHTER, {})
	var effect: Dictionary = weapon_upgrades.get(MEN_FB_OBJECT, {})
	_check(
		"forged_blades_effect_carries_damage",
		not effect.is_empty() and _effect_damage(effect) > 0.0,
		"effect=%s damage=%s" % [effect.keys(), str(_effect_damage(effect))]
	)


func _effect_damage(effect: Dictionary) -> float:
	# The compiled weapon-swap effect stores its damage under one of a few shapes;
	# accept any positive authored damage so the assertion survives compiler naming.
	var direct: Variant = effect.get("damage")
	if typeof(direct) == TYPE_DICTIONARY:
		return float((direct as Dictionary).get("value", 0.0))
	if typeof(direct) == TYPE_FLOAT or typeof(direct) == TYPE_INT:
		return float(direct)
	for key in ["damage_value", "value"]:
		if effect.has(key):
			return float(effect[key])
	for nugget_value in Array(effect.get("nuggets", [])):
		var nugget := nugget_value as Dictionary
		var dmg: Variant = nugget.get("damage")
		if typeof(dmg) == TYPE_DICTIONARY:
			return float((dmg as Dictionary).get("value", 0.0))
		if typeof(dmg) == TYPE_FLOAT or typeof(dmg) == TYPE_INT:
			return float(dmg)
	return 0.0


# ---------------------------------------------------------------------------
# Twin-run determinism over the cross-faction setup
# ---------------------------------------------------------------------------

func _build_twin():
	var sim = _make_cross_faction_sim()
	sim.team_resources[0] = 100000
	sim.team_resources[1] = 100000
	_add_forge(sim, 5000, 0)
	_add_forge(sim, 5001, 1, [ELF_FORGE_L2])
	sim._add_battalion(700, 0, Vector2(-2.0, 0.0), "Men-Fighter", FIGHTER, FIGHTER_HORDE)
	sim.queue_structure_upgrade(0, 5000, MEN_FB_TECH)
	sim.queue_structure_upgrade(1, 5001, ELF_FB_TECH)
	return sim


func _run_twin_determinism() -> void:
	var twin_a = _build_twin()
	var twin_b = _build_twin()
	twin_a.advance(400)
	twin_b.advance(400)
	_check(
		"cross_faction_twin_run_deterministic",
		twin_a.state_hash() == twin_b.state_hash(),
		"%s != %s" % [twin_a.state_hash(), twin_b.state_hash()]
	)


# ---------------------------------------------------------------------------

func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_XF_UPGRADE PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_XF_UPGRADE FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	print("RETAIL_XF_UPGRADE_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
