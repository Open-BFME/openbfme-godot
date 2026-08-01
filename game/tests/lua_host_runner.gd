extends SceneTree

## Lua 4.0.1 VM - host interface, sandboxing and containment contract.
##
## This is the runner that matters to the action/condition dispatch tables: it
## pins the registration API they will bind ~474 game actions to, and proves
## the four containment properties a lockstep simulation depends on.
##
##   1. ISOLATION   - two sandboxes share no globals, no registered functions,
##                    no tag methods and no tag counter.
##   2. BUDGET      - a runaway script is stopped, loudly, and cannot catch its
##                    own budget error to keep running.
##   3. CONTAINMENT - a script error never crashes the host, and the sandbox
##                    stays usable afterwards.
##   4. DETERMINISM - identical sandboxes running identical scripts produce
##                    identical observable output, including table identities.

const LuaSandbox := preload("res://src/lua/lua_sandbox.gd")
const LuaValue := preload("res://src/lua/lua_value.gd")
const LuaTable := preload("res://src/lua/lua_table.gd")

var passed := 0
var failed := 0

## Scratch state the registered host functions in this runner write to.
var _calls: Array[String] = []
var _received: Array = []


## A plain host object, to prove a script can carry a reference it cannot open.
class HostHandle extends RefCounted:
	var label: String = ""
	var secret: int = 0

	func _init(p_label: String, p_secret: int) -> void:
		label = p_label
		secret = p_secret


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_registration_and_return_conversions()
	_test_host_calls_script()
	_test_calls_in_both_directions()
	_test_userdata_passthrough()
	_test_isolation()
	_test_step_budget()
	_test_error_containment()
	_test_determinism()
	_test_no_host_access()
	_test_resource_containment()
	_test_reentrancy()
	_finish()


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("LUA_HOST PASS %s" % label)
	else:
		failed += 1
		printerr("LUA_HOST FAIL %s%s" % [label, " (%s)" % detail if detail != "" else ""])


func _first(outcome: Dictionary) -> Variant:
	if not bool(outcome["ok"]):
		return "<error:%s>" % outcome["error"]
	var values: Array = outcome["values"]
	return values[0] if not values.is_empty() else null


# --- registration and the return-value contract ------------------------------

func _test_registration_and_return_conversions() -> void:
	var box := LuaSandbox.new("host")

	box.register_function("give_int", func(_args): return 7)
	box.register_function("give_float", func(_args): return 2.5)
	box.register_function("give_string", func(_args): return "text")
	box.register_function("give_true", func(_args): return true)
	box.register_function("give_false", func(_args): return false)
	box.register_function("give_nil", func(_args): return null)
	box.register_function("give_pair", func(_args): return [1, 2])
	box.register_function("give_nothing", func(_args): return [])
	box.register_function("give_dict", func(_args): return {"a": 1, "b": 2})
	box.register_function("give_array", func(_args): return [[10, 20]])
	# pass_sandbox form - a lambda that CAPTURED `box` would be reachable from
	# the globals table and close a reference cycle GDScript cannot collect.
	box.register_function("blow_up",
		func(sandbox, _args): return sandbox.error("host said no"), true)
	box.register_function("echo_count", func(args): return args.size())
	box.register_function("sum", func(args):
		var total := 0.0
		for value in args:
			total += float(value)
		return total)

	_check("int_return_becomes_a_number",
		_first(box.do_string("return give_int()", "c")) == 7.0)
	_check("float_return_survives",
		_first(box.do_string("return give_float()", "c")) == 2.5)
	_check("string_return_survives",
		_first(box.do_string("return give_string()", "c")) == "text")
	# Lua 4.0 has no booleans: true is 1, false is nil.
	_check("bool_true_becomes_one",
		_first(box.do_string("return give_true()", "c")) == 1.0)
	_check("bool_false_becomes_nil",
		_first(box.do_string("return give_false()", "c")) == null)
	_check("null_return_is_nil",
		_first(box.do_string("return give_nil()", "c")) == null)
	_check("array_return_is_multiple_values",
		_first(box.do_string("local a, b = give_pair() return b", "c")) == 2.0)
	_check("empty_array_return_is_no_values",
		_first(box.do_string("local a = give_nothing() if a then return 1 end return 0", "c"))
			== 0.0)
	_check("dictionary_return_becomes_a_table",
		_first(box.do_string("local t = give_dict() return t.a + t.b", "c")) == 3.0)
	# A nested Array becomes a 1-based table with an `n` field, the Lua 4.0
	# array convention.
	_check("nested_array_becomes_a_table_with_n",
		_first(box.do_string("local t = give_array() return t[1] + t[2] + t.n", "c"))
			== 32.0)

	var raised := box.do_string("blow_up()", "c")
	_check("host_function_can_raise_a_script_error",
		not bool(raised["ok"]) and String(raised["error"]).contains("host said no"),
		String(raised["error"]))
	var after := box.do_string("return 1", "c")
	_check("sandbox_survives_a_host_raised_error", bool(after["ok"]))

	_check("arguments_arrive_in_order",
		_first(box.do_string("return sum(1, 2, 3)", "c")) == 6.0)
	_check("argument_count_is_exact",
		_first(box.do_string("return echo_count(1, 2)", "c")) == 2.0)
	_check("absent_arguments_are_simply_not_there",
		_first(box.do_string("return echo_count()", "c")) == 0.0)

	# The pass_sandbox form, for handlers that need the environment.
	box.register_function("with_box", func(sandbox, args):
		return sandbox.name + ":" + str(args.size()), true)
	_check("pass_sandbox_hands_over_the_environment",
		_first(box.do_string("return with_box(1, 2)", "c")) == "host:2")

	# Bulk registration - the form a generated dispatch table uses.
	var bulk := LuaSandbox.new("bulk")
	bulk.register_functions({
		"ActionA": func(_args): return "a",
		"ActionB": func(_args): return "b",
	})
	_check("register_functions_binds_every_entry",
		_first(bulk.do_string("return ActionA() .. ActionB()", "c")) == "ab")
	_check("has_function_reports_registration",
		bulk.has_function("ActionA") and not bulk.has_function("ActionZ"))
	bulk.unregister_function("ActionA")
	_check("unregister_function_removes_it", not bulk.has_function("ActionA"))

	# Host-set globals and readback.
	box.set_global("difficulty", 3)
	box.set_global("flags", {"hard": true})
	_check("host_set_global_is_visible",
		_first(box.do_string("return difficulty", "c")) == 3.0)
	_check("host_set_dictionary_global_becomes_a_table",
		_first(box.do_string("return flags.hard", "c")) == 1.0)
	box.do_string("result = 'from script'", "c")
	_check("host_reads_a_script_written_global",
		box.get_global("result") == "from script")
	box.do_string("record = {} record.x = 5", "c")
	var as_host: Variant = box.get_global_as_host("record")
	_check("host_reads_a_table_as_a_dictionary",
		as_host is Dictionary and float(as_host["x"]) == 5.0, str(as_host))


# --- host calls into script --------------------------------------------------

func _test_host_calls_script() -> void:
	var box := LuaSandbox.new("hooks")
	var loaded := box.do_string(
		"function onCreate(name, hp) return name .. ':' .. hp end "
		+ "function noReturn() end", "hooks")
	_check("script_defining_hooks_loads", bool(loaded["ok"]), String(loaded["error"]))

	var called := box.call_function("onCreate", ["Legolas", 50])
	_check("call_function_passes_arguments_and_returns",
		bool(called["ok"]) and String(called["values"][0]) == "Legolas:50",
		str(called["values"]))
	var empty := box.call_function("noReturn")
	_check("call_function_handles_no_return_values",
		bool(empty["ok"]) and empty["values"].is_empty())
	var missing := box.call_function("neverDefined")
	_check("calling_a_missing_hook_is_a_clean_failure",
		not bool(missing["ok"]) and String(missing["error"]).contains("no script function"),
		String(missing["error"]))

	# compile() once, call many - no re-parsing per tick.
	var compiled := box.compile("return 6 * 7", "cached")
	_check("compile_returns_a_reusable_closure", bool(compiled["ok"]))
	var run_a := box.call_closure(compiled["closure"], [])
	var run_b := box.call_closure(compiled["closure"], [])
	_check("a_compiled_closure_runs_repeatedly",
		bool(run_a["ok"]) and bool(run_b["ok"])
			and float(run_a["values"][0]) == 42.0 and float(run_b["values"][0]) == 42.0)

	_check("result_record_reports_steps_used",
		int(run_a["steps"]) > 0, str(run_a["steps"]))


# --- script -> host -> script ------------------------------------------------

func _test_calls_in_both_directions() -> void:
	var box := LuaSandbox.new("both")
	_calls.clear()

	box.register_function("log", func(args):
		_calls.append(String(args[0]))
		return [])

	# A host function that calls BACK into script through the sandbox. It takes
	# the pass_sandbox form, so its signature is func(box, args).
	var apply_twice := func(sandbox, args):
		var fn: Variant = args[0]
		var value: Variant = args[1]
		var once: Array = sandbox.call_value(fn, [value])
		if sandbox.has_error():
			return []
		var twice: Array = sandbox.call_value(fn, [once[0]])
		if sandbox.has_error():
			return []
		return twice[0]
	box.register_function("apply_twice", apply_twice, true)

	var outcome := box.do_string("""
		log('starting')
		local result = apply_twice(function(x) return x * 3 end, 2)
		log('done')
		return result
	""", "both")
	_check("host_function_can_call_a_script_function",
		bool(outcome["ok"]) and float(outcome["values"][0]) == 18.0,
		"%s %s" % [str(outcome["error"]), str(outcome["values"])])
	_check("host_side_effects_happened_in_order",
		_calls == ["starting", "done"], str(_calls))

	# An error raised inside the script callback travels back out through the
	# host function rather than being swallowed.
	var propagated := box.do_string(
		"return apply_twice(function(x) error('callback failed') end, 1)", "both")
	_check("an_error_in_a_script_callback_propagates_through_the_host",
		not bool(propagated["ok"])
			and String(propagated["error"]).contains("callback failed"),
		String(propagated["error"]))


# --- userdata ----------------------------------------------------------------

func _test_userdata_passthrough() -> void:
	var box := LuaSandbox.new("udata")
	var handle := HostHandle.new("orc_01", 1234)
	_received.clear()

	box.register_function("get_handle", func(_args): return handle)
	box.register_function("handle_label", func(sandbox, args):
		var value: Variant = args[0]
		if not (value is LuaValue.Userdata):
			return sandbox.error("expected a userdata handle")
		return value.payload.label, true)
	box.register_function("keep", func(args):
		_received.append(args[0])
		return [])

	_check("host_object_reaches_script_as_userdata",
		_first(box.do_string("return type(get_handle())", "c")) == "userdata")
	_check("userdata_round_trips_back_to_the_host",
		_first(box.do_string("return handle_label(get_handle())", "c")) == "orc_01")
	box.do_string("keep(get_handle())", "c")
	_check("host_gets_the_same_object_back",
		_received.size() == 1 and _received[0] is LuaValue.Userdata
			and _received[0].payload == handle,
		str(_received))

	# The script can hold and compare it but cannot open it.
	_check("userdata_compares_by_identity",
		_first(box.do_string("local a = get_handle() local b = get_handle() "
			+ "if a == b then return 1 end return 0", "c")) == 1.0)
	var probed := box.do_string("return get_handle().secret", "c")
	_check("script_cannot_index_a_userdata_handle",
		not bool(probed["ok"]) and String(probed["error"]).contains("index a userdata value"),
		String(probed["error"]))


# --- isolation ---------------------------------------------------------------

func _test_isolation() -> void:
	var a := LuaSandbox.new("a")
	var b := LuaSandbox.new("b")

	a.do_string("shared = 'from A' function onlyInA() return 1 end", "a")
	b.do_string("shared = 'from B'", "b")

	_check("globals_do_not_leak_between_sandboxes",
		a.get_global("shared") == "from A" and b.get_global("shared") == "from B",
		"%s / %s" % [str(a.get_global("shared")), str(b.get_global("shared"))])
	_check("script_functions_do_not_leak_between_sandboxes",
		_first(b.do_string("if onlyInA then return 1 end return 0", "b")) == 0.0)

	a.register_function("SecretAction", func(_args): return 1)
	_check("registered_functions_do_not_leak_between_sandboxes",
		a.has_function("SecretAction") and not b.has_function("SecretAction"))
	var reached := b.do_string("return SecretAction()", "b")
	_check("calling_another_sandboxes_function_fails",
		not bool(reached["ok"]) and String(reached["error"]).contains("call a nil value"),
		String(reached["error"]))

	# Tag methods and the tag counter are per sandbox too.
	a.do_string("mytag = newtag() settagmethod(mytag, 'index', function() return 42 end)", "a")
	var tag_value: Variant = a.get_global("mytag")
	var leaked := b.do_string(
		"local t = {} settag(t, %d) return t.missing" % int(tag_value), "b")
	_check("tag_methods_do_not_leak_between_sandboxes",
		bool(leaked["ok"]) and leaked["values"][0] == null, str(leaked["values"]))
	# Each sandbox mints tags from its OWN counter, so two fresh sandboxes hand
	# out the same first tag - which is exactly why tags cannot be used to
	# reach across sandboxes.
	_check("fresh_sandboxes_mint_the_same_first_tag",
		float(_first(LuaSandbox.new("x").do_string("return newtag()", "x")))
			== float(_first(LuaSandbox.new("y").do_string("return newtag()", "y"))))

	# Tables created in one sandbox are not reachable from the other, and each
	# sandbox has its own PRNG stream position.
	_check("output_buffers_are_separate",
		a.output_lines().is_empty() and b.output_lines().is_empty())
	a.do_string("print('only A')", "a")
	_check("print_lands_only_in_its_own_sandbox",
		a.output_lines().size() == 1 and b.output_lines().is_empty())


# --- step budget -------------------------------------------------------------

func _test_step_budget() -> void:
	var box := LuaSandbox.new("budget")
	box.step_budget = 5000

	var runaway := box.do_string("while 1 do end", "spin")
	_check("an_infinite_loop_is_stopped",
		not bool(runaway["ok"]), "the loop must not run forever")
	_check("budget_exhaustion_is_its_own_error_kind",
		String(runaway["error_kind"]) == "budget", String(runaway["error_kind"]))
	_check("budget_error_is_identifiable_by_message",
		String(runaway["error"]).contains("LUA_STEP_BUDGET_EXCEEDED"),
		String(runaway["error"]))

	var runaway_for := box.do_string("local n = 0 for i = 1, 100000000 do n = n + 1 end", "spin")
	_check("a_long_numeric_for_is_stopped_too",
		not bool(runaway_for["ok"]) and String(runaway_for["error_kind"]) == "budget",
		String(runaway_for["error"]))

	# The budget is PER INVOCATION: a script that fits still runs after one
	# that did not.
	var fine := box.do_string("local n = 0 for i = 1, 10 do n = n + i end return n", "ok")
	_check("budget_resets_for_the_next_invocation",
		bool(fine["ok"]) and float(fine["values"][0]) == 55.0, String(fine["error"]))

	# A protected call must NOT be able to swallow the budget error, or a
	# runaway script could catch its own leash and carry on.
	var swallow := box.do_string("""
		function spin() while 1 do end end
		call(spin, {}, 'x')
		return 'kept going'
	""", "swallow")
	_check("a_protected_call_cannot_swallow_the_budget_error",
		not bool(swallow["ok"]) and String(swallow["error_kind"]) == "budget",
		"%s / %s" % [String(swallow["error_kind"]), str(swallow["values"])])

	# Ordinary errors ARE catchable - the exemption is specific to the budget.
	var caught := box.do_string("""
		function boom() error('ordinary') end
		call(boom, {}, 'x')
		return 'kept going'
	""", "catch")
	_check("an_ordinary_error_is_still_catchable",
		bool(caught["ok"]) and String(caught["values"][0]) == "kept going",
		String(caught["error"]))

	# Recursion is capped before GDScript's own stack gives out.
	var deep := LuaSandbox.new("deep")
	deep.max_call_depth = 40
	var overflow := deep.do_string("function f(n) return f(n + 1) end return f(1)", "deep")
	_check("call_depth_is_capped",
		not bool(overflow["ok"]) and String(overflow["error"]).contains("stack overflow"),
		String(overflow["error"]))

	# Pattern matching gets its own bound, so an exponential pattern cannot
	# stall a tick either.
	# Lua patterns cannot quantify a capture, so the classic regex bomb does
	# not apply - but stacked greedy wildcards with no possible match still
	# backtrack polynomially (here O(n^4) over 200 characters), which is more
	# than enough to stall a 60Hz tick.
	var pattern_box := LuaSandbox.new("pat")
	pattern_box.pattern_step_budget = 20000
	var exponential := pattern_box.do_string(
		"return strfind(strrep('a', 200), '.*.*.*.*b')", "pat")
	_check("a_backtracking_pattern_is_stopped",
		not bool(exponential["ok"])
			and String(exponential["error"]).contains("pattern"),
		String(exponential["error"]))
	# The same pattern budget must not interfere with ordinary matching.
	var ordinary := pattern_box.do_string("return strfind('hello world', 'wor')", "pat")
	_check("the_pattern_budget_leaves_normal_matching_alone",
		bool(ordinary["ok"]) and float(ordinary["values"][0]) == 7.0,
		String(ordinary["error"]))


# --- error containment -------------------------------------------------------

func _test_error_containment() -> void:
	var box := LuaSandbox.new("contain")
	var cases := [
		"error('explicit')",
		"return nil + 1",
		"local t = nil return t.x",
		"return (5)()",
		"return 'a' .. {}",
		"return {} < {}",
		"this is not lua at all",
		"return strsub()",
	]
	var all_contained := true
	var details := PackedStringArray()
	for source in cases:
		var outcome := box.do_string(source, "case")
		if bool(outcome["ok"]):
			all_contained = false
			details.append("ACCEPTED: %s" % source)
		elif String(outcome["error"]).is_empty():
			all_contained = false
			details.append("EMPTY MESSAGE: %s" % source)
	_check("every_failure_mode_is_contained_with_a_message",
		all_contained, ", ".join(details))

	var still_alive := box.do_string("return 'alive'", "after")
	_check("the_sandbox_still_runs_after_eight_failures",
		bool(still_alive["ok"]) and String(still_alive["values"][0]) == "alive")

	# Every error record carries chunk and line, not just prose.
	var located := box.do_string("\n\n\nerror('here')", "located")
	_check("errors_carry_chunk_and_line",
		int(located["error_line"]) == 4 and String(located["error_chunk"]) == "located"
			and String(located["error"]).begins_with("located:4:"),
		String(located["error"]))


# --- determinism -------------------------------------------------------------

func _test_determinism() -> void:
	# A script that exercises every observable ordering surface at once:
	# iteration order, table identity strings, number formatting and the PRNG.
	var source := """
		local out = ''
		local t = {}
		t.delta = 1 t.alpha = 2 t[3] = 3 t.charlie = 4
		for k, v in t do out = out .. tostring(k) .. '=' .. tostring(v) .. ';' end
		out = out .. tostring({}) .. ';'
		out = out .. tostring({}) .. ';'
		out = out .. format('%.14g|%.6f|%g', 1/3, 2/7, 1e-7) .. ';'
		randomseed(42)
		for i = 1, 4 do out = out .. random(1000) .. ',' end
		sorted = {5, 1, 4, 2, 3}
		sort(sorted)
		for i = 1, 5 do out = out .. sorted[i] end
		return out
	"""
	var runs := PackedStringArray()
	for _index in range(4):
		var outcome := LuaSandbox.new("det").do_string(source, "det")
		if not bool(outcome["ok"]):
			runs.append("<error:%s>" % outcome["error"])
		else:
			runs.append(String(outcome["values"][0]))
	var identical := true
	for value in runs:
		if value != runs[0]:
			identical = false
	_check("four_identical_sandboxes_produce_identical_output",
		identical and not runs[0].begins_with("<error"),
		str(runs))

	# tostring of a table must not embed a machine address - two peers would
	# disagree, and so would two runs in one process.
	var stringified := LuaSandbox.new("det2").do_string("return tostring({})", "d")
	var text := String(stringified["values"][0])
	_check("table_identity_string_has_no_address",
		text.begins_with("table: ") and not text.contains("0x")
			and text == "table: 00000001",
		text)

	# The step count is itself deterministic, which is what lets a host budget
	# a script by measuring it once.
	var steps := PackedInt32Array()
	for _index in range(3):
		steps.append(int(LuaSandbox.new("steps").do_string(source, "det")["steps"]))
	_check("step_counts_are_reproducible",
		steps[0] == steps[1] and steps[1] == steps[2], str(steps))


# --- the sandbox has no host access ------------------------------------------

func _test_no_host_access() -> void:
	var box := LuaSandbox.new("closed")
	var withheld := [
		["io", "no filesystem access"],
		["os", "would desync"],
		["dofile", "no filesystem access"],
		["clock", "wall-clock"],
		["date", "wall-clock"],
		["getenv", "environment access is withheld"],
		["execute", "process execution"],
		["exit", "must not be able to terminate the host"],
		["require", "no module search path"],
		["loadstring", "use dostring"],
	]
	var all_named := true
	var details := PackedStringArray()
	for entry in withheld:
		var outcome := box.do_string("return %s" % entry[0], "closed")
		if bool(outcome["ok"]):
			all_named = false
			details.append("%s was READABLE" % entry[0])
		elif not String(outcome["error"]).containsn(String(entry[1])):
			all_named = false
			details.append("%s: %s" % [entry[0], String(outcome["error"])])
	_check("withheld_host_facilities_explain_themselves",
		all_named, ", ".join(details))

	# Godot's own API surface is simply not present as globals.
	var engine_names := ["OS", "Engine", "FileAccess", "ResourceLoader",
		"ProjectSettings", "DirAccess", "JavaScriptBridge", "self", "load_file"]
	var all_absent := true
	var seen := PackedStringArray()
	for engine_name in engine_names:
		var outcome := box.do_string("return type(%s)" % engine_name, "closed")
		if not bool(outcome["ok"]) or String(outcome["values"][0]) != "nil":
			all_absent = false
			seen.append(engine_name)
	_check("godot_api_names_are_absent_from_the_environment",
		all_absent, ", ".join(seen))

	# A sandbox can be built with NO library at all, for a host that wants to
	# hand over an exact vocabulary and nothing else.
	var bare := LuaSandbox.new("bare", false)
	bare.register_function("Allowed", func(_args): return 1)
	var allowed := bare.do_string("return Allowed()", "b")
	var denied := bare.do_string("return strsub('abc', 1, 2)", "b")
	_check("a_library_less_sandbox_exposes_only_what_the_host_registered",
		bool(allowed["ok"]) and float(allowed["values"][0]) == 1.0
			and not bool(denied["ok"])
			and String(denied["error"]).contains("call a nil value"),
		"%s / %s" % [str(allowed["values"]), String(denied["error"])])


# --- review regressions: resource containment and re-entrancy ----------------
##
## The fidelity review cleared this VM of information escape - no filesystem,
## OS, clock or Godot reach - but found the sandbox escapable on RESOURCES and
## corruptible on RE-ENTRY. These pin both.

func _test_resource_containment() -> void:
	# THE `n` LOOPHOLE. getn() honours a table's `n` field, which the script
	# controls. Setting t.n = 1e9 costs one step but makes tremove/tinsert/
	# sort/call run billion-iteration loops entirely inside GDScript, where the
	# per-AST-node budget never sees them - a script could stall a lockstep
	# tick without ever exceeding its step count.
	var cases := {
		"tremove": "local t = {} t.n = 100000000 tremove(t)",
		"tinsert": "local t = {} t.n = 100000000 tinsert(t, 'x')",
		"sort": "local t = {} t.n = 100000000 sort(t)",
		"call": "local t = {} t.n = 100000000 call(function() end, t)",
		"foreachi": "local t = {} t.n = 100000000 foreachi(t, function() end)",
	}
	var all_stopped := true
	var details := PackedStringArray()
	for label in cases:
		var box := LuaSandbox.new("n_loophole")
		box.step_budget = 50000
		var outcome := box.do_string(String(cases[label]), "n")
		if bool(outcome["ok"]) or String(outcome["error_kind"]) != "budget":
			all_stopped = false
			details.append("%s -> ok=%s kind=%s" % [label, str(outcome["ok"]),
				String(outcome["error_kind"])])
	_check("host_side_loops_cannot_outrun_the_step_budget",
		all_stopped, ", ".join(details))

	# ...and honest table work still runs.
	var honest := LuaSandbox.new("honest")
	var fine := honest.do_string(
		"local t = {} for i = 1, 50 do tinsert(t, i) end sort(t) return getn(t)", "h")
	_check("ordinary_table_work_is_unaffected_by_the_charge",
		bool(fine["ok"]) and float(fine["values"][0]) == 50.0, String(fine["error"]))


func _test_reentrancy() -> void:
	# A host function that runs more script is the obvious way to implement an
	# "execute script" game action. Before this fix every nested entry reset
	# _steps and cleared _frames, so the nested call corrupted the frame stack
	# it was unwinding into AND handed itself a brand new budget - a script
	# could loop "call a host action that runs a script" forever.
	var box := LuaSandbox.new("reentry")
	box.step_budget = 40000
	box.register_function("run_script", func(sandbox, args):
		var nested: Dictionary = sandbox.do_string(String(args[0]), "nested")
		return 1 if bool(nested["ok"]) else 0, true)

	var nested_ok := box.do_string("marker = 0 run_script('marker = 7') return marker", "outer")
	_check("a_host_function_can_run_nested_script",
		bool(nested_ok["ok"]) and float(nested_ok["values"][0]) == 7.0,
		"%s %s" % [String(nested_ok["error"]), str(nested_ok["values"])])

	# The outer frame stack survives the nested run: execution continues in the
	# right place and the outer chunk still returns its own value.
	var survives := box.do_string("""
		function outer()
			local before = 5
			run_script('return 1')
			return before + 1
		end
		return outer()
	""", "outer")
	_check("the_outer_frame_survives_a_nested_run",
		bool(survives["ok"]) and float(survives["values"][0]) == 6.0,
		"%s %s" % [String(survives["error"]), str(survives["values"])])

	# THE BUDGET IS SHARED. Measured against the work itself rather than
	# against a number this VM happened to produce: run one unit of work and
	# note the cost, set a budget that comfortably fits ONE unit, then run two
	# units where the first is nested. If nesting reset the leash the second
	# unit would fit too; because the budget is shared, it does not.
	const UNIT := "local s = 0 for i = 1, 500 do s = s + i end"
	var meter := LuaSandbox.new("meter")
	meter.register_function("run_script", func(sandbox, args):
		var nested: Dictionary = sandbox.do_string(String(args[0]), "nested")
		return 1 if bool(nested["ok"]) else 0, true)
	var one_unit := meter.do_string(UNIT, "one")
	_check("one_unit_of_work_succeeds_and_reports_a_cost",
		bool(one_unit["ok"]) and int(one_unit["steps"]) > 0, str(one_unit["steps"]))

	meter.step_budget = int(one_unit["steps"]) * 2
	var two_units := meter.do_string(
		"run_script([[%s]]) %s" % [UNIT, UNIT], "two")
	_check("nested_runs_share_the_budget_instead_of_resetting_it",
		not bool(two_units["ok"]) and String(two_units["error_kind"]) == "budget",
		"two units of work (one nested) must not fit a budget sized for one: "
			+ "%s / %s" % [str(two_units["ok"]), String(two_units["error_kind"])])

	# is_running() lets a host function tell script-initiated calls from
	# host-initiated ones.
	var flags: Array = []
	var probe := LuaSandbox.new("probe")
	probe.register_function("note", func(sandbox, _args):
		flags.append(sandbox.is_running())
		return [], true)
	probe.do_string("note()", "p")
	_check("is_running_reports_script_context",
		flags.size() == 1 and bool(flags[0]), str(flags))


func _finish() -> void:
	print("LUA_HOST_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
