extends SceneTree
## Unit-level pin for the roster-audio closure rule that decides whether an
## UNBOUND voice refuses the launch or degrades the hero to silence.
##
## The regression this exists for: a republished faction pack added a `voice`
## block to `cah.system` (retail hero voice sets, named BY REFERENCE), while the
## mounted audio registry - which lives in a different pack - defines none of
## those events. Roster closure treated "authored but unbound" as a hard refusal
## and took Men and Dwarves offline at the end of a successful boot.
##
## The rule under test is deliberately narrow, so all three shapes are pinned
## here: the created hero the registry cannot answer (degrade), a created hero
## whose event the registry DOES define but cannot deliver (still refuse), and a
## converted pack unit with the same unanswerable event (still refuse).

const AudioScript = preload("res://src/retail_slice/retail_slice_audio.gd")
const PlayableUnitAdapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")

var passed := 0
var failed := 0

const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	# Stall tracking armed, plus the (passed, failed) provider, because THIS lane
	# is the one that watched retail_music_runner idle on a silently aborted
	# coroutine. The provider contract is `func() -> Vector2i` - a String there
	# crashes the abort path itself, which a prior lane shipped.
	_runner_watchdog.start(self, "CREATED_HERO_VOICE_CLOSURE_RUNNER", 0, 0, true)
	_runner_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	var content_db := root.get_node_or_null("ContentDB")
	if not _check("autoloads_available", content_db != null, "ContentDB autoload missing"):
		_finish()
		return
	content_db.reload()
	var host_root := _host_pack_root(content_db)
	if not _check("host_pack_root_resolves", host_root != "", "no mounted pack can host the slice"):
		_finish()
		return

	# An event id no converted pack defines. Held as a constant so the two
	# "still refuses" cases below use the SAME unanswerable id as the degrading
	# case - the only difference between them is the document that authored it.
	var unanswerable := "OpenBfmeTestVoiceNoMountedPackDefinesThis"
	_check(
		"fixture_event_is_genuinely_absent_from_the_mounted_registry",
		content_db.get_retail_audio_event(unanswerable).is_empty()
			and content_db.get_retail_audio_multisound(unanswerable).is_empty(),
		unanswerable
	)

	# 1. THE REGRESSION. A created hero whose authored voice the mounted registry
	#    cannot answer at all: named degradation, launch still closes.
	var hero_document := _created_hero_document("test.created-hero", unanswerable)
	# The adapter derives the runtime object id from the document, exactly as the
	# slice does; asserting against a hand-written id would pin the slug rule.
	var hero_id := PlayableUnitAdapter.runtime_member_id(hero_document)
	var hero_audio = _configure(host_root, {"test.created-hero": hero_document})
	_check(
		"created_hero_with_unanswerable_voice_does_not_block_closure",
		hero_audio.readiness_diagnostics().filter(func(row): return String(row).begins_with("missing_voice_event")).is_empty(),
		str(hero_audio.readiness_diagnostics())
	)
	_check(
		"created_hero_with_unanswerable_voice_is_reported_by_name",
		hero_audio.unvoiced_roster_degradations.has(
			"unvoiced_created_hero:%s:select:%s" % [hero_id, unanswerable]
		),
		str(hero_audio.unvoiced_roster_degradations)
	)
	_dispose(hero_audio)

	# 2. FAIL-CLOSED HALF. The SAME created hero, but its own document ships the
	#    binding contract for the event (an `authored-silent` resolution here):
	#    the document claims to answer it, so an unbound route is a broken pack.
	var declared := _created_hero_document("test.created-hero", unanswerable)
	((declared["registration"] as Dictionary)["audioBindings"] as Dictionary)[unanswerable] = []
	var declared_audio = _configure(host_root, {"test.created-hero": declared})
	_check(
		"created_hero_that_declares_its_own_binding_still_fails_closed",
		declared_audio.readiness_diagnostics().has(
			"missing_voice_event:%s:select:%s" % [hero_id, unanswerable]
		),
		str(declared_audio.readiness_diagnostics())
	)
	_dispose(declared_audio)

	# 3. FAIL-CLOSED HALF. A converted PACK unit (no createAHero marker) with the
	#    same unanswerable voice keeps refusing: the degradation is scoped to
	#    runtime-synthesized heroes, not to converted content.
	var pack_unit := _created_hero_document("test.pack-unit", unanswerable)
	(pack_unit["registration"] as Dictionary).erase("createAHero")
	var pack_unit_id := PlayableUnitAdapter.runtime_member_id(pack_unit)
	var pack_audio = _configure(host_root, {"test.pack-unit": pack_unit})
	_check(
		"converted_pack_unit_with_unanswerable_voice_still_fails_closed",
		pack_audio.readiness_diagnostics().has(
			"missing_voice_event:%s:select:%s" % [pack_unit_id, unanswerable]
		),
		str(pack_audio.readiness_diagnostics())
	)
	_check(
		"converted_pack_unit_is_never_reported_as_an_unvoiced_hero",
		pack_audio.unvoiced_roster_degradations.is_empty(),
		str(pack_audio.unvoiced_roster_degradations)
	)
	_dispose(pack_audio)

	# 4. FAIL-CLOSED HALF. A document carrying a FALSY `createAHero` marker is not
	#    a created hero. Pinned because `registration.has("createAHero")` would
	#    have handed the degradation to it on the strength of the key alone.
	var falsy := _created_hero_document("test.falsy-marker", unanswerable)
	(falsy["registration"] as Dictionary)["createAHero"] = false
	var falsy_id := PlayableUnitAdapter.runtime_member_id(falsy)
	var falsy_audio = _configure(host_root, {"test.falsy-marker": falsy})
	_check(
		"falsy_create_a_hero_marker_still_fails_closed",
		falsy_audio.readiness_diagnostics().has(
			"missing_voice_event:%s:select:%s" % [falsy_id, unanswerable]
		),
		str(falsy_audio.readiness_diagnostics())
	)
	_dispose(falsy_audio)

	# 5. FAIL-CLOSED HALF. Two documents claiming one id: a created hero and a
	#    converted unit. The merge keeps the STRICTER answer, so the shared id
	#    never inherits the hero's degradation from whichever document loaded
	#    last.
	var shared_hero := _created_hero_document("test.shared-id", unanswerable)
	var shared_unit := _created_hero_document("test.shared-id", unanswerable)
	(shared_unit["registration"] as Dictionary).erase("createAHero")
	var shared_id := PlayableUnitAdapter.runtime_member_id(shared_hero)
	var shared_audio = _configure(host_root, {
		"a.hero": shared_hero,
		"b.unit": shared_unit,
	})
	_check(
		"an_id_two_documents_claim_keeps_the_stricter_answer",
		shared_audio.readiness_diagnostics().has(
			"missing_voice_event:%s:select:%s" % [shared_id, unanswerable]
		)
			and shared_audio.unvoiced_roster_degradations.is_empty(),
		str(shared_audio.readiness_diagnostics()) + " " + str(shared_audio.unvoiced_roster_degradations)
	)
	_dispose(shared_audio)

	# 6. A created hero whose voice the registry DOES define keeps binding, so the
	#    rule never suppresses a voice the mounted content can actually speak.
	var answerable := _first_routable_event(host_root)
	if _check("mounted_registry_answers_at_least_one_event", answerable != "", "registry routed nothing"):
		var voiced_document := _created_hero_document("test.voiced-hero", answerable)
		var voiced_id := PlayableUnitAdapter.runtime_member_id(voiced_document)
		var bound_audio = _configure(host_root, {"test.voiced-hero": voiced_document})
		_check(
			"created_hero_with_an_answerable_voice_binds_it",
			bound_audio.roster_voice_routes.get(voiced_id, {}).has("select"),
			str(bound_audio.readiness_diagnostics()) + " " + str(bound_audio.unvoiced_roster_degradations)
		)
		_dispose(bound_audio)

	# 7. THE OWNER'S HERO. Every retail Create-a-Hero subclass names its voice
	#    set by reference (createaheroaudio.inc: HeroWestMale*, HeroWizard*,
	#    HeroDwarf*, ...). The mounted registry must DEFINE and ROUTE them, or
	#    every custom hero is mute - which is what the owner's v0.2.8 run.log
	#    showed (`unvoiced_created_hero:...:HeroWestMaleVoiceAttack`). The
	#    importer now cooks the class voice sets into every registry surface.
	for retail_event in ["HeroWestMaleVoiceSelectMS", "HeroWizardVoiceSelectMS", "HeroDwarfVoiceSelectMS"]:
		_check(
			"retail_cah_voice_event_is_defined:%s" % retail_event,
			not content_db.get_retail_audio_event(retail_event).is_empty()
				or not content_db.get_retail_audio_multisound(retail_event).is_empty(),
			"registry does not define it - CAH class voices not cooked into the mounted packs"
		)
	var owner_document := _created_hero_document("test.owner-hero", "HeroWestMaleVoiceSelectMS")
	var owner_id := PlayableUnitAdapter.runtime_member_id(owner_document)
	var owner_audio = _configure(host_root, {"test.owner-hero": owner_document})
	_check(
		"created_hero_with_a_retail_voice_set_is_voiced",
		owner_audio.roster_voice_routes.get(owner_id, {}).has("select")
			and owner_audio.unvoiced_roster_degradations.is_empty(),
		str(owner_audio.readiness_diagnostics()) + " " + str(owner_audio.unvoiced_roster_degradations)
			+ " route_failures=" + str(owner_audio.route_failures.get("herowestmalevoiceselectms", "<none>"))
			+ " sub=" + str(owner_audio.route_failures.get("herowestmalevoiceselect", "<none>"))
			+ " sub2=" + str(owner_audio.route_failures.get("herovoiceselect2", "<none>"))
	)
	_dispose(owner_audio)
	_finish()


func _configure(host_root: String, documents: Dictionary):
	var audio = AudioScript.new()
	root.add_child(audio)
	audio.configure(host_root, false, documents)
	return audio


func _dispose(audio) -> void:
	if audio.has_method("dispose"):
		audio.dispose()
	root.remove_child(audio)
	audio.free()


func _host_pack_root(content_db) -> String:
	var pack_capability = load("res://src/content/pack_capability.gd")
	return String(pack_capability.resolve_host_slice_pack(content_db.pack_meta).get("root", ""))


func _first_routable_event(host_root: String) -> String:
	## An event the mounted content can genuinely DELIVER, taken from a plain
	## configure rather than guessed from the registry: a definition with no
	## reachable samples would make case 4 assert the wrong thing.
	var probe = _configure(host_root, {})
	var chosen := ""
	var keys: Array = probe.audio_event_routes.keys()
	keys.sort()
	for key_value in keys:
		var route: Dictionary = probe.audio_event_routes[key_value] as Dictionary
		if Array(route.get("leaves", [])).is_empty():
			continue
		chosen = String(route.get("event_id", ""))
		break
	_dispose(probe)
	return chosen


func _created_hero_document(object_id: String, select_event: String) -> Dictionary:
	## The shape `cah_heroes.roster_document` emits: voice events named by
	## reference under `registration.audioRoutes.primaryMember`, a `createAHero`
	## marker, and no audio bindings of its own.
	return {
		"schema": "openbfme.playable-unit-runtime",
		"schemaVersion": 0,
		"objectId": object_id,
		"category": "hero",
		"registration": {
			"createAHero": {"heroId": "closure-fixture"},
			"audioBindings": {},
			"audioRoutes": {
				"container": {},
				"primaryMember": {"VoiceSelect": [{"id": select_event}]},
				"weapon": {},
			},
			"composition": {
				"containerObjectId": object_id,
				"primaryMemberObjectId": object_id,
				"members": [{"objectId": object_id, "count": 1}],
			},
		},
	}


func _check(name: String, ok: bool, detail: String) -> bool:
	_runner_watchdog.note(name)
	if ok:
		passed += 1
		print("PASS %s" % name)
	else:
		failed += 1
		print("FAIL %s :: %s" % [name, detail])
	return ok


func _finish() -> void:
	print("CREATED_HERO_VOICE_CLOSURE_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
