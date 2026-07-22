extends SceneTree
## Spellbook gate: the Men palantir tree, purchase validation, cooldowns,
## doc-evidenced casts, and the powers orb all derive from the selected pack's
## openbfme.spellbook-runtime document (menspellbook.json).

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")

var passed := 0
var failed := 0

var HudScript: Script

const EXPECTED_POWER_ORDER := [
	"SpellBookHeal", "SpellBookRallyingCall", "SpellBookElvenWoodMP",
	"SpellBookSpawnLoneTower", "SpellBookArrowVolleyGood", "SpellBookTomBombadil",
	"SpellBookHobbitAllies", "SpellBookRohanAllies", "SpellBookCloudBreak",
	"SpellBookDunedainAllies", "SpellBookArmyoftheDead", "SpellBookEarthquake",
]
const EXPECTED_COSTS := [5, 5, 5, 10, 10, 10, 10, 15, 15, 15, 25, 25]


func _initialize() -> void:
	OS.set_environment("OPENBFME_STARTER_ARMY", "1")
	call_deferred("_run")


func _run() -> void:
	root.size = Vector2i(1920, 1080)
	var content_db = root.get_node_or_null("ContentDB")
	_check("content_db_available", content_db != null)
	if content_db == null:
		_finish()
		return
	var doc := _men_spellbook_document(content_db)
	_check("men_spellbook_document_loaded", String((doc.get("target", {}) as Dictionary).get("faction", "")) == "Men", String((doc.get("target", {}) as Dictionary).get("faction", "")))
	if doc.is_empty():
		_finish()
		return
	_sim_tree_checks(doc)
	_sim_purchase_checks(doc)
	_sim_reset_accept_checks(doc)
	_sim_cast_checks(doc)
	_sim_effect_casts(doc)
	_sim_economy_checks(doc)
	_sim_pause_checks(doc)
	_sim_ui_state_checks(doc)
	_per_faction_checks(content_db)
	_hud_integration_checks(content_db, doc)
	_audio_routing_checks(content_db, doc)
	_finish()


## --- A. The tree drives from the doc ---

func _sim_tree_checks(doc: Dictionary) -> void:
	var sim = SimScript.new()
	_check("spellbook_configures_from_doc", sim.configure_spellbook_runtime(doc) and sim.spellbook_error() == "", sim.spellbook_error())
	_check("tree_has_twelve_powers_in_purchase_slot_order", sim.spellbook_power_ids() == EXPECTED_POWER_ORDER, str(sim.spellbook_power_ids()))
	var costs_ok := true
	var slots_ok := true
	for index in EXPECTED_POWER_ORDER.size():
		var row: Dictionary = sim.spellbook_power(EXPECTED_POWER_ORDER[index])
		if int(row.get("cost", -1)) != EXPECTED_COSTS[index] or int(row.get("purchase_slot", -1)) != index + 1:
			costs_ok = false
		if int(row.get("purchase_slot", -1)) != index + 1:
			slots_ok = false
	_check("costs_match_doc_point_cost_mp", costs_ok)
	_check("purchase_slots_are_authored_sequence", slots_ok)
	var heal: Dictionary = sim.spellbook_power("SpellBookHeal")
	var rally: Dictionary = sim.spellbook_power("SpellBookRallyingCall")
	var earthquake: Dictionary = sim.spellbook_power("SpellBookEarthquake")
	_check(
		"reload_times_from_doc",
		int(heal.get("reload_ticks", 0)) == 1800 and int(rally.get("reload_ticks", 0)) == 1200 and int(earthquake.get("reload_ticks", 0)) == 7200,
		"heal=%d rally=%d quake=%d" % [int(heal.get("reload_ticks", -1)), int(rally.get("reload_ticks", -1)), int(earthquake.get("reload_ticks", -1))]
	)
	_check("intrinsic_faction_science_owned", sim.owned_sciences(0) == ["SCIENCE_MEN"], str(sim.owned_sciences(0)))
	_check("heal_and_rally_castable_from_converted_leaves", bool(heal.get("castable", false)) and bool(rally.get("castable", false)))
	var locked_count := 0
	var reasons_ok := true
	for power_id in EXPECTED_POWER_ORDER:
		var row: Dictionary = sim.spellbook_power(power_id)
		if bool(row.get("castable", false)):
			continue
		locked_count += 1
		if String(row.get("locked_reason", "")) == "":
			reasons_ok = false
	_check("all_powers_castable_from_converted_leaves", locked_count == 0, "locked=%d" % locked_count)
	# Fail closed when a needed leaf is genuinely absent: strip the volley
	# weapon from the doc and the power re-locks with the gap named.
	var damaged: Dictionary = doc.duplicate(true)
	var damaged_weapons: Array = ((damaged.get("registration", {}) as Dictionary).get("leaves", {}) as Dictionary).get("weapons", []) as Array
	for index in range(damaged_weapons.size() - 1, -1, -1):
		if String((damaged_weapons[index] as Dictionary).get("id", "")) == "ArrowVolleyOneWeapon":
			damaged_weapons.remove_at(index)
	var damaged_sim = SimScript.new()
	damaged_sim.configure_spellbook_runtime(damaged)
	var damaged_row: Dictionary = damaged_sim.spellbook_power("SpellBookArrowVolleyGood")
	_check("missing_weapon_leaf_relocks_with_gap", not bool(damaged_row.get("castable", true)) and String(damaged_row.get("locked_reason", "")).contains("ArrowVolleyOneWeapon"), String(damaged_row.get("locked_reason", "")))
	damaged_sim.setup({}, _rules())
	damaged_sim.team_power_points[0] = 30
	damaged_sim.purchase_power(0, "SpellBookHeal")
	damaged_sim.purchase_power(0, "SpellBookArrowVolleyGood")
	_check("relocked_power_cast_rejected", String(damaged_sim.cast_power(0, "SpellBookArrowVolleyGood", Vector2.ZERO).get("reason", "")) == "effect-unsupported")
	# Malformed documents fail closed.
	var broken = SimScript.new()
	var configured: bool = broken.configure_spellbook_runtime({"schema": "not-a-spellbook"})
	_check("malformed_doc_fails_closed", not configured and broken.spellbook_error() != "" and not broken.spellbook_available())
	broken.setup({}, _rules())
	_check("malformed_doc_blocks_purchase_and_cast", String(broken.purchase_power(0, "SpellBookHeal").get("reason", "")) == "spellbook-unavailable" and String(broken.cast_power(0, "SpellBookHeal", Vector2.ZERO).get("reason", "")) == "spellbook-unavailable")


## --- B. Purchase validation ---

func _sim_purchase_checks(doc: Dictionary) -> void:
	var sim = SimScript.new()
	sim.configure_spellbook_runtime(doc)
	sim.setup({}, _rules())
	_check("starting_points_from_setup", sim.power_points(0) == 1, str(sim.power_points(0)))
	_check("insufficient_points_rejected", String(sim.purchase_power(0, "SpellBookHeal").get("reason", "")) == "insufficient-power-points")
	sim.team_power_points[0] = 5
	_check("prerequisite_gate_blocks_tier_two", String(sim.purchase_power(0, "SpellBookSpawnLoneTower").get("reason", "")) == "prerequisites-unmet")
	_check("unknown_power_rejected", String(sim.purchase_power(0, "SpellBookNope").get("reason", "")) == "unknown-power")
	var mismatch: Dictionary = sim.purchase_power(0, "SpellBookHeal", 7)
	_check("caller_cost_mismatch_rejected", String(mismatch.get("reason", "")) == "cost-mismatch" and sim.power_points(0) == 5 and not sim.has_power(0, "SpellBookHeal"))
	var bought: Dictionary = sim.purchase_power(0, "SpellBookHeal", 5)
	_check(
		"heal_purchases_at_doc_cost",
		bool(bought.get("ok", false)) and int(bought.get("cost", -1)) == 5 and sim.power_points(0) == 0 and sim.has_power(0, "SpellBookHeal") and sim.owned_sciences(0).has("SCIENCE_Heal"),
		str(bought)
	)
	_check("purchase_event_carries_doc_evidence", _last_power_event(sim, "power.purchased").get("science_id", "") == "SCIENCE_Heal" and int(_last_power_event(sim, "power.purchased").get("purchase_slot", -1)) == 1, str(_last_power_event(sim, "power.purchased")))
	_check("already_purchased_rejected", String(sim.purchase_power(0, "SpellBookHeal").get("reason", "")) == "already-purchased")
	sim.team_power_points[0] = 20
	_check("tier_two_unlocked_by_prereq", bool(sim.can_purchase_power(0, "SpellBookSpawnLoneTower").get("ok", false)))
	_check("or_group_satisfied_by_rally_alternative", bool(sim.purchase_power(0, "SpellBookRallyingCall").get("ok", false)) and bool(sim.can_purchase_power(0, "SpellBookArrowVolleyGood").get("ok", false)))
	_check("or_group_tom_bombadil_via_rally", bool(sim.can_purchase_power(0, "SpellBookTomBombadil").get("ok", false)))
	_check("cloud_break_needs_deeper_tier", not bool(sim.can_purchase_power(0, "SpellBookCloudBreak").get("ok", false)))
	sim.purchase_power(0, "SpellBookArrowVolleyGood")
	sim.team_power_points[0] = 30
	_check("cloud_break_unlocked_by_arrow_volley_group", bool(sim.can_purchase_power(0, "SpellBookCloudBreak").get("ok", false)))
	_check("tier_four_still_gated", not bool(sim.can_purchase_power(0, "SpellBookEarthquake").get("ok", false)))


## --- C. RESET / ACCEPT ---

func _sim_reset_accept_checks(doc: Dictionary) -> void:
	var sim = SimScript.new()
	sim.configure_spellbook_runtime(doc)
	sim.setup({}, _rules())
	sim.team_power_points[0] = 15
	sim.purchase_power(0, "SpellBookHeal")
	sim.purchase_power(0, "SpellBookRallyingCall")
	var reset: Dictionary = sim.reset_spellbook_purchases(0)
	_check(
		"reset_refunds_unspent_picks",
		bool(reset.get("ok", false)) and int(reset.get("refunded", -1)) == 10 and sim.power_points(0) == 15 and not sim.has_power(0, "SpellBookHeal") and not sim.owned_sciences(0).has("SCIENCE_Heal"),
		str(reset)
	)
	_check("reset_allows_repurchase", bool(sim.purchase_power(0, "SpellBookHeal").get("ok", false)))
	sim.accept_spellbook_purchases(0)
	var post_accept: Dictionary = sim.reset_spellbook_purchases(0)
	_check("accept_commits_picks", int(post_accept.get("refunded", -1)) == 0 and sim.has_power(0, "SpellBookHeal"), str(post_accept))


## --- D. Casts (doc-driven effects + cooldowns) ---

func _sim_cast_checks(doc: Dictionary) -> void:
	var sim = SimScript.new()
	sim.configure_spellbook_runtime(doc)
	sim.setup({}, _rules())
	var battalion: Dictionary = sim.entity(1)
	_check("fixture_battalion_spawned", not battalion.is_empty() and int(battalion.get("member_count", 0)) > 0)
	var maximum_member := int(battalion.get("member_maximum_health", 1))
	var wounded: Array = battalion.get("member_health", [])
	for index in wounded.size():
		wounded[index] = maximum_member / 4
	battalion["member_health"] = wounded
	battalion["health"] = _sum_ints(wounded)
	_check("unowned_power_cast_rejected", String(sim.cast_power(0, "SpellBookHeal", Vector2(battalion["position"])).get("reason", "")) == "power-not-purchased")
	sim.team_power_points[0] = 30
	sim.purchase_power(0, "SpellBookHeal")
	var before: Array = battalion.get("member_health", []).duplicate()
	var cast: Dictionary = sim.cast_power(0, "SpellBookHeal", Vector2(battalion["position"]))
	var expected_heal := maxi(1, roundi(float(maximum_member) * 0.5))
	var healed_ok := true
	var after: Array = battalion.get("member_health", [])
	for index in after.size():
		var expected: int = mini(maximum_member, int(before[index]) + expected_heal)
		if int(after[index]) != expected:
			healed_ok = false
	_check("heal_restores_doc_fraction_per_member", bool(cast.get("ok", false)) and int(cast.get("battalions", 0)) == 1 and healed_ok and int(battalion.get("health", 0)) == _sum_ints(after), str(cast))
	var cast_event := _last_power_event(sim, "power.cast")
	_check(
		"heal_cast_event_carries_doc_bindings",
		String(cast_event.get("sound_id", "")) == "SpellHeal" and String(cast_event.get("effect_kind", "")) == "heal" and float(cast_event.get("radius_source", 0.0)) == 200.0 and Array(cast_event.get("fx_lists", [])).has("FX_SpellHealUnitHealBuff") and Array(cast_event.get("ocls", [])).has("OCL_HealSpellHordeReplenishPing"),
		str(cast_event)
	)
	var cooldown := sim.power_cooldown_state(0, "SpellBookHeal")
	_check("cooldown_started_from_doc_reload", int(cooldown.get("remaining_ticks", 0)) == 1800 and int(cooldown.get("total_ticks", 0)) == 1800, str(cooldown))
	_check("cast_on_cooldown_rejected", String(sim.cast_power(0, "SpellBookHeal", Vector2(battalion["position"])).get("reason", "")) == "power-recharging")
	sim.tick_index += 1800
	for index in wounded.size():
		wounded[index] = 1
	battalion["member_health"] = wounded
	battalion["health"] = _sum_ints(wounded)
	_check("cast_after_cooldown_ok", bool(sim.cast_power(0, "SpellBookHeal", Vector2(battalion["position"])).get("ok", false)))
	# Heal with nobody wounded: no cooldown, no event.
	sim.tick_index += 1800
	for index in wounded.size():
		wounded[index] = maximum_member
	battalion["member_health"] = wounded
	battalion["health"] = _sum_ints(wounded)
	var events_before: int = sim.events.size()
	var dry: Dictionary = sim.cast_power(0, "SpellBookHeal", Vector2(battalion["position"]))
	_check("heal_without_wounded_fails_without_cooldown", String(dry.get("reason", "")) == "no-wounded-allies-in-range" and int(sim.power_cooldown_state(0, "SpellBookHeal").get("remaining_ticks", 0)) == 0 and sim.events.size() == events_before)
	# Cast validation still rejects powers the tree does not grant.
	var unowned_cast: Dictionary = sim.cast_power(0, "SpellBookEarthquake", Vector2(battalion["position"]))
	_check("unowned_tier_four_cast_rejected", String(unowned_cast.get("reason", "")) == "power-not-purchased", str(unowned_cast))
	# Rallying Call: doc modifier (DAMAGE_MULT 150%, 60s, range 100 source).
	sim.purchase_power(0, "SpellBookRallyingCall")
	var second: Dictionary = sim.entity(2)
	var rally_tick: int = sim.tick_index
	var rally: Dictionary = sim.cast_power(0, "SpellBookRallyingCall", Vector2(battalion["position"]))
	var rallied_battalion := int(battalion.get("rally_until_tick", -1)) == rally_tick + 600 and is_equal_approx(float(battalion.get("rally_damage_mult", 0.0)), 1.5)
	var second_unaffected := int(second.get("rally_until_tick", -1)) != rally_tick + 600
	_check("rally_applies_doc_modifier_in_doc_range_only", bool(rally.get("ok", false)) and int(rally.get("battalions", 0)) == 1 and rallied_battalion and second_unaffected, str(rally))
	var rally_event := _last_power_event(sim, "power.cast")
	_check("rally_cast_event_carries_doc_bindings", String(rally_event.get("power_id", "")) == "SpellBookRallyingCall" and String(rally_event.get("sound_id", "")) == "SpellRallyingCallMS" and float(rally_event.get("radius_source", 0.0)) == 100.0, str(rally_event))
	_check("rally_cooldown_from_doc", int(sim.power_cooldown_state(0, "SpellBookRallyingCall").get("remaining_ticks", 0)) == 1200)
	# A staged pick that gets cast is spent: RESET must not refund it.
	var sim_two = SimScript.new()
	sim_two.configure_spellbook_runtime(doc)
	sim_two.setup({}, _rules())
	sim_two.team_power_points[0] = 30
	sim_two.purchase_power(0, "SpellBookHeal")
	var two_battalion: Dictionary = sim_two.entity(1)
	var two_health: Array = two_battalion.get("member_health", [])
	two_health[0] = 1
	two_battalion["member_health"] = two_health
	two_battalion["health"] = _sum_ints(two_health)
	sim_two.cast_power(0, "SpellBookHeal", Vector2(two_battalion["position"]))
	var spent_reset: Dictionary = sim_two.reset_spellbook_purchases(0)
	_check("cast_spends_the_staged_pick", int(spent_reset.get("refunded", -1)) == 0 and sim_two.has_power(0, "SpellBookHeal"), str(spent_reset))
	# Legacy wrappers stay live for existing callers.
	var wrapped: Dictionary = sim_two.cast_heal(0, Vector2(battalion["position"]))
	_check("legacy_cast_wrappers_route_through_doc", String(wrapped.get("reason", "")) == "power-recharging" or bool(wrapped.get("ok", false)))


## --- D2. Converted effect casts (strikes, summons, tower, grove, stun) ---

func _effect_sim(doc: Dictionary) -> Object:
	var sim = SimScript.new()
	sim.configure_spellbook_runtime(doc)
	sim.setup({}, _rules())
	sim.ai_enabled = false
	sim.team_power_points[0] = 300
	return sim


func _sim_effect_casts(doc: Dictionary) -> void:
	_volley_checks(doc)
	_earthquake_checks(doc)
	_summon_checks(doc)
	_lone_tower_checks(doc)
	_elven_wood_checks(doc)
	_cloud_break_checks(doc)


func _volley_checks(doc: Dictionary) -> void:
	var sim = _effect_sim(doc)
	sim.purchase_power(0, "SpellBookHeal")
	sim.purchase_power(0, "SpellBookArrowVolleyGood")
	var enemy: Dictionary = sim.entity(101)
	var enemy_pos := Vector2(enemy["position"])
	var enemy_health_before := int(enemy.get("health", 0))
	var ally_health_before := int((sim.entity(1) as Dictionary).get("health", 0))
	var cast: Dictionary = sim.cast_power(0, "SpellBookArrowVolleyGood", enemy_pos)
	_check("volley_cast_starts_doc_cooldown", bool(cast.get("ok", false)) and int(sim.power_cooldown_state(0, "SpellBookArrowVolleyGood").get("remaining_ticks", 0)) == 3600, str(cast))
	for _i in range(46):
		sim.tick()
	_check("volley_waits_for_authored_fire_delay", int(sim.entity(101).get("health", 0)) == enemy_health_before and _last_power_event(sim, "power.strike").is_empty())
	sim.tick()
	var strike := _last_power_event(sim, "power.strike")
	_check(
		"volley_strike_deals_converted_damage_in_radius",
		not strike.is_empty() and float(strike.get("damage", 0.0)) == 800.0 and int(strike.get("battalions", 0)) >= 1 and int(sim.entity(101).get("health", 0)) < enemy_health_before,
		str(strike)
	)
	_check("volley_leaves_allies_unharmed", int((sim.entity(1) as Dictionary).get("health", 0)) == ally_health_before)


func _earthquake_checks(doc: Dictionary) -> void:
	var sim = _effect_sim(doc)
	sim.purchase_power(0, "SpellBookHeal")
	sim.purchase_power(0, "SpellBookArrowVolleyGood")
	sim.purchase_power(0, "SpellBookCloudBreak")
	sim.purchase_power(0, "SpellBookEarthquake")
	var enemy: Dictionary = sim.entity(101)
	var enemy_pos := Vector2(enemy["position"])
	var enemy_health_before := int(enemy.get("health", 0))
	var cast: Dictionary = sim.cast_power(0, "SpellBookEarthquake", enemy_pos)
	_check("earthquake_cast_starts_doc_cooldown", bool(cast.get("ok", false)) and int(sim.power_cooldown_state(0, "SpellBookEarthquake").get("remaining_ticks", 0)) == 7200, str(cast))
	for _i in range(34):
		sim.tick()
	var strikes: Array = []
	for event_value in sim.events:
		if String((event_value as Dictionary).get("kind", "")) == "power.strike":
			strikes.append(event_value)
	var damages: Array = []
	var ticks: Array = []
	for strike in strikes:
		damages.append(float((strike as Dictionary).get("damage", 0.0)))
		ticks.append(int((strike as Dictionary).get("tick", -1)))
	_check(
		"earthquake_phases_fire_in_authored_order_and_delays",
		damages == [2000.0, 1500.0, 1000.0, 500.0] and ticks == [8, 12, 18, 34],
		"damages=%s ticks=%s" % [str(damages), str(ticks)]
	)
	_check("earthquake_damages_enemy_battalion", int(sim.entity(101).get("health", 0)) < enemy_health_before, "%d < %d" % [int(sim.entity(101).get("health", 0)), enemy_health_before])


func _summon_checks(doc: Dictionary) -> void:
	# Rohan Allies: five hordes of five with converted stats and the lifetime.
	var rohan_sim = _effect_sim(doc)
	rohan_sim.purchase_power(0, "SpellBookHeal")
	rohan_sim.purchase_power(0, "SpellBookArrowVolleyGood")
	rohan_sim.purchase_power(0, "SpellBookRohanAllies")
	var roster_before: Array[int] = rohan_sim.entity_ids()
	var cast: Dictionary = rohan_sim.cast_power(0, "SpellBookRohanAllies", Vector2.ZERO)
	_check("rohan_cast_accepted", bool(cast.get("ok", false)), str(cast))
	for _i in range(19):
		rohan_sim.tick()
	_check("rohan_hatch_waits_for_egg_delay", rohan_sim.entity_ids() == roster_before)
	rohan_sim.tick()
	var summon := _last_power_event(rohan_sim, "power.summon")
	var spawned: Array = summon.get("spawned", [])
	_check("rohan_hatches_five_hordes", spawned.size() == 5, str(spawned))
	var horde_ok := spawned.size() == 5
	for entity_id_value in spawned:
		var row: Dictionary = rohan_sim.entity(int(entity_id_value))
		if int(row.get("member_count", 0)) != 5 or int(row.get("member_maximum_health", 0)) != 450 or int(row.get("member_damage", 0)) != 80 or String(row.get("category", "")) != "cavalry":
			horde_ok = false
	_check("rohan_hordes_carry_converted_stats", horde_ok)
	var lifetime_tick: int = rohan_sim.tick_index
	for _i in range(749):
		rohan_sim.tick()
	_check("rohan_summons_persist_for_lifetime", int(rohan_sim.entity(int(spawned[0])).get("health", 0)) > 0)
	rohan_sim.tick()
	_check("rohan_summons_fade_at_authored_lifetime", int(rohan_sim.entity(int(spawned[0])).get("health", -1)) == 0 and not _last_power_event(rohan_sim, "power.summon_expired").is_empty(), "tick=%d" % (rohan_sim.tick_index - lifetime_tick))
	# Dunedain: three hordes of ten rangers (warhead damage through the bow chain).
	var dun_sim = _effect_sim(doc)
	dun_sim.purchase_power(0, "SpellBookHeal")
	dun_sim.purchase_power(0, "SpellBookRallyingCall")
	dun_sim.purchase_power(0, "SpellBookTomBombadil")
	dun_sim.purchase_power(0, "SpellBookDunedainAllies")
	dun_sim.cast_power(0, "SpellBookDunedainAllies", Vector2.ZERO)
	for _i in range(20):
		dun_sim.tick()
	var dun_spawned: Array = _last_power_event(dun_sim, "power.summon").get("spawned", [])
	var dun_ok := dun_spawned.size() == 3
	for entity_id_value in dun_spawned:
		var row: Dictionary = dun_sim.entity(int(entity_id_value))
		if int(row.get("member_count", 0)) != 10 or int(row.get("member_maximum_health", 0)) != 300 or int(row.get("member_damage", 0)) != 65:
			dun_ok = false
	_check("dunedain_hordes_carry_converted_stats", dun_ok, str(dun_spawned.size()))
	# Tom Bombadil: one hero with converted punch and 60s lifetime.
	var tom_sim = _effect_sim(doc)
	tom_sim.purchase_power(0, "SpellBookHeal")
	tom_sim.purchase_power(0, "SpellBookRallyingCall")
	tom_sim.purchase_power(0, "SpellBookTomBombadil")
	tom_sim.cast_power(0, "SpellBookTomBombadil", Vector2.ZERO)
	for _i in range(20):
		tom_sim.tick()
	var tom_spawned: Array = _last_power_event(tom_sim, "power.summon").get("spawned", [])
	var tom_ok := tom_spawned.size() == 1
	if tom_ok:
		var tom: Dictionary = tom_sim.entity(int(tom_spawned[0]))
		tom_ok = int(tom.get("member_maximum_health", 0)) == 5000 and int(tom.get("member_damage", 0)) == 200 and String(tom.get("category", "")) == "hero"
	_check("tom_bombadil_carries_converted_stats", tom_ok)
	# Hobbits: three hordes plus Sam, Frodo, and Merry as single units.
	var hob_sim = _effect_sim(doc)
	hob_sim.purchase_power(0, "SpellBookHeal")
	hob_sim.purchase_power(0, "SpellBookRallyingCall")
	hob_sim.purchase_power(0, "SpellBookElvenWoodMP")
	hob_sim.purchase_power(0, "SpellBookHobbitAllies")
	hob_sim.cast_power(0, "SpellBookHobbitAllies", Vector2.ZERO)
	for _i in range(20):
		hob_sim.tick()
	var hob_spawned: Array = _last_power_event(hob_sim, "power.summon").get("spawned", [])
	_check("hobbits_hatch_horde_and_named_friends", hob_spawned.size() == 7, str(hob_spawned.size()))
	# Army of the Dead: six immortal-bodied hordes of sixteen, 45s lifetime.
	var aod_sim = _effect_sim(doc)
	aod_sim.purchase_power(0, "SpellBookHeal")
	aod_sim.purchase_power(0, "SpellBookArrowVolleyGood")
	aod_sim.purchase_power(0, "SpellBookCloudBreak")
	aod_sim.purchase_power(0, "SpellBookArmyoftheDead")
	aod_sim.cast_power(0, "SpellBookArmyoftheDead", Vector2.ZERO)
	for _i in range(40):
		aod_sim.tick()
	var aod_spawned: Array = _last_power_event(aod_sim, "power.summon").get("spawned", [])
	var aod_ok := aod_spawned.size() == 6
	for entity_id_value in aod_spawned:
		var row: Dictionary = aod_sim.entity(int(entity_id_value))
		if int(row.get("member_count", 0)) != 16 or int(row.get("member_maximum_health", 0)) != 2000:
			aod_ok = false
	_check("army_of_the_dead_hatches_six_hordes_with_converted_stats", aod_ok, str(aod_spawned.size()))


func _lone_tower_checks(doc: Dictionary) -> void:
	var sim = _effect_sim(doc)
	sim.purchase_power(0, "SpellBookHeal")
	sim.purchase_power(0, "SpellBookSpawnLoneTower")
	var tower_point := Vector2(10.0, 0.0)
	var cast: Dictionary = sim.cast_power(0, "SpellBookSpawnLoneTower", tower_point)
	_check("lone_tower_cast_accepted", bool(cast.get("ok", false)) and int(sim.power_cooldown_state(0, "SpellBookSpawnLoneTower").get("remaining_ticks", 0)) == 2400, str(cast))
	var tower_id := 0
	for structure_id in sim.structure_ids(0):
		if String((sim.structure(structure_id) as Dictionary).get("structure_kind", "")) == "lone_tower":
			tower_id = structure_id
	var tower: Dictionary = sim.structure(tower_id)
	_check("lone_tower_spawns_under_construction", tower_id != 0 and int(tower.get("maximum_health", 0)) == 2500 and float(tower.get("construction_progress", 1.0)) == 0.0, str(tower.get("maximum_health", "missing")))
	for _i in range(50):
		sim.tick()
	_check("lone_tower_completes_in_authored_build_time", float(sim.structure(tower_id).get("construction_progress", 0.0)) == 1.0)
	var enemy: Dictionary = sim.entity(101)
	enemy["position"] = tower_point + Vector2(1.0, 0.0)
	var enemy_health_before := int(enemy.get("health", 0))
	for _i in range(20):
		sim.tick()
	_check("lone_tower_fires_converted_bow", int(sim.entity(101).get("health", 0)) < enemy_health_before, "health=%d was=%d" % [int(sim.entity(101).get("health", 0)), enemy_health_before])


func _elven_wood_checks(doc: Dictionary) -> void:
	var sim = _effect_sim(doc)
	sim.purchase_power(0, "SpellBookHeal")
	sim.purchase_power(0, "SpellBookRallyingCall")
	sim.purchase_power(0, "SpellBookElvenWoodMP")
	var battalion: Dictionary = sim.entity(1)
	var grove_point := Vector2(battalion["position"])
	var cast: Dictionary = sim.cast_power(0, "SpellBookElvenWoodMP", grove_point)
	_check("elven_wood_cast_registers_grove", bool(cast.get("ok", false)) and not _last_power_event(sim, "power.grove").is_empty(), str(cast))
	sim.tick()
	var buffed: Dictionary = sim.entity(1)
	_check(
		"grove_aura_applies_converted_armor_modifier",
		int(buffed.get("grove_armor_until", -1)) == sim.tick_index + 30 and float(buffed.get("grove_armor_mult", 1.0)) == 0.5 and sim._grove_armor_factor(buffed) == 0.5,
		"until=%d mult=%s" % [int(buffed.get("grove_armor_until", -1)), str(buffed.get("grove_armor_mult", 0.0))]
	)
	var hero_check := true
	for id in sim.living_ids(0):
		var row: Dictionary = sim.entity(id)
		if String(row.get("category", "")) == "hero" and int(row.get("grove_armor_until", -1)) >= sim.tick_index:
			hero_check = false
	_check("grove_aura_excludes_heroes_per_filter", hero_check)
	for _i in range(3000):
		sim.tick()
	# The grove is gone; the buff lingers only for the modifier's duration.
	for _i in range(35):
		sim.tick()
	var after: Dictionary = sim.entity(1)
	_check("grove_aura_expires_with_authored_lifetime", sim._grove_armor_factor(after) == 1.0)


func _cloud_break_checks(doc: Dictionary) -> void:
	var sim = _effect_sim(doc)
	sim.purchase_power(0, "SpellBookHeal")
	sim.purchase_power(0, "SpellBookArrowVolleyGood")
	sim.purchase_power(0, "SpellBookCloudBreak")
	var enemy: Dictionary = sim.entity(101)
	var enemy_pos := Vector2(enemy["position"])
	var cast_tick: int = sim.tick_index
	var cast: Dictionary = sim.cast_power(0, "SpellBookCloudBreak", enemy_pos)
	var stunned := int(cast.get("battalions", -1))
	var enemy_after: Dictionary = sim.entity(101)
	_check(
		"cloud_break_stuns_enemies_for_weather_duration",
		bool(cast.get("ok", false)) and stunned >= 1 and int(enemy_after.get("stun_until_tick", -1)) == cast_tick + 300 and (enemy_after.get("route", []) as Array).is_empty(),
		str(cast)
	)
	var frozen := Vector2(sim.entity(101)["position"])
	# AI orders from tick 15 must not move a stunned battalion; once the
	# weather duration elapses the same orders drive it forward.
	sim.ai_enabled = true
	for _i in range(299):
		sim.tick()
	var stunned_row: Dictionary = sim.entity(101)
	_check("stunned_battalion_holds_position", Vector2(stunned_row["position"]).is_equal_approx(frozen))
	_check("stun_gate_overrides_ai_orders_during_stun", int(stunned_row.get("health", 0)) == 500, "health=%d" % int(stunned_row.get("health", 0)))
	var recovered := false
	for _i in range(120):
		sim.tick()
		if not Vector2((sim.entity(101) as Dictionary)["position"]).is_equal_approx(frozen):
			recovered = true
			break
	_check("battalion_recovers_after_duration", recovered)

func _sim_economy_checks(doc: Dictionary) -> void:
	var sim = SimScript.new()
	sim.configure_spellbook_runtime(doc)
	var rules := _rules()
	rules["power_point_kills"] = 2
	sim.setup({}, rules)
	sim.award_power_kill(0)
	_check("kill_below_rules_rate_earns_nothing", sim.power_points(0) == 1, str(sim.power_points(0)))
	sim.award_power_kill(0)
	_check("kills_at_rules_rate_earn_point", sim.power_points(0) == 2 and not _last_power_event(sim, "power.point_earned").is_empty(), str(sim.power_points(0)))


## --- E2. Sim clock pause seam (single-player spellbook pause) ---

func _sim_pause_checks(doc: Dictionary) -> void:
	var sim = SimScript.new()
	sim.configure_spellbook_runtime(doc)
	sim.setup({}, _rules())
	_check("clock_unpaused_after_setup", not sim.clock_paused)
	# March a battalion so the halt covers movement, not just the tick counter.
	sim.issue_move([1], Vector2(-20.0, 0.0))
	var before := Vector2(sim.entity(1).get("position", Vector2.ZERO))
	for _i in range(10):
		sim.tick()
	var marched := Vector2(sim.entity(1).get("position", Vector2.ZERO))
	_check("ticks_advance_while_closed", sim.tick_index == 10 and not marched.is_equal_approx(before), "tick=%d" % sim.tick_index)
	sim.set_spellbook_orb_open(true)
	_check("orb_open_sets_clock_pause", sim.clock_paused)
	var paused_tick: int = sim.tick_index
	for _i in range(10):
		sim.tick()
	var after := Vector2(sim.entity(1).get("position", Vector2.ZERO))
	_check("ticks_halt_while_orb_open", sim.tick_index == paused_tick and after.is_equal_approx(marched), "tick=%d pos=%s" % [sim.tick_index, str(after)])
	sim.set_spellbook_orb_open(false)
	for _i in range(10):
		sim.tick()
	_check("ticks_resume_exactly_on_close", sim.tick_index == paused_tick + 10 and not Vector2(sim.entity(1).get("position", Vector2.ZERO)).is_equal_approx(after), "tick=%d" % sim.tick_index)
	# Match setup always restarts unpaused.
	sim.set_spellbook_orb_open(true)
	sim.setup({}, _rules())
	_check("setup_clears_clock_pause", not sim.clock_paused)


## --- F. Sim-provided orb state ---

func _sim_ui_state_checks(doc: Dictionary) -> void:
	var sim = SimScript.new()
	sim.configure_spellbook_runtime(doc)
	sim.setup({}, _rules())
	sim.team_power_points[0] = 6
	var state: Dictionary = sim.spellbook_ui_state(0)
	var powers: Dictionary = state.get("powers", {})
	_check("ui_state_covers_full_tree", powers.size() == 12 and int(state.get("points", -1)) == 6)
	var heal: Dictionary = powers.get("SpellBookHeal", {})
	var tower: Dictionary = powers.get("SpellBookSpawnLoneTower", {})
	_check("ui_state_purchasable_and_locked_reasons", bool(heal.get("purchasable", false)) and not bool(tower.get("purchasable", false)) and String(tower.get("locked_reason", "")) == "prerequisites-unmet", "%s / %s" % [str(heal), str(tower)])
	var volley: Dictionary = powers.get("SpellBookArrowVolleyGood", {})
	var prereqs: Array = volley.get("prereq_power_ids", [])
	_check("prereq_forks_from_doc_groups", prereqs.has("SpellBookHeal") and prereqs.has("SpellBookRallyingCall"), str(prereqs))
	sim.purchase_power(0, "SpellBookHeal")
	var staged_state: Dictionary = (sim.spellbook_ui_state(0).get("powers", {}) as Dictionary).get("SpellBookHeal", {})
	sim.accept_spellbook_purchases(0)
	var committed_state: Dictionary = (sim.spellbook_ui_state(0).get("powers", {}) as Dictionary).get("SpellBookHeal", {})
	_check("staged_pick_marks_until_accept", bool(staged_state.get("staged", false)) and not bool(committed_state.get("staged", false)))


## --- G. HUD orb integration (real doc, real sim state) ---

func _hud_integration_checks(content_db, doc: Dictionary) -> void:
	HudScript = load("res://src/retail_slice/retail_hud.gd")
	_check("hud_script_available", HudScript != null)
	if HudScript == null:
		return
	var soldier_definition: Dictionary = content_db.get_bundle_object("bfme2.object.gondor-fighter")
	var selected_pack_root := String(soldier_definition.get("_pack_root", ""))
	_check("private_men_pack_selected", selected_pack_root != "" and selected_pack_root.contains("bfme2-men-vslice"), selected_pack_root)
	var hud = HudScript.new()
	root.add_child(hud)
	hud.build()
	hud.configure_spellbook_runtime(doc)
	var rows: Array = hud._spellbook_power_rows
	_check("orb_rows_drive_from_doc", rows.size() == 12)
	var order_ok := rows.size() == 12
	var labels_ok := rows.size() == 12
	for index in mini(rows.size(), EXPECTED_POWER_ORDER.size()):
		var row: Dictionary = rows[index]
		if String(row.get("power_id", "")) != EXPECTED_POWER_ORDER[index] or int(row.get("cost", -1)) != EXPECTED_COSTS[index] or int(row.get("purchase_slot", -1)) != index + 1:
			order_ok = false
		if String(row.get("label", "")) == "" or String(row.get("tooltip", "")) == "":
			labels_ok = false
	_check("orb_rows_match_purchase_slots_and_costs", order_ok)
	_check("orb_labels_and_tooltips_are_pack_strings", labels_ok)
	_check("orb_display_name_is_pack_string", hud.power_display_name("SpellBookHeal") == "Heal", hud.power_display_name("SpellBookHeal"))
	var bind_error: String = hud.bind_retail_train_commands(content_db, selected_pack_root, true)
	_check("retail_art_binds_for_orb", bind_error == "", bind_error)
	var buttons_ok: bool = hud.power_buttons.size() == 12
	var icons_bound := true
	for index in hud.power_buttons.size():
		var button: Button = hud.power_buttons[index]
		if String(button.get_meta("power_id", "")) != EXPECTED_POWER_ORDER[index] or int(button.get_meta("power_cost", -1)) != EXPECTED_COSTS[index]:
			buttons_ok = false
		if button.icon == null:
			icons_bound = false
	_check("orb_twelve_buttons_in_slot_order_with_doc_costs", buttons_ok)
	_check("orb_icons_are_converted_spellbook_crops", icons_bound)
	# Live sim state drives the orb visuals.
	var sim = SimScript.new()
	sim.configure_spellbook_runtime(doc)
	sim.setup({}, _rules())
	sim.team_power_points[0] = 30
	hud.refresh_powers(sim.power_points(0), sim.purchased_powers[0], sim.spellbook_ui_state(0))
	var heal_button := _power_button(hud, "SpellBookHeal")
	var quake_button := _power_button(hud, "SpellBookEarthquake")
	_check(
		"orb_states_locked_dim_purchasable_glow",
		heal_button != null and quake_button != null and heal_button.self_modulate.is_equal_approx(Color(1.15, 1.25, 1.05)) and quake_button.self_modulate.is_equal_approx(Color(0.42, 0.46, 0.42, 0.85)),
		"%s / %s" % [str(heal_button.self_modulate) if heal_button != null else "null", str(quake_button.self_modulate) if quake_button != null else "null"]
	)
	sim.purchase_power(0, "SpellBookHeal")
	hud.refresh_powers(sim.power_points(0), sim.purchased_powers[0], sim.spellbook_ui_state(0))
	_check("orb_owned_state_lits_icon", heal_button.self_modulate.is_equal_approx(Color(1.05, 1.08, 0.92)), str(heal_button.self_modulate))
	_check("dock_lists_owned_castable_power", hud.powers_dock_buttons.has("SpellBookHeal"), str(hud.powers_dock_buttons.keys()))
	_check("dock_skips_locked_effects", not hud.powers_dock_buttons.has("SpellBookArrowVolleyGood"))
	# Dock population: every owned castable power docks, in the authored cast
	# command-set order (MenSpellBookCommandSet), not purchase-click order.
	sim.team_power_points[0] = 30
	sim.purchase_power(0, "SpellBookRallyingCall")
	hud.refresh_powers(sim.power_points(0), sim.purchased_powers[0], sim.spellbook_ui_state(0))
	_check("all_owned_castable_powers_dock", hud.powers_dock_buttons.has("SpellBookHeal") and hud.powers_dock_buttons.has("SpellBookRallyingCall"), str(hud.powers_dock_buttons.keys()))
	var heal_dock: Button = hud.powers_dock_buttons.get("SpellBookHeal")
	var rally_dock: Button = hud.powers_dock_buttons.get("SpellBookRallyingCall")
	# Cast slots: heal=1, rally=2 — even though rally was purchased second.
	_check("dock_orders_by_authored_cast_slot", heal_dock != null and rally_dock != null and heal_dock.position.y < rally_dock.position.y, "%s vs %s" % [str(heal_dock.position), str(rally_dock.position)])
	# Dock invariants (both palantir-rim layouts): the column hangs at the
	# rim's left edge (x = FIRST_CENTER.x), cast slots stride by the authored
	# RETAIL_POWER_DOCK_STRIDE, lowest cast slot first, all inside the palantir
	# rim band (never the screen's right edge).
	var viewport_h := root.size.y
	var dock_top: float = viewport_h - HudScript.RETAIL_PALANTIR_FRAME_DISPLAY_SIZE.y
	var heal_center: Vector2 = heal_dock.position + heal_dock.size * 0.5
	var rally_center: Vector2 = rally_dock.position + rally_dock.size * 0.5
	_check(
		"dock_anchors_at_palantir_left_rim",
		is_equal_approx(heal_center.x, HudScript.RETAIL_POWER_DOCK_FIRST_CENTER.x)
			and is_equal_approx(rally_center.x, HudScript.RETAIL_POWER_DOCK_FIRST_CENTER.x)
			and (rally_center - heal_center).is_equal_approx(Vector2(0.0, HudScript.RETAIL_POWER_DOCK_STRIDE))
			and heal_center.y > dock_top - HudScript.RETAIL_POWER_DOCK_STRIDE * 3.0
			and heal_center.y < viewport_h + 10.0,
		"first=%s dock_top=%s" % [str(heal_center), str(dock_top)]
	)
	var battalion: Dictionary = sim.entity(1)
	var wounded: Array = battalion.get("member_health", [])
	wounded[0] = 1
	battalion["member_health"] = wounded
	battalion["health"] = _sum_ints(wounded)
	sim.cast_power(0, "SpellBookHeal", Vector2(battalion["position"]))
	hud.refresh_powers(sim.power_points(0), sim.purchased_powers[0], sim.spellbook_ui_state(0))
	var sweep := heal_button.get_node_or_null("CooldownSweep")
	var sweep_progress: float = float(sweep.get("progress")) if sweep != null else -1.0
	_check("orb_cooldown_sweep_tracks_doc_reload", sweep != null and sweep.visible and sweep_progress < 0.01, str(sweep_progress))
	_check("points_header_shows_balance", hud.power_points_label.text == "%d" % sim.power_points(0), hud.power_points_label.text)
	# Orb chrome signals: RESET re-emits for the sim; ACCEPT closes (commit).
	var reset_seen := [false]
	var closed_seen := [false]
	hud.powers_reset_requested.connect(func() -> void: reset_seen[0] = true)
	hud.powers_closed.connect(func() -> void: closed_seen[0] = true)
	hud.powers_palette.visible = true
	hud.powers_palette.reset_button.pressed.emit()
	_check("orb_reset_button_re_emits", reset_seen[0])
	hud.powers_palette.accept_button.pressed.emit()
	_check("orb_accept_closes_and_commits", closed_seen[0] and not hud.powers_palette.visible)
	# Esc close path stays live and also commits.
	closed_seen[0] = false
	hud.powers_palette.visible = true
	_check("esc_close_path_commits", hud.close_powers_palette() and closed_seen[0])
	# Dock mirrors the cast cooldown: dim while recharging, lit when ready.
	hud.refresh_powers(sim.power_points(0), sim.purchased_powers[0], sim.spellbook_ui_state(0))
	_check("dock_dims_during_cooldown", heal_dock.self_modulate.is_equal_approx(Color(0.45, 0.45, 0.45)), str(heal_dock.self_modulate))
	sim.tick_index += 1800
	hud.refresh_powers(sim.power_points(0), sim.purchased_powers[0], sim.spellbook_ui_state(0))
	_check("dock_relits_after_cooldown", heal_dock.self_modulate.is_equal_approx(Color.WHITE), str(heal_dock.self_modulate))
	# ACCEPT/RESET chrome: green frame + gold text, hover glow per HUD
	# convention (retail_hud.gd _wire_button_feel modulation values).
	var reset_chrome: Button = hud.powers_palette.reset_button
	var accept_chrome: Button = hud.powers_palette.accept_button
	var hover_style := reset_chrome.get_theme_stylebox("hover") as StyleBoxFlat
	var normal_style := reset_chrome.get_theme_stylebox("normal") as StyleBoxFlat
	_check(
		"orb_chrome_buttons_retail_gold_and_frame",
		reset_chrome.get_theme_color("font_color").is_equal_approx(Color("e9d489")) and accept_chrome.get_theme_color("font_color").is_equal_approx(Color("e9d489")) and hover_style != null and normal_style != null and hover_style.border_color != normal_style.border_color,
		"hover_border=%s normal_border=%s" % [str(hover_style.border_color) if hover_style != null else "none", str(normal_style.border_color) if normal_style != null else "none"]
	)
	reset_chrome.mouse_entered.emit()
	var hover_glow := reset_chrome.self_modulate
	reset_chrome.mouse_exited.emit()
	_check("orb_chrome_hover_glow_matches_hud", hover_glow.is_equal_approx(Color(1.22, 1.16, 1.02)) and reset_chrome.self_modulate.is_equal_approx(Color.WHITE), str(hover_glow))
	# 1:1 layout: tier rows from the authored MenSpellStoreCommandSet grid.
	var groups: Array = hud.powers_palette._tier_groups()
	var row_ids: Array = []
	for group in groups:
		var ids: Array = []
		for row in group:
			ids.append(String((row as Dictionary).get("power_id", "")))
		row_ids.append(ids)
	var expected_rows := [
		["SpellBookHeal", "SpellBookRallyingCall", "SpellBookElvenWoodMP"],
		["SpellBookSpawnLoneTower", "SpellBookArrowVolleyGood", "SpellBookTomBombadil", "SpellBookHobbitAllies"],
		["SpellBookRohanAllies", "SpellBookCloudBreak", "SpellBookDunedainAllies"],
		["SpellBookArmyoftheDead", "SpellBookEarthquake"],
	]
	_check("orb_grid_rows_match_authored_commandset", row_ids == expected_rows, str(row_ids))
	var anchors_ordered := groups.size() == 4
	var previous_row_y := -1.0
	for group in groups:
		var previous_x := -INF
		var row_y := -1.0
		for row in group:
			var anchor: Vector2 = hud.powers_palette.power_anchor(String((row as Dictionary).get("power_id", "")))
			if anchor.x <= previous_x:
				anchors_ordered = false
			previous_x = anchor.x
			row_y = anchor.y
		if previous_row_y >= 0.0 and row_y <= previous_row_y:
			anchors_ordered = false
		previous_row_y = row_y
	_check("orb_grid_positions_follow_slot_order", anchors_ordered)
	# Fork edges: the exact MEN-relevant prereq links from the doc's OR groups.
	var ui_powers: Dictionary = (sim.spellbook_ui_state(0).get("powers", {}) as Dictionary)
	var expected_edges := {
		"SpellBookSpawnLoneTower": ["SpellBookHeal"],
		"SpellBookArrowVolleyGood": ["SpellBookHeal", "SpellBookRallyingCall"],
		"SpellBookTomBombadil": ["SpellBookRallyingCall", "SpellBookElvenWoodMP"],
		"SpellBookHobbitAllies": ["SpellBookElvenWoodMP"],
		"SpellBookRohanAllies": ["SpellBookSpawnLoneTower", "SpellBookArrowVolleyGood"],
		"SpellBookCloudBreak": ["SpellBookTomBombadil", "SpellBookArrowVolleyGood"],
		"SpellBookDunedainAllies": ["SpellBookTomBombadil", "SpellBookHobbitAllies"],
		"SpellBookArmyoftheDead": ["SpellBookCloudBreak", "SpellBookRohanAllies"],
		"SpellBookEarthquake": ["SpellBookDunedainAllies", "SpellBookCloudBreak"],
	}
	var edges_ok := true
	for dependent_id in expected_edges.keys():
		var prereqs: Array = (ui_powers.get(dependent_id, {}) as Dictionary).get("prereq_power_ids", [])
		var expected: Array = expected_edges[dependent_id]
		prereqs.sort()
		expected = expected.duplicate()
		expected.sort()
		if prereqs != expected:
			edges_ok = false
	_check("orb_fork_edges_match_doc_prereq_groups", edges_ok)
	hud.free()


func _power_button(hud, power_id: String) -> Button:
	for button in hud.power_buttons:
		if String((button as Button).get_meta("power_id", "")) == power_id:
			return button
	return null


## --- H. Cast audio routes through the doc's sound bindings ---

func _audio_routing_checks(content_db, doc: Dictionary) -> void:
	var AudioScript = load("res://src/retail_slice/retail_slice_audio.gd")
	_check("audio_script_available", AudioScript != null)
	if AudioScript == null:
		return
	var soldier_definition: Dictionary = content_db.get_bundle_object("bfme2.object.gondor-fighter")
	var pack_root := String(soldier_definition.get("_pack_root", ""))
	var audio = AudioScript.new()
	root.add_child(audio)
	audio.configure(pack_root, false, {})
	var powers: Array = ((doc.get("registration", {}) as Dictionary).get("powerTree", {}) as Dictionary).get("powers", []) as Array
	var sound_ids: Array = []
	for power_value in powers:
		var sound_id := String((power_value as Dictionary).get("initiateSoundId", ""))
		if sound_id != "" and not sound_ids.has(sound_id):
			sound_ids.append(sound_id)
	var unresolved: Array = []
	for sound_id in sound_ids:
		if not bool((audio.route_audio_event(sound_id, 1) as Dictionary).get("ok", false)):
			unresolved.append(sound_id)
	_check("every_power_sound_routes_through_pack", sound_ids.size() == 11 and unresolved.is_empty(), "ids=%d unresolved=%s" % [sound_ids.size(), str(unresolved)])
	audio.audio_event_routes.erase("spellheal")
	audio._consume_event({"kind": "power.cast", "sequence": 999, "entity_id": 0, "target_id": 0, "sound_id": "SpellHeal"})
	# The lazy promotion into the route table proves the cast branch dispatched.
	_check("power_cast_event_consumed_by_audio", audio.audio_event_routes.has("spellheal"))
	audio.free()


## --- F2. Per-faction spellbook trees (each faction's own doc + pack) ---

const PER_FACTION_SPELLBOOKS := {
	"Elves": {"fragment": "elves-vslice", "intrinsic": "SCIENCE_ELVES", "slot_one": "SpellBookRallyingCall", "cast_power": "SpellBookRallyingCall", "cast_check": "rally"},
	"Dwarves": {"fragment": "dwarves-vslice", "intrinsic": "SCIENCE_DWARVES", "slot_one": "SpellBookRallyingCall", "cast_power": "SpellBookHeal", "cast_check": "heal"},
	"Isengard": {"fragment": "isengard-vslice", "intrinsic": "SCIENCE_ISENGARD", "slot_one": "SpellBookPalantirVision", "cast_power": "SpellBookWarChant", "cast_check": "rally"},
	"Mordor": {"fragment": "mordor-vslice", "intrinsic": "SCIENCE_MORDOR", "slot_one": "SpellBookTaint", "cast_power": "SpellBookWarChant", "cast_check": "rally"},
	"Wild": {"fragment": "wild-vslice", "intrinsic": "SCIENCE_WILD", "slot_one": "SpellBookWarChant", "cast_power": "SpellBookWarChant", "cast_check": "rally"},
}

const MEN_POWER_ORDER_PREFIX := "SpellBookHeal"


func _per_faction_checks(content_db) -> void:
	var mod_loader = root.get_node_or_null("ModLoader")
	_check("mod_loader_available_for_packs", mod_loader != null)
	if mod_loader == null:
		return
	var pack_roots: Array = mod_loader.list_pack_roots()
	var hud_script = load("res://src/retail_slice/retail_hud.gd")
	for faction in PER_FACTION_SPELLBOOKS.keys():
		var spec: Dictionary = PER_FACTION_SPELLBOOKS[faction]
		var doc := _faction_spellbook_doc(mod_loader, pack_roots, faction, String(spec["fragment"]))
		_check(
			"%s_spellbook_doc_resolves_from_own_pack" % faction.to_lower(),
			not doc.is_empty() and String((doc.get("target", {}) as Dictionary).get("faction", "")) == faction and String(doc.get("_pack_root", "")).contains(String(spec["fragment"])),
			String(doc.get("_pack_root", "missing"))
		)
		if doc.is_empty():
			continue
		var sim = SimScript.new()
		_check("%s_tree_configures_from_faction_doc" % faction.to_lower(), sim.configure_spellbook_runtime(doc), sim.spellbook_error())
		var power_ids := sim.spellbook_power_ids()
		_check("%s_tree_has_twelve_faction_powers" % faction.to_lower(), power_ids.size() == 12, str(power_ids.size()))
		if faction != "Men":
			_check("%s_tree_is_not_the_men_tree" % faction.to_lower(), power_ids[0] != MEN_POWER_ORDER_PREFIX or String(spec["slot_one"]) == MEN_POWER_ORDER_PREFIX, str(power_ids[0]))
		_check(
			"%s_intrinsic_science_from_doc" % faction.to_lower(),
			sim.owned_sciences(0) == [String(spec["intrinsic"])],
			str(sim.owned_sciences(0))
		)
		# Authored slot order and cost drive the tree identically per doc.
		var expected_order: Array = []
		var sciences: Array = ((doc.get("registration", {}) as Dictionary).get("powerTree", {}) as Dictionary).get("sciences", []) as Array
		var purchasable: Array = []
		for science_value in sciences:
			var science := science_value as Dictionary
			if not (science.get("purchase", {}) as Dictionary).is_empty():
				purchasable.append(science)
		purchasable.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
			return int((a.get("purchase", {}) as Dictionary).get("slot", 0)) < int((b.get("purchase", {}) as Dictionary).get("slot", 0))
		)
		var slots_ok := purchasable.size() == 12
		for science in purchasable:
			var science_id := String(science.get("id", ""))
			var found := ""
			for power_id in power_ids:
				if String((sim.spellbook_power(power_id) as Dictionary).get("science_id", "")) == science_id:
					found = power_id
					break
			expected_order.append(found)
			if int((science.get("purchase", {}) as Dictionary).get("slot", 0)) != expected_order.size() or found == "":
				slots_ok = false
		_check("%s_authored_slots_match_doc_sequence" % faction.to_lower(), slots_ok and expected_order == power_ids, str(expected_order))
		# Purchase + cooldown drive from the doc: slot-1 is open with the
		# intrinsic science; a 25-cost tier-4 power stays gated.
		sim.setup({}, _rules())
		sim.team_power_points[0] = 100
		var slot_one := String(spec["slot_one"])
		var slot_one_row: Dictionary = sim.spellbook_power(slot_one)
		_check(
			"%s_slot_one_purchases_at_doc_cost" % faction.to_lower(),
			int(slot_one_row.get("cost", -1)) == 5 and bool(sim.purchase_power(0, slot_one).get("ok", false)),
			str(slot_one_row.get("cost", -1))
		)
		var tier_four := ""
		for power_id in power_ids:
			if int((sim.spellbook_power(power_id) as Dictionary).get("cost", 0)) == 25:
				tier_four = power_id
				break
		_check(
			"%s_tier_four_gated_by_prereq_groups" % faction.to_lower(),
			tier_four != "" and String(sim.purchase_power(0, tier_four).get("reason", "")) == "prerequisites-unmet",
			tier_four
		)
		_check(
			"%s_cooldown_total_from_doc_reload" % faction.to_lower(),
			int(sim.power_cooldown_state(0, slot_one).get("total_ticks", 0)) == int(slot_one_row.get("reload_ticks", -2)),
			str(sim.power_cooldown_state(0, slot_one))
		)
		# The orb shows THIS faction's powers (row order = authored slots).
		if hud_script != null:
			var hud = hud_script.new()
			root.add_child(hud)
			hud.build()
			hud.configure_spellbook_runtime(doc)
			var rows: Array = hud._spellbook_power_rows
			var row_ids: Array = []
			for row in rows:
				row_ids.append(String((row as Dictionary).get("power_id", "")))
			_check("%s_orb_rows_are_faction_powers_not_men" % faction.to_lower(), row_ids == power_ids, str(row_ids))
			# Icons bind from the faction's own pack crops.
			var icons_ok := not rows.is_empty()
			for row in rows:
				var icon_id := String((row as Dictionary).get("icon_id", ""))
				if hud._spellbook_icon_from_doc_pack(icon_id) == null:
					icons_ok = false
			_check("%s_orb_icons_bind_from_faction_pack" % faction.to_lower(), icons_ok)
			hud.free()
		# Cast effects where the doc's effect shapes carry them (rally/heal class).
		_faction_cast_check(faction, doc, spec)


func _faction_cast_check(faction: String, doc: Dictionary, spec: Dictionary) -> void:
	var sim = SimScript.new()
	sim.configure_spellbook_runtime(doc)
	sim.setup({}, _rules())
	sim.ai_enabled = false
	sim.team_power_points[0] = 100
	var power_id := String(spec["cast_power"])
	var row: Dictionary = sim.spellbook_power(power_id)
	if not bool(row.get("castable", false)):
		_check("%s_cast_effect_check" % faction.to_lower(), false, "expected castable: %s (%s)" % [power_id, String(row.get("locked_reason", ""))])
		return
	sim.purchase_power(0, power_id)
	var battalion: Dictionary = sim.entity(1)
	var kind := String(spec["cast_check"])
	if kind == "heal":
		var wounded: Array = battalion.get("member_health", [])
		wounded[0] = 1
		battalion["member_health"] = wounded
		battalion["health"] = _sum_ints(wounded)
		var healed: Dictionary = sim.cast_power(0, power_id, Vector2(battalion["position"]))
		_check(
			"%s_heal_cast_restores_wounded_members" % faction.to_lower(),
			bool(healed.get("ok", false)) and int((sim.entity(1) as Dictionary).get("member_health", [])[0]) > 1,
			str(healed)
		)
	elif kind == "rally":
		var rally_tick: int = sim.tick_index
		var rallied: Dictionary = sim.cast_power(0, power_id, Vector2(battalion["position"]))
		var after: Dictionary = sim.entity(1)
		_check(
			"%s_war_chant_rallies_with_doc_modifier" % faction.to_lower(),
			bool(rallied.get("ok", false)) and int(after.get("rally_until_tick", -1)) == rally_tick + 600 and is_equal_approx(float(after.get("rally_damage_mult", 0.0)), 1.5),
			str(rallied)
		)
	# Every locked power keeps a recorded reason (no invented effects).
	var locked_reasons_ok := true
	for other_id in sim.spellbook_power_ids():
		var other: Dictionary = sim.spellbook_power(other_id)
		if not bool(other.get("castable", false)) and String(other.get("locked_reason", "")) == "":
			locked_reasons_ok = false
	_check("%s_locked_powers_keep_recorded_reasons" % faction.to_lower(), locked_reasons_ok)


func _faction_spellbook_doc(mod_loader, pack_roots: Array, faction: String, fragment: String) -> Dictionary:
	for pack_root_value in pack_roots:
		var pack_root := String(pack_root_value)
		if not pack_root.contains(fragment):
			continue
		var pack_document := mod_loader._read_json(pack_root.path_join("pack.json")) as Dictionary
		var files: Dictionary = pack_document.get("files", {}) as Dictionary
		for key_value in files.keys():
			var key := String(key_value)
			if not key.begins_with("spellbook."):
				continue
			var relative := String(files.get(key, ""))
			if relative == "":
				continue
			var document := mod_loader._read_json(mod_loader.resolve_pack_path(pack_root, relative)) as Dictionary
			if document.is_empty() or String(document.get("schema", "")) != "openbfme.spellbook-runtime":
				continue
			if String((document.get("target", {}) as Dictionary).get("faction", "")) != faction:
				continue
			document["_pack_root"] = pack_root
			document["_pack_file_key"] = key
			return document
	return {}
func _last_power_event(sim, kind: String) -> Dictionary:
	var found: Dictionary = {}
	for event_value in sim.events:
		if typeof(event_value) != TYPE_DICTIONARY:
			continue
		if String((event_value as Dictionary).get("kind", "")) == kind:
			found = event_value
	return found


func _sum_ints(values: Array) -> int:
	var result := 0
	for value in values:
		result += int(value)
	return result


func _rules() -> Dictionary:
	return {
		"source_map_transform_scale": 0.01,
		"unit_rules": {
			SimScript.SOLDIER_OBJECT_ID: _unit_rule(SimScript.SOLDIER_HORDE_ID, "infantry", 5, 50),
			SimScript.ARCHER_OBJECT_ID: _unit_rule(SimScript.ARCHER_OBJECT_ID, "ranged-infantry", 5, 50),
			SimScript.TOWER_GUARD_OBJECT_ID: _unit_rule(SimScript.TOWER_GUARD_OBJECT_ID, "infantry", 5, 50),
			SimScript.KNIGHT_OBJECT_ID: _unit_rule(SimScript.KNIGHT_OBJECT_ID, "cavalry", 5, 50),
			SimScript.BUILDER_OBJECT_ID: _unit_rule(SimScript.BUILDER_OBJECT_ID, "", 1, 1, true),
		},
	}


func _unit_rule(horde_id: String, category: String, member_count: int, member_damage: int, is_builder: bool = false) -> Dictionary:
	var positions: Array[Vector3] = []
	for index in range(member_count):
		positions.append(Vector3(float(index), 0.0, 0.0))
	return {
		"horde_id": horde_id,
		"category": category,
		"is_builder": is_builder,
		"speed": 1.0,
		"speed_source": 10.0,
		"acceleration": 1.0,
		"acceleration_source": 10.0,
		"turn_rate_degrees_per_second": 180.0,
		"braking": 1.0,
		"braking_source": 10.0,
		"attack_range": 1.15,
		"attack_range_source": 11.5,
		"minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0,
		"vision_range": 17.5,
		"vision_range_source": 175.0,
		"delay_between_shots_ms": 600.0,
		"pre_attack_delay_ms": 200.0,
		"firing_duration_ms": 200.0,
		"attack_period_ticks": 10,
		"pre_attack_ticks": 2,
		"firing_duration_ticks": 2,
		"member_health": 100,
		"member_damage": member_damage,
		"member_count": member_count,
		"formation_positions": positions,
		"provenance": {},
	}


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_SPELLBOOK PASS %s" % name)
	else:
		failed += 1
		print("RETAIL_SPELLBOOK FAIL %s %s" % [name, detail])


func _finish() -> void:
	print("RETAIL_SPELLBOOK_RESULT passed=%d failed=%d" % [passed, failed])
	quit()


func _men_spellbook_document(content_db) -> Dictionary:
	## The registry spellbook slot is last-pack-wins across mounted packs; the
	## Men gate needs the active pack's own Men spellbook document.
	var soldier_definition: Dictionary = content_db.get_bundle_object("bfme2.object.gondor-fighter")
	var pack_root := String(soldier_definition.get("_pack_root", ""))
	if pack_root == "":
		return {}
	var mod_loader = root.get_node_or_null("ModLoader")
	var pack_document := {}
	if mod_loader != null:
		pack_document = mod_loader._read_json(pack_root.path_join("pack.json")) as Dictionary
	var files: Dictionary = pack_document.get("files", {}) as Dictionary
	var keys: Array[String] = []
	for key_value in files.keys():
		if String(key_value).begins_with("spellbook."):
			keys.append(String(key_value))
	keys.sort()
	for key in keys:
		var relative := String(files.get(key, ""))
		if relative == "":
			continue
		var document := {}
		if mod_loader != null:
			document = mod_loader._read_json(mod_loader.resolve_pack_path(pack_root, relative)) as Dictionary
		if document.is_empty() or String(document.get("schema", "")) != "openbfme.spellbook-runtime":
			continue
		if String((document.get("target", {}) as Dictionary).get("faction", "")) == "Men":
			return document
	return content_db.get_spellbook_runtime() if content_db.has_method("get_spellbook_runtime") else {}
