extends SceneTree

## Proof that the importer's ability-FX ingress lane reaches the presentation.
##
## Fixture documents are verbatim reductions of the `fxBindings` block the
## importer seals into every converted playableUnit / spellbook runtime
## document (schema `openbfme.ability-fx-bindings`), carrying the retail
## values for FX_TelekinesisAtBone -> FX_Telekinesis -> GandalfWaveBlastWave
## (data/ini/fxparticlesystem.ini:30668).

const AbilityFxControllerScript = preload("res://src/retail_slice/retail_ability_fx_controller.gd")

var passed := 0
var failed := 0


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_ABILITY_FX_BINDING_RUNNER")
	call_deferred("_run")


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		return
	failed += 1
	printerr("FAIL %s%s" % [label, "" if detail == "" else ": " + detail])


func _wizard_blast_bindings() -> Dictionary:
	return {
		"schema": "openbfme.ability-fx-bindings",
		"schemaVersion": 0,
		"authoredFxListIds": ["FX_TelekinesisAtBone"],
		"fxLists": [
			{
				"fxListId": "FX_TelekinesisAtBone",
				"particleSystemIds": [],
				"nestedFxListIds": ["FX_Telekinesis"],
				"audioEventIds": ["GandalfWizardBlast"],
				"resolvedParticleSystemIds": ["GandalfWaveBlastProxy", "GandalfWaveBlastWave"],
			},
		],
		"definitionRegistry": [
			{
				"kind": "FXParticleSystem",
				"definitionId": "GandalfWaveBlastProxy",
				"definitionResourceId": "fx-gondorgandalf-def-fxparticlesystem-gandalfwaveblastproxy",
				"textureResourceIds": ["fx-gondorgandalf-tex-excloud01"],
				"authoredScalars": {"color1": "R:255 G:255 B:255 0", "lifetime": "35 35"},
			},
			{
				"kind": "FXParticleSystem",
				"definitionId": "GandalfWaveBlastWave",
				"definitionResourceId": "fx-gondorgandalf-def-fxparticlesystem-gandalfwaveblastwave",
				"textureResourceIds": ["fx-gondorgandalf-tex-exshockwavvtight"],
				"authoredScalars": {
					"isgroundaligned": "Yes",
					"color1": "R:82 G:139 B:235 0",
					"sizerate": "5 10",
				},
			},
		],
		"textures": [],
		"presentableFxListIds": ["FX_TelekinesisAtBone"],
		"unresolved": [],
	}


func _sound_only_bindings() -> Dictionary:
	return {
		"schema": "openbfme.ability-fx-bindings",
		"schemaVersion": 0,
		"authoredFxListIds": ["FX_EomerSpearThrow"],
		"fxLists": [
			{
				"fxListId": "FX_EomerSpearThrow",
				"particleSystemIds": [],
				"nestedFxListIds": [],
				"audioEventIds": ["EomerSpearFly"],
				"resolvedParticleSystemIds": [],
			},
		],
		"definitionRegistry": [],
		"textures": [],
		"presentableFxListIds": [],
		"unresolved": [],
	}


func _run() -> void:
	var unit_document := {"registration": {"fxBindings": _wizard_blast_bindings()}}
	var spellbook_document := {
		"registration": {"presentation": {"fxBindings": _sound_only_bindings()}}
	}
	var registry: Dictionary = AbilityFxControllerScript.collect_fx_registry(
		[unit_document, spellbook_document, {"registration": {}}, "not-a-document"]
	)

	_check("unit fxBindings register", registry.has("FX_TelekinesisAtBone"))
	_check(
		"sound-only FXList never registers",
		not registry.has("FX_EomerSpearThrow"),
		"a list with no converted particle system must stay unresolved"
	)

	var wizard_blast: Dictionary = registry.get("FX_TelekinesisAtBone", {})
	_check("authored ground alignment survives", bool(wizard_blast.get("ground_aligned", false)))
	_check("authored colour is claimed", bool(wizard_blast.get("has_authored_color", false)))
	var color: Color = wizard_blast.get("color", Color.BLACK)
	_check(
		"authored Color1 R:82 G:139 B:235 converts exactly",
		(
			absf(color.r - 82.0 / 255.0) < 0.0001
			and absf(color.g - 139.0 / 255.0) < 0.0001
			and absf(color.b - 235.0 / 255.0) < 0.0001
		),
		str(color)
	)

	# A cast whose FXList converted is presented with the authored evidence and
	# leaves the unresolved ledger clean.
	var controller = AbilityFxControllerScript.new()
	controller.configure(Callable(), registry)
	controller.present_ability_cast({
		"point": [10.0, 20.0],
		"ability_id": "Command_SpecialAbilityWizardBlast",
		"effect_kind": "weapon-blast",
		"fx_lists": ["FX_TelekinesisAtBone"],
		"fx_radius": 12.0,
	})
	_check("resolved cast is presented", controller.cues_presented == 1)
	_check("resolved cast records no gap", controller.unresolved_fx_list_ids.is_empty())
	var record: Dictionary = controller.cue_log[0]
	_check("resolved cast is flagged resolved", bool(record.get("fx_resolved", false)))
	_check("resolved cast is a ground-aligned shockwave", String(record.get("family", "")) == "shockwave")
	_check("resolved cast carries the authored colour", record.has("authored_color"))
	controller.free()

	# An unconverted id keeps the neutral cue and is still reported.
	var bare = AbilityFxControllerScript.new()
	bare.configure(Callable(), {})
	bare.present_ability_cast({
		"point": [1.0, 2.0],
		"ability_id": "Command_SpecialAbilityWizardBlast",
		"effect_kind": "weapon-blast",
		"fx_lists": ["FX_TelekinesisAtBone"],
		"fx_radius": 12.0,
	})
	_check("unconverted id lands in the ledger", bare.unresolved_fx_list_ids == ["FX_TelekinesisAtBone"])
	_check("unconverted cast invents no colour", not (bare.cue_log[0] as Dictionary).has("authored_color"))
	bare.free()

	_run_caster_animation_checks()

	print("ability-fx bindings: %d passed, %d failed" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _gandalf_ability_document() -> Dictionary:
	## Verbatim reduction of the converted GondorGandalf runtime document's
	## `registration.abilities` rows. `specialWeaponSlot` is the compiler's seal
	## of `WeaponFireSpecialAbilityUpdate.WhichSpecialWeapon`
	## (gandalf.ini:1300 gives SpecialAbilityWizardBlast slot 2, :1277 gives
	## SpecialAbilityWordOfPower slot 1, :1241 gives SpecialAbilityIstariLight
	## slot 3); `unpackingVariation` seals `ArrowStormUpdate.UnpackingVariation`
	## (gandalf.ini:1330 gives SpecialAbilityLightningSword variation 1).
	return {
		"objectId": "GondorGandalf",
		"registration": {
			"abilities": [
				{
					"id": "Command_SpecialAbilityWizardBlast",
					"specialPowerId": "SpecialAbilityWizardBlast",
					"effect": {"kind": "weapon-blast", "specialWeaponSlot": 2},
				},
				{
					"id": "Command_SpecialAbilityWordOfPower",
					"specialPowerId": "SpecialAbilityWordOfPower",
					"effect": {"kind": "weapon-blast", "specialWeaponSlot": 1},
				},
				{
					"id": "Command_GondorGandalfIstariLight",
					"specialPowerId": "SpecialAbilityIstariLight",
					"effect": {"kind": "weapon-blast", "specialWeaponSlot": 3},
				},
				{
					"id": "Command_GondorGandalfLightningSword",
					"specialPowerId": "SpecialAbilityLightningSword",
					"effect": {"kind": "arrow-storm", "unpackingVariation": 1},
				},
				{
					"id": "Command_GandalfShadowfax",
					"specialPowerId": "SpecialAbilityToggleMounted",
					"effect": {"kind": "mount-toggle", "unpackMs": 2000, "packMs": 2000},
				},
			]
		},
	}


func _run_caster_animation_checks() -> void:
	var registry: Dictionary = AbilityFxControllerScript.collect_ability_animation_registry(
		[_gandalf_ability_document(), {"registration": {}}, "not-a-document"]
	)
	_check(
		"WhichSpecialWeapon 2 selects SPECIAL_WEAPON_TWO",
		String(registry.get("specialabilitywizardblast", "")) == "specialWeaponTwo",
		str(registry)
	)
	_check(
		"WhichSpecialWeapon 1 and 3 select ONE and THREE",
		String(registry.get("specialabilitywordofpower", "")) == "specialWeaponOne"
		and String(registry.get("specialabilityistarilight", "")) == "specialWeaponThree",
		str(registry)
	)
	_check(
		"UnpackingVariation 1 selects PACKING_TYPE_1",
		String(registry.get("specialabilitylightningsword", "")) == "packingType1",
		str(registry)
	)
	_check(
		"an ability with neither number invents no pose",
		not registry.has("specialabilitytogglemounted"),
		str(registry)
	)

	var controller = AbilityFxControllerScript.new()
	controller.configure(Callable(), {}, registry)
	var states: Array[String] = controller.resolve_cast_animation_states({
		"special_power_id": "SpecialAbilityWizardBlast",
		"ability_id": "Command_SpecialAbilityWizardBlast",
	})
	# gandalf.ini:305 binds SPECIAL_WEAPON_TWO to GUGandalfG_SKL.GUGandalfG_SPCL
	# — the staff-lowering wizard blast. The fire frame is preferred, with the
	# envelope phases as fallbacks for abilities that only author part of it.
	_check(
		"wizard blast resolves its own capability state",
		states == [
			"ability:specialWeaponTwo:cast",
			"ability:specialWeaponTwo:unpack",
			"ability:specialWeaponTwo:prepare",
		],
		str(states)
	)
	_check(
		"an unjoined power requests no pose",
		controller.resolve_cast_animation_states({
			"special_power_id": "SpecialAbilityToggleMounted"
		}).is_empty()
	)
	controller.free()
