extends SceneTree

## UPnP automatic port-forwarding contract (F2).
##
## The point of this module is HONESTY, so that is what is asserted. Most of
## the checks are on pure functions and hold on any machine. One check performs
## a REAL discovery attempt against whatever network this runs on; it does not
## assume a router is present or absent, and instead asserts the property that
## must hold either way: the attempt reaches a terminal state, that state is
## never "mapped" without an actual mapping behind it, and the outcome always
## carries a concrete stated reason. A silent or optimistic result fails.

const UpnpScript = preload("res://src/net/upnp_port_mapping.gd")
const FlyoutScript = preload("res://src/ui/multiplayer_flyout.gd")

## Bounded wait for the worker; discovery's own budget is 2s. A router that
## stops answering can sit on a SOAP call far longer, so exceeding this is a
## legitimate outcome the test handles rather than hangs on.
const ATTEMPT_TIMEOUT_MSEC := 8000

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_result_names()
	_test_explanations()
	_test_status_text_is_honest()
	_test_invalid_port_refused()
	await _test_real_attempt()
	await _test_flyout_surfaces_the_outcome()
	_finish()


func _test_result_names() -> void:
	var supported: bool = UpnpScript.upnp_supported()
	_check("upnp_support_matches_the_engine_build", supported == ClassDB.class_exists("UPNP"))
	if not supported:
		# A build without UPnP must still behave: every helper answers, and the
		# answer says so rather than pretending.
		_check("unsupported_build_is_named_not_guessed",
			UpnpScript.result_name(0) == "UPNP_UNSUPPORTED_BUILD" \
				and UpnpScript.is_environmental(0) \
				and UpnpScript.explain_result(0).contains("no UPnP support"))
		return
	_check("result_codes_map_to_engine_symbols",
		UpnpScript.result_name(0) == "UPNP_RESULT_SUCCESS" \
			and UpnpScript.result_name(26) == "UPNP_RESULT_NO_GATEWAY" \
			and UpnpScript.result_name(1) == "UPNP_RESULT_NOT_AUTHORIZED",
		"%s / %s / %s" % [UpnpScript.result_name(0), UpnpScript.result_name(26), UpnpScript.result_name(1)])
	# An unknown code must not be silently swallowed into a generic message.
	_check("unknown_result_code_is_still_reported_numerically",
		UpnpScript.result_name(9999) == "UPNP_RESULT_9999" \
			and UpnpScript.explain_result(9999).contains("UPNP_RESULT_9999"),
		UpnpScript.explain_result(9999))
	# "No router here" and "router said no" are different facts and must not be
	# collapsed: the first is an expected environment, the second is a refusal.
	_check("environment_and_refusal_are_distinguished",
		UpnpScript.is_environmental(26) and UpnpScript.is_environmental(27) \
			and not UpnpScript.is_environmental(1) \
			and not UpnpScript.is_environmental(13))


func _test_explanations() -> void:
	if not UpnpScript.upnp_supported():
		return
	# Every code must get a substantial explanation...
	var all_explained := true
	for code in [0, 1, 11, 13, 15, 16, 17, 23, 24, 26, 27]:
		var text: String = UpnpScript.explain_result(code)
		if text.strip_edges().is_empty() or text.length() < 12:
			all_explained = false
	# ...and codes with genuinely DIFFERENT causes must read differently. Codes
	# that mean the same thing to a player (NO_GATEWAY / NO_DEVICES) deliberately
	# share wording, so distinctness is asserted one representative per cause.
	var distinct_causes := true
	var seen: Dictionary = {}
	for code in [0, 1, 11, 13, 15, 16, 17, 26]:
		var text: String = UpnpScript.explain_result(code)
		if seen.has(text):
			distinct_causes = false
		seen[text] = true
	_check("every_common_result_has_its_own_concrete_explanation",
		all_explained and distinct_causes)
	# The two failures a player can actually act on must say what to do.
	_check("actionable_failures_tell_the_player_what_to_do",
		UpnpScript.explain_result(13).to_lower().contains("port") \
			and UpnpScript.explain_result(1).to_lower().contains("disabled"),
		"%s | %s" % [UpnpScript.explain_result(13), UpnpScript.explain_result(1)])


func _test_status_text_is_honest() -> void:
	var mapped: String = UpnpScript.status_text(UpnpScript.STATE_MAPPED, "203.0.113.7", 26015, "ok")
	var mapped_no_address: String = UpnpScript.status_text(UpnpScript.STATE_MAPPED, "", 26015, "Ask a player to look it up.")
	var unavailable: String = UpnpScript.status_text(UpnpScript.STATE_UNAVAILABLE, "", 0, "No UPnP-capable router answered.")
	var failed_text: String = UpnpScript.status_text(UpnpScript.STATE_FAILED, "", 0, "The router refused the request.")
	var idle: String = UpnpScript.status_text(UpnpScript.STATE_IDLE, "", 0, "")
	_check("mapped_status_shows_the_address_players_need",
		mapped.contains("203.0.113.7") and mapped.contains("26015"), mapped)
	# Mapped-but-no-WAN-address must not silently print an empty address.
	_check("mapped_without_an_address_says_so",
		not mapped_no_address.contains("(") and mapped_no_address.to_lower().contains("would not report") \
			and mapped_no_address.contains("Ask a player"), mapped_no_address)
	# The unavailable line must carry the reason AND reassure that the normal
	# LAN/Radmin path is unaffected — it is not a failure of the product.
	_check("unavailable_status_states_the_reason_and_the_fallback",
		unavailable.contains("No UPnP-capable router answered.") \
			and unavailable.contains("LAN and Radmin"), unavailable)
	_check("failed_status_states_the_reason_and_the_manual_route",
		failed_text.contains("The router refused the request.") \
			and failed_text.to_lower().contains("by hand"), failed_text)
	# No non-mapped state may ever read as a success.
	var never_claims_success := true
	for status in [UpnpScript.STATE_IDLE, UpnpScript.STATE_WORKING, UpnpScript.STATE_UNAVAILABLE, UpnpScript.STATE_FAILED]:
		var text: String = UpnpScript.status_text(status, "", 0, "reason.")
		if text.contains("forwarded -"):
			never_claims_success = false
	_check("only_a_real_mapping_reads_as_forwarded", never_claims_success and idle.contains("not attempted"))


func _test_invalid_port_refused() -> void:
	var upnp = UpnpScript.new()
	var started: bool = upnp.start(80)
	_check("out_of_range_port_refused_with_the_range_named",
		not started and upnp.state == UpnpScript.STATE_FAILED \
			and upnp.detail.contains("1024") and upnp.detail.contains("65535"),
		"%s / %s" % [upnp.state, upnp.detail])
	# Teardown is safe when nothing was ever mapped.
	upnp.release()
	upnp.close()
	_check("close_is_safe_when_nothing_was_mapped", upnp.state == UpnpScript.STATE_IDLE)


## A REAL attempt on this machine's actual network. Environment-independent:
## whatever the router does (or does not) do, the module must land in a
## terminal state with a stated reason and must not overclaim.
func _test_real_attempt() -> void:
	var upnp = UpnpScript.new()
	# GDScript lambdas capture by VALUE, so the signal payload is collected in a
	# reference type; a captured String would silently never be updated.
	var outcomes: Array = []
	upnp.finished.connect(func(state: String, _detail: String) -> void: outcomes.append(state))
	var started: bool = upnp.start(26015)
	if not started:
		# Only legitimate when the build genuinely has no UPnP module.
		_check("unsupported_build_reports_unavailable_immediately",
			not UpnpScript.upnp_supported() and upnp.state == UpnpScript.STATE_UNAVAILABLE \
				and upnp.detail != "",
			"%s / %s" % [upnp.state, upnp.detail])
		upnp.close()
		return
	_check("attempt_reports_itself_as_working_while_it_runs",
		upnp.state == UpnpScript.STATE_WORKING and upnp.is_busy())
	var deadline := Time.get_ticks_msec() + ATTEMPT_TIMEOUT_MSEC
	while upnp.is_busy() and Time.get_ticks_msec() < deadline:
		upnp.poll()
		await process_frame
	var terminal: Array[String] = [UpnpScript.STATE_MAPPED, UpnpScript.STATE_UNAVAILABLE, UpnpScript.STATE_FAILED]
	if upnp.is_busy():
		# The router never answered inside the budget. That is an environment
		# fact, not a test failure — but the module must still be telling the
		# truth about it rather than sitting on a stale or invented result.
		_check("unfinished_attempt_still_reads_as_in_progress",
			upnp.state == UpnpScript.STATE_WORKING and outcomes.is_empty() \
				and upnp.status_line().contains("asking your router"),
			upnp.status_line())
	else:
		_check("real_attempt_reaches_a_terminal_state_with_a_reason",
			terminal.has(upnp.state) and upnp.detail.strip_edges() != "" \
				and outcomes.size() == 1 and String(outcomes[0]) == upnp.state,
			"state=%s outcomes=%s detail=%s" % [upnp.state, str(outcomes), upnp.detail])
		# The honesty invariant: "mapped" is only ever claimed with a real
		# mapping, and any other outcome names the engine result behind it.
		var honest := true
		if upnp.state == UpnpScript.STATE_MAPPED:
			honest = upnp.external_port == 26015 and upnp.last_result_name == "UPNP_RESULT_SUCCESS"
		else:
			honest = upnp.external_address == "" and upnp.external_port == 0 \
				and upnp.last_result_name != "" and upnp.last_result_name != "UPNP_RESULT_SUCCESS"
		_check("outcome_never_overclaims", honest,
			"state=%s result=%s addr='%s' port=%d" % [
				upnp.state, upnp.last_result_name, upnp.external_address, upnp.external_port])
	_check("status_line_matches_the_pure_formatter",
		upnp.status_line() == UpnpScript.status_text(
			upnp.state, upnp.external_address, upnp.external_port, upnp.detail))
	# Teardown must return immediately even with a job still in flight: waiting
	# on a wedged router here is exactly what used to freeze the UI.
	var close_started := Time.get_ticks_msec()
	upnp.close()
	var close_elapsed := Time.get_ticks_msec() - close_started
	_check("close_never_blocks_on_an_unfinished_router_call", close_elapsed < 250,
		"close took %dms" % close_elapsed)
	_check("close_releases_any_mapping_and_returns_to_idle",
		upnp.state == UpnpScript.STATE_IDLE and not upnp.is_busy())


## The NETWORK flyout must own a UPnP module and must SHOW its outcome; a
## result that never reaches a label is the defect this feature exists to
## avoid.
func _test_flyout_surfaces_the_outcome() -> void:
	var packed: PackedScene = load("res://scenes/boot.tscn")
	var menu := packed.instantiate()
	root.add_child(menu)
	await process_frame
	await process_frame
	var flyout = menu.get_node_or_null("Center/MultiplayerFlyout")
	if flyout == null or flyout.upnp == null or flyout.upnp_label == null:
		_check("flyout_owns_a_upnp_module_and_a_label", false)
		menu.queue_free()
		await process_frame
		return
	_check("flyout_owns_a_upnp_module_and_a_label", true)
	_check("upnp_label_starts_in_the_not_attempted_state",
		flyout.upnp_label.text.contains("not attempted"), flyout.upnp_label.text)
	# Drive the module into a terminal state directly (no router required) and
	# confirm the label follows.
	flyout.upnp.state = UpnpScript.STATE_UNAVAILABLE
	flyout.upnp.detail = "No UPnP-capable router answered."
	flyout.upnp.finished.emit(UpnpScript.STATE_UNAVAILABLE, flyout.upnp.detail)
	_check("unavailable_outcome_is_shown_verbatim_in_the_flyout",
		flyout.upnp_label.text.contains("No UPnP-capable router answered.") \
			and flyout.upnp_label.text.contains("LAN and Radmin"),
		flyout.upnp_label.text)
	flyout.upnp.state = UpnpScript.STATE_MAPPED
	flyout.upnp.external_address = "203.0.113.7"
	flyout.upnp.external_port = 26015
	flyout.upnp.detail = "The router accepted the port forward."
	flyout.upnp.finished.emit(UpnpScript.STATE_MAPPED, flyout.upnp.detail)
	_check("mapped_outcome_shows_the_external_endpoint_in_the_flyout",
		flyout.upnp_label.text.contains("203.0.113.7:26015"), flyout.upnp_label.text)
	# The flyout also advertises the real seat capacity now.
	_check("flyout_advertises_eight_player_capacity", int(flyout.MAX_PLAYERS) == 8,
		str(flyout.MAX_PLAYERS))
	# Radmin stays plain text with no brand asset, unchanged by this feature.
	_check("radmin_naming_stays_plain_text",
		flyout.network_hint_label.text.contains("Radmin") \
			and not flyout.network_hint_label.text.contains("["),
		flyout.network_hint_label.text)
	# Fitting the UPnP line in meant compacting the flyout; absolute positioning
	# gives no error when a control runs off the panel, so assert it does not.
	var overflowing: Array[String] = []
	for child in flyout.get_children():
		if not (child is Control):
			continue
		var control := child as Control
		if control.position.x < 0.0 or control.position.y < 0.0 \
			or control.position.x + control.size.x > flyout.size.x \
			or control.position.y + control.size.y > flyout.size.y:
			overflowing.append("%s@%s+%s" % [control.name, str(control.position), str(control.size)])
	_check("every_flyout_control_fits_inside_the_panel", overflowing.is_empty(),
		"panel=%s %s" % [str(flyout.size), ", ".join(overflowing).substr(0, 260)])
	# The UPnP line must not sit on top of its neighbours either.
	_check("upnp_line_has_its_own_band",
		flyout.upnp_label.position.y >= flyout.network_hint_label.position.y + flyout.network_hint_label.size.y \
			and flyout.upnp_label.position.y + flyout.upnp_label.size.y <= flyout.join_address_edit.position.y,
		"hint=%.0f upnp=%.0f..%.0f join=%.0f" % [
			flyout.network_hint_label.position.y + flyout.network_hint_label.size.y,
			flyout.upnp_label.position.y,
			flyout.upnp_label.position.y + flyout.upnp_label.size.y,
			flyout.join_address_edit.position.y])
	menu.queue_free()
	await process_frame


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("NET_UPNP PASS %s" % name)
	else:
		failed += 1
		printerr("NET_UPNP FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	print("NET_UPNP_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
