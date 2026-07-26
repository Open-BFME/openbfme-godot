extends SceneTree

## Lua 4.0.1 VM - core language contract.
##
## Covers the lexer, parser and evaluator: literals and coercion, Lua 4.0's
## own operator precedence (which is NOT 5.x's), control flow, functions,
## recursion, multiple returns, varargs through `arg`, %upvalue closures,
## tables, tag methods, and the loud refusal of every Lua 5.x construct.
##
## Sibling runners: lua_stdlib_runner.gd (flat library, patterns, format) and
## lua_host_runner.gd (sandboxing, registration, budget, determinism).

const LuaSandbox := preload("res://src/lua/lua_sandbox.gd")
const LuaValue := preload("res://src/lua/lua_value.gd")
const LuaTable := preload("res://src/lua/lua_table.gd")

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_literals_and_coercion()
	_test_precedence()
	_test_control_flow()
	_test_functions()
	_test_upvalues()
	_test_tables()
	_test_tag_methods()
	_test_error_model()
	_test_loud_refusals()
	_test_review_regressions()
	_finish()


# --- helpers -----------------------------------------------------------------

func _box() -> LuaSandbox:
	return LuaSandbox.new("test")


## Evaluates a case and returns its first Lua value, or the marker string
## "<error:...>" so a failing case can never silently read as nil.
##
## Cases are written both ways - as a bare expression ("2 + 2") and as a whole
## chunk that returns for itself ("local x = 1 return x"). We COMPILE-probe the
## "return <source>" form and fall back to running the source as written, so a
## case is never quietly reinterpreted: if neither form compiles the error is
## reported, and only one of the two can ever compile for a given case.
func _value(source: String) -> Variant:
	var box := _box()
	var probe := box.compile("return " + source, "expr")
	var outcome := box.call_closure(probe["closure"], []) if bool(probe["ok"]) \
		else box.do_string(source, "expr")
	if not bool(outcome["ok"]):
		return "<error:%s>" % outcome["error"]
	var values: Array = outcome["values"]
	return values[0] if not values.is_empty() else null


func _run_chunk(source: String) -> Dictionary:
	return _box().do_string(source, "chunk")


## Asserts a chunk fails, and that the message names the given fragment. The
## fragment check is the point: a generic failure would satisfy "it errored"
## while telling a modder nothing.
func _check_refused(label: String, source: String, fragment: String,
		kind: String = "") -> void:
	var outcome := _run_chunk(source)
	if bool(outcome["ok"]):
		failed += 1
		printerr("LUA_VM FAIL %s (chunk was ACCEPTED, expected refusal)" % label)
		return
	var message := String(outcome["error"])
	var kind_ok := kind == "" or String(outcome["error_kind"]) == kind
	if message.containsn(fragment) and kind_ok:
		passed += 1
		print("LUA_VM PASS %s" % label)
	else:
		failed += 1
		printerr("LUA_VM FAIL %s (message did not name '%s' / kind %s: %s [%s])"
			% [label, fragment, kind, message, outcome["error_kind"]])


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("LUA_VM PASS %s" % label)
	else:
		failed += 1
		printerr("LUA_VM FAIL %s%s" % [label, " (%s)" % detail if detail != "" else ""])


func _check_number(label: String, source: String, expected: float) -> void:
	var got: Variant = _value(source)
	_check(label, got is float and is_equal_approx(float(got), expected),
		"%s -> %s, wanted %f" % [source, str(got), expected])


func _check_string(label: String, source: String, expected: String) -> void:
	var got: Variant = _value(source)
	_check(label, got is String and String(got) == expected,
		"%s -> %s, wanted '%s'" % [source, str(got), expected])


## Lua 4.0 has no booleans: true is the number 1, false is nil.
func _check_true(label: String, source: String) -> void:
	var got: Variant = _value(source)
	_check(label, got is float and float(got) == 1.0,
		"%s -> %s, wanted the number 1" % [source, str(got)])


func _check_nil(label: String, source: String) -> void:
	var got: Variant = _value(source)
	_check(label, got == null, "%s -> %s, wanted nil" % [source, str(got)])


# --- literals and coercion ---------------------------------------------------

func _test_literals_and_coercion() -> void:
	_check_number("integer_literal", "42", 42.0)
	_check_number("float_literal", "3.5", 3.5)
	_check_number("exponent_literal", "314.16e-2", 3.1416)
	_check_number("leading_dot_literal", ".5", 0.5)
	_check_string("single_quoted_string", "'hi'", "hi")
	_check_string("escape_sequences", "\"a\\tb\\nc\"", "a\tb\nc")
	_check_string("decimal_escape", "\"\\65\\66\"", "AB")
	_check_string("long_string", "[[raw \\n not an escape]]", "raw \\n not an escape")
	# 4.0 KEEPS a newline that immediately follows [[ - see the
	# long_string_keeps_the_leading_newline regression below for the source
	# citation. Dropping it is Lua 5.0's rule.
	_check_string("long_string_keeps_first_newline", "[[\nline]]", "\nline")
	_check_string("nested_long_string", "[[a [[b]] c]]", "a [[b]] c")
	_check_nil("nil_literal", "nil")

	# Arithmetic coerces strings that look like numbers; concatenation coerces
	# numbers to strings. Equality does NOT coerce either way.
	_check_number("string_coerces_in_arithmetic", "'10' + 5", 15.0)
	_check_string("number_coerces_in_concat", "1 .. 2", "12")
	_check_nil("equality_does_not_coerce", "('0' == 0) ")
	_check_true("distinct_types_are_unequal", "'0' ~= 0")

	# tostring uses %.14g, so a whole number has no decimal point.
	_check_string("tostring_of_whole_number", "tostring(3)", "3")
	_check_string("tostring_trims_trailing_zeros", "tostring(3.14)", "3.14")
	_check_string("tostring_of_negative", "tostring(-0.5)", "-0.5")
	_check_string("tostring_uses_14_significant_digits",
		"tostring(1/3)", "0.33333333333333")
	_check_string("tostring_switches_to_exponent", "tostring(1e20)", "1e+20")

	# Truth is "not nil": 0 and "" are both TRUE in Lua.
	_check_true("zero_is_true", "if 0 then return 1 else return nil end")
	_check_true("empty_string_is_true", "if '' then return 1 else return nil end")
	_check_nil("not_zero_is_nil", "not 0")
	_check_true("not_nil_is_one", "not nil")


# --- operator precedence -----------------------------------------------------

func _test_precedence() -> void:
	_check_number("multiplication_binds_tighter", "2 + 3 * 4", 14.0)
	_check_number("unary_minus_below_power", "-2 ^ 2", -4.0)
	_check_number("power_is_right_associative", "2 ^ 3 ^ 2", 512.0)
	_check_number("unary_above_multiplication", "-2 * 3", -6.0)
	_check_string("concat_is_right_associative", "'a' .. 'b' .. 'c'", "abc")
	# '..' has priority 4 and '+' has 5, so this is 1 .. (2 + 3) - and the
	# result is the STRING "15", not the number 15.
	_check_string("concat_binds_looser_than_plus", "1 .. 2 + 3", "15")

	# THE Lua 4.0 quirk: `and` and `or` share priority 1 and are LEFT
	# associative, so this parses as (nil or 1) and 2 == 2. Lua 5.0 gave `and`
	# a higher priority, which would make it nil or (1 and 2) == 1.
	_check_number("and_or_share_priority_and_associate_left",
		"nil or 1 and 2", 2.0)
	_check_number("and_or_left_association_second_case",
		"1 and nil or 3", 3.0)

	# Comparison and equality also share priority 2 in 4.0, so this is
	# (1 < 2) == 1, which is true because `<` yields the number 1.
	_check_true("comparison_and_equality_share_priority", "1 < 2 == 1")

	# Relational operators yield the NUMBER 1 or nil, never a boolean.
	_check_true("less_than_yields_one", "1 < 2")
	_check_nil("failed_comparison_yields_nil", "2 < 1")
	_check_true("string_comparison", "'abc' < 'abd'")
	_check_true("greater_equal", "3 >= 3")
	_check_true("less_equal", "3 <= 3")

	# and/or return operands, not booleans.
	_check_number("and_returns_second_operand", "1 and 7", 7.0)
	_check_number("or_returns_first_truthy", "5 or 7", 5.0)
	_check_nil("and_short_circuits_on_nil", "nil and error('never runs')")

	# '^' works only because the math library installs a `pow` tag method on
	# the number tag - Lua 4.0's core has no built-in exponentiation.
	_check_number("power_through_pow_tag_method", "2 ^ 10", 1024.0)
	var bare := LuaSandbox.new("bare", false)
	var bare_outcome := bare.do_string("return 2 ^ 3", "bare")
	_check("power_fails_without_the_math_library",
		not bool(bare_outcome["ok"]),
		"a stdlib-less 4.0 state genuinely cannot evaluate '^': %s"
			% str(bare_outcome["error"]))


# --- control flow ------------------------------------------------------------

func _test_control_flow() -> void:
	_check_number("if_elseif_else", """
		local x = 5
		if x < 3 then return 1
		elseif x < 10 then return 2
		else return 3 end
	""", 2.0)

	_check_number("while_loop_sums",
		"local s = 0 local i = 1 while i <= 10 do s = s + i i = i + 1 end return s", 55.0)
	_check_number("repeat_until_runs_at_least_once",
		"local n = 0 repeat n = n + 1 until n >= 3 return n", 3.0)
	# lparser.c repeatstat: block(ls) then cond(ls, &v). block() runs
	# removelocalvars() on the way out, so the until-expression is compiled
	# AFTER the body's locals have left scope and `done` resolves to the global
	# (nil) - the loop never ends on its own. Lua 5.1 moved the condition
	# inside the body scope; asserting the 5.1 reading here would have made a
	# script that hangs on retail look correct.
	_check("repeat_condition_cannot_see_body_locals",
		String(_value("local n = 0 repeat local done = 1 n = n + 1 until done return n"))
			.contains("LUA_STEP_BUDGET_EXCEEDED"),
		"a body local must not satisfy the until-condition")
	# Reading a GLOBAL of the same name does terminate it, which is how 4.0
	# code has to be written.
	_check_number("repeat_condition_reads_a_global",
		"n = 0 repeat done = n >= 2 n = n + 1 until done return n", 3.0)
	_check_number("numeric_for", "local s = 0 for i = 1, 5 do s = s + i end return s", 15.0)
	_check_number("numeric_for_with_step",
		"local s = 0 for i = 1, 10, 3 do s = s + i end return s", 22.0)
	_check_number("numeric_for_counts_down",
		"local s = 0 for i = 3, 1, -1 do s = s * 10 + i end return s", 321.0)
	_check_number("numeric_for_control_variable_is_a_copy",
		"local n = 0 for i = 1, 3 do i = 99 n = n + 1 end return n", 3.0)
	_check_number("break_leaves_the_loop",
		"local s = 0 for i = 1, 10 do if i > 4 then break end s = s + i end return s", 10.0)
	_check_number("break_inside_while",
		"local i = 0 while 1 do i = i + 1 if i == 7 then break end end return i", 7.0)
	_check_number("generic_for_over_table",
		"local t = {10, 20, 30} local s = 0 for k, v in t do s = s + v end return s", 60.0)
	_check_number("nested_loops_and_break",
		"local n = 0 for i = 1, 3 do for j = 1, 3 do if j == 2 then break end n = n + 1 end end return n",
		3.0)
	_check_number("do_block_scopes_locals",
		"local x = 1 do local x = 2 end return x", 1.0)
	_check_refused("for_step_zero_is_named",
		"for i = 1, 10, 0 do end", "step is zero")


# --- functions ---------------------------------------------------------------

func _test_functions() -> void:
	_check_number("global_function_and_call",
		"function double(x) return x * 2 end return double(21)", 42.0)
	_check_number("recursion_factorial",
		"function fact(n) if n <= 1 then return 1 end return n * fact(n - 1) end return fact(10)",
		3628800.0)
	_check_number("mutual_recursion",
		"function odd(n) if n == 0 then return nil end return even(n - 1) end "
		+ "function even(n) if n == 0 then return 1 end return odd(n - 1) end "
		+ "if even(10) then return 1 else return 0 end", 1.0)
	_check_number("deep_recursion_fib",
		"function fib(n) if n < 2 then return n end return fib(n-1) + fib(n-2) end return fib(18)",
		2584.0)

	# Multiple returns: expanded in the last position, truncated elsewhere.
	_check_number("multiple_returns_second_value",
		"function two() return 1, 2 end local a, b = two() return b", 2.0)
	_check_number("call_truncated_when_not_last",
		"function two() return 1, 2 end local a, b = two(), 9 return b", 9.0)
	_check_number("parentheses_truncate_to_one_value",
		"function two() return 1, 2 end local a, b = (two()) if b then return 1 end return 0",
		0.0)
	_check_number("return_forwards_all_values",
		"function two() return 1, 2 end function fwd() return two() end local a, b = fwd() return b",
		2.0)
	_check_number("call_arguments_expand",
		"function two() return 3, 4 end function add(a, b) return a + b end return add(two())",
		7.0)
	_check_number("missing_arguments_are_nil",
		"function f(a, b) if b then return 1 end return 0 end return f(1)", 0.0)

	# Varargs: Lua 4.0 has no `...` expression, only the local table `arg`.
	_check_number("vararg_arg_count",
		"function f(...) return arg.n end return f(1, 2, 3)", 3.0)
	_check_number("vararg_arg_values",
		"function f(...) return arg[1] + arg[3] end return f(10, 20, 30)", 40.0)
	_check_number("vararg_after_named_parameters",
		"function f(a, ...) return a + arg.n end return f(5, 1, 1)", 7.0)

	# Method sugar.
	_check_number("method_definition_and_call",
		"t = {} t.v = 7 function t:get() return self.v end return t:get()", 7.0)
	_check_number("dotted_function_name",
		"a = {} a.b = {} function a.b.c() return 3 end return a.b.c()", 3.0)
	_check_number("function_as_value",
		"local f = function(x) return x + 1 end return f(1)", 2.0)
	_check_number("function_call_with_string_sugar",
		"function f(s) return strlen(s) end return f'abcd'", 4.0)
	_check_number("function_call_with_table_sugar",
		"function f(t) return t.x end return f{x = 9}", 9.0)

	# Locals really are local; a function body cannot see the caller's locals.
	_check_number("callee_cannot_see_caller_locals",
		"function probe() if hidden then return 1 end return 0 end "
		+ "function outer() local hidden = 5 return probe() end return outer()", 0.0)


# --- %upvalues ---------------------------------------------------------------

func _test_upvalues() -> void:
	_check_number("upvalue_captures_a_local",
		"function make() local n = 41 return function() return %n + 1 end end return make()()",
		42.0)

	# Capture is BY VALUE at closure creation. Lua 4.0 has no cells, so a later
	# assignment to the original local is invisible to the closure. This is the
	# single biggest behavioural difference from 5.x closures.
	_check_number("upvalue_is_frozen_at_creation", """
		function make()
			local n = 1
			local f = function() return %n end
			n = 99
			return f()
		end
		return make()
	""", 1.0)

	# The reference is frozen, not the referent: a captured table can still be
	# mutated, exactly as the 4.0 manual says.
	_check_number("captured_table_contents_stay_mutable", """
		function make()
			local t = {}
			t.v = 1
			local f = function() return %t.v end
			t.v = 5
			return f()
		end
		return make()
	""", 5.0)

	_check_number("upvalue_of_a_global",
		"g = 7 function make() return function() return %g end end g = 8 return make()()",
		8.0)
	_check_number("two_closures_capture_independently", """
		function make(x)
			return function() return %x end
		end
		local a = make(3)
		local b = make(4)
		return a() * 10 + b()
	""", 34.0)
	# An upvalue reaches exactly ONE level out - to a variable visible where the
	# closure is written. A grandparent's local is not visible there, so it
	# resolves to the global of that name (nil here). This is the constraint
	# that forces 4.0 code to hand values down level by level.
	_check_nil("upvalue_cannot_reach_a_grandparent_local", """
		function outer()
			local a = 2
			return function()
				return function() return %a end
			end
		end
		return outer()()()
	""")
	_check_number("upvalues_chain_one_level_at_a_time", """
		function outer()
			local a = 2
			return function()
				local b = %a * 3
				return function() return %b end
			end
		end
		return outer()()()
	""", 6.0)
	_check_number("upvalue_inside_a_loop_captures_each_iteration", """
		local fs = {}
		for i = 1, 3 do
			fs[i] = function() return %i end
		end
		return fs[1]() * 100 + fs[2]() * 10 + fs[3]()
	""", 123.0)


# --- tables ------------------------------------------------------------------

func _test_tables() -> void:
	_check_number("positional_constructor", "local t = {5, 6, 7} return t[2]", 6.0)
	_check_number("keyed_constructor", "local t = {x = 1, y = 2} return t.y", 2.0)
	_check_number("bracketed_key_constructor", "local t = {[10] = 'a', [20] = 'b'} return 1",
		1.0)
	_check_string("bracketed_key_value", "local t = {[10] = 'a'} return t[10]", "a")
	# Lua 4.0 separates the positional and keyed sections with ';', not ','.
	_check_number("semicolon_separated_sections",
		"local t = {1, 2; x = 3} return t[1] + t[2] + t.x", 6.0)
	_check_number("empty_constructor", "local t = {} return 1", 1.0)
	# listfields reaches every element through exp1(), i.e. luaK_tostack(..., 1),
	# so the last positional field is closed to ONE value. Multret inside a
	# constructor is Lua 5.0.
	_check_number("constructor_closes_last_call_to_one_value",
		"function two() return 8, 9 end local t = {two()} return t[1]", 8.0)
	_check_nil("constructor_does_not_expand_multiple_returns",
		"function two() return 8, 9 end local t = {two()} return t[2]")
	# Call arguments and return statements DO still expand - the restriction is
	# specific to constructors.
	_check_number("call_arguments_still_expand",
		"function two() return 8, 9 end function add(a, b) return a + b end return add(two())",
		17.0)
	_check_number("nested_tables", "local t = {a = {b = {c = 4}}} return t.a.b.c", 4.0)
	_check_number("assignment_creates_fields",
		"local t = {} t.x = 1 t['y'] = 2 t[3] = 3 return t.x + t.y + t[3]", 6.0)
	_check_nil("missing_field_is_nil", "local t = {} return t.nothing")
	_check_number("integer_and_float_keys_are_one_key",
		"local t = {} t[1] = 5 t[1.0] = 6 return t[1]", 6.0)
	_check_number("assigning_nil_removes_a_key",
		"local t = {a = 1} t.a = nil local n = 0 for k, v in t do n = n + 1 end return n",
		0.0)
	_check_number("tables_compare_by_identity",
		"local a = {} local b = {} if a == b then return 1 end if a == a then return 2 end return 0",
		2.0)
	_check_number("multiple_assignment_swaps",
		"local a, b = 1, 2 a, b = b, a return a * 10 + b", 21.0)
	_check_refused("nil_table_index_is_named", "local t = {} t[nil] = 1", "index is nil")
	_check_refused("nan_table_index_is_named",
		"local t = {} t[0/0] = 1", "NaN")
	_check_refused("indexing_nil_is_named", "local t = nil return t.x", "index a nil value")
	_check_refused("calling_a_number_is_named", "local n = 5 return n()", "call a number value")


# --- tag methods -------------------------------------------------------------

func _test_tag_methods() -> void:
	# `index` fires only on a MISS, never for a field that is present.
	_check_number("index_tag_method_on_miss", """
		local t = {}
		local mytag = newtag()
		settag(t, mytag)
		settagmethod(mytag, 'index', function(tbl, key) return 99 end)
		t.present = 1
		return t.present * 1000 + t.absent
	""", 1099.0)

	_check_number("arithmetic_tag_methods", """
		local mytag = newtag()
		settagmethod(mytag, 'add', function(a, b) return a.v + b.v end)
		settagmethod(mytag, 'sub', function(a, b) return a.v - b.v end)
		settagmethod(mytag, 'mul', function(a, b) return a.v * b.v end)
		local x = {v = 10} settag(x, mytag)
		local y = {v = 3}  settag(y, mytag)
		return (x + y) * 100 + (x - y) * 10 + (x * y) / 10
	""", 1373.0)

	_check_number("unm_tag_method", """
		local mytag = newtag()
		settagmethod(mytag, 'unm', function(a) return -a.v end)
		local x = {v = 8} settag(x, mytag)
		return -x
	""", -8.0)

	_check_string("concat_tag_method", """
		local mytag = newtag()
		settagmethod(mytag, 'concat', function(a, b) return 'joined' end)
		local x = {} settag(x, mytag)
		return x .. 'tail'
	""", "joined")

	# 4.0 passes the EVENT NAME as a third argument to every binary tag method.
	# lvm.c's call_binTM pushes luaT_eventname[event] unconditionally, and the
	# manual's 4.8 pseudo-code is tm(op1, op2, "concat") / tm(op1, op2, "lt").
	# These two events were missing it while add/sub/mul/unm already had it, so
	# a handler shared across events could not tell which one fired.
	_check_string("concat_tag_method_receives_the_event_name", """
		local mytag = newtag()
		settagmethod(mytag, 'concat', function(a, b, event) return event end)
		local x = {} settag(x, mytag)
		return x .. 'tail'
	""", "concat")

	_check_string("lt_tag_method_receives_the_event_name", """
		local mytag = newtag()
		-- `seen` is deliberately GLOBAL: Lua 4.0 has no lexical closures over
		-- locals, so a local here would not be visible inside the handler.
		seen = 'none'
		settagmethod(mytag, 'lt', function(a, b, event) seen = event return 1 end)
		local x = {} settag(x, mytag)
		local y = {} settag(y, mytag)
		local ignored = x < y
		return seen
	""", "lt")

	# All four ordering operators are rewritten in terms of `lt` before reaching
	# the handler, so the event name is "lt" even for `>`. That is 4.0 behaviour,
	# not an artefact of the rewrite.
	_check_string("greater_than_still_reports_lt", """
		local mytag = newtag()
		-- `seen` is deliberately GLOBAL: Lua 4.0 has no lexical closures over
		-- locals, so a local here would not be visible inside the handler.
		seen = 'none'
		settagmethod(mytag, 'lt', function(a, b, event) seen = event return 1 end)
		local x = {} settag(x, mytag)
		local y = {} settag(y, mytag)
		local ignored = x > y
		return seen
	""", "lt")

	# 4.0's grammar allows the two constructor halves in EITHER order:
	#   fieldlist -> lfieldlist | ffieldlist
	#              | lfieldlist ';' ffieldlist | ffieldlist ';' lfieldlist
	# lparser.c's constructor() requires only that the two parts differ in kind.
	_check_number("constructor_keyed_then_positional", """
		local t = {x = 1; 10, 20}
		return t.x * 1000 + t[1] * 10 + t[2]
	""", 1120.0)

	_check_number("constructor_positional_then_keyed_still_works", """
		local t = {10, 20; x = 1}
		return t.x * 1000 + t[1] * 10 + t[2]
	""", 1120.0)

	_check_number("constructor_single_keyed_section_still_works",
		"local t = {a = 2, b = 3} return t.a * 10 + t.b", 23.0)

	# Two sections of the SAME kind stay rejected, matching 4.0's
	# "invalid constructor syntax" check.
	_check_refused("constructor_rejects_two_keyed_sections",
		"local t = {x = 1; y = 2} return t.x", "differ in kind")
	_check_refused("constructor_rejects_two_positional_sections",
		"local t = {1, 2; 3, 4} return t[1]", "differ in kind")
	# And the 5.0 comma-mixed form stays rejected.
	_check_refused("constructor_rejects_the_5_0_mixed_form",
		"local t = {1, 2, x = 3} return t.x", "Lua 5.0")

	# All four ordering operators route through `lt`: 4.0 defines a>b as b<a,
	# a<=b as not (b<a) and a>=b as not (a<b).
	_check_number("all_ordering_routes_through_lt", """
		local mytag = newtag()
		settagmethod(mytag, 'lt', function(a, b) return a.v < b.v end)
		local x = {v = 1} settag(x, mytag)
		local y = {v = 2} settag(y, mytag)
		local n = 0
		if x < y then n = n + 1 end
		if y > x then n = n + 10 end
		if x <= y then n = n + 100 end
		if y >= x then n = n + 1000 end
		return n
	""", 1111.0)

	_check_number("function_tag_method_makes_a_table_callable", """
		local mytag = newtag()
		settagmethod(mytag, 'function', function(self, a, b) return a + b end)
		local callable = {} settag(callable, mytag)
		return callable(3, 4)
	""", 7.0)

	# A getglobal hook on the NIL tag traps reads of undefined globals, because
	# 4.0 picks the tag method from the tag of the value currently stored.
	_check_number("getglobal_tag_method_on_the_nil_tag", """
		settagmethod(tag(nil), 'getglobal', function(name, value)
			if value then return value end
			return 123
		end)
		return neverAssigned
	""", 123.0)

	# A proxy table whose reads are served from a backing store the tag method
	# reaches through a %upvalue. The reference is frozen at closure creation,
	# but the table's CONTENTS still change - which is why the value written
	# afterwards is visible.
	_check_number("index_tag_method_can_proxy_to_a_captured_table", """
		local mytag = newtag()
		local store = {}
		settagmethod(mytag, 'index', function(t, k) return rawget(%store, k) end)
		local proxy = {} settag(proxy, mytag)
		rawset(store, 'k', 42)
		return proxy.k
	""", 42.0)

	_check_number("gettagmethod_returns_what_was_set", """
		local mytag = newtag()
		local f = function() return 1 end
		settagmethod(mytag, 'index', f)
		if gettagmethod(mytag, 'index') == f then return 1 end
		return 0
	""", 1.0)

	_check_number("copytagmethods_duplicates_a_tag", """
		local a = newtag()
		local b = newtag()
		settagmethod(a, 'index', function() return 5 end)
		copytagmethods(b, a)
		local t = {} settag(t, b)
		return t.missing
	""", 5.0)

	_check_number("newtag_hands_out_distinct_tags",
		"if newtag() ~= newtag() then return 1 end return 0", 1.0)
	_check_number("tag_of_a_table_defaults_to_the_table_tag",
		"if tag({}) == tag({}) then return 1 end return 0", 1.0)

	# The three deprecated 4.0 event names never fire, so installing one would
	# be a silent no-op. Refuse it instead.
	_check_refused("deprecated_le_event_is_refused",
		"settagmethod(newtag(), 'le', function() end)", "deprecated")
	_check_refused("unknown_event_is_refused",
		"settagmethod(newtag(), 'newindex', function() end)", "not a valid tag method event")


# --- error model -------------------------------------------------------------

func _test_error_model() -> void:
	var outcome := _run_chunk("local x = 1\nlocal y = 2\nreturn nil + 1")
	_check("runtime_error_reports_the_line",
		not bool(outcome["ok"]) and int(outcome["error_line"]) == 3,
		"line=%s message=%s" % [str(outcome["error_line"]), str(outcome["error"])])
	_check("runtime_error_reports_the_chunk_name",
		String(outcome["error_chunk"]) == "chunk"
			and String(outcome["error"]).begins_with("chunk:3:"),
		String(outcome["error"]))
	_check("runtime_error_kind_is_runtime",
		String(outcome["error_kind"]) == "runtime", String(outcome["error_kind"]))

	var raised := _run_chunk("error('deliberate failure')")
	_check("error_builtin_surfaces_its_message",
		not bool(raised["ok"]) and String(raised["error"]).contains("deliberate failure"),
		String(raised["error"]))

	var asserted := _run_chunk("assert(nil, 'assertion detail')")
	_check("assert_surfaces_its_message",
		not bool(asserted["ok"]) and String(asserted["error"]).contains("assertion detail"),
		String(asserted["error"]))

	var bare_assert := _run_chunk("assert(nil)")
	_check("assert_without_a_message_says_assertion_failed",
		not bool(bare_assert["ok"])
			and String(bare_assert["error"]).contains("assertion failed"),
		String(bare_assert["error"]))

	# luaB_assert `return 0`: Lua 4.0's assert yields NOTHING on success. The
	# `local x = assert(f())` pass-through idiom is 5.0's assert and silently
	# produces nil here.
	_check_nil("assert_returns_no_values_on_success", "return assert(7)")
	_check_number("assert_still_lets_execution_continue",
		"assert(1) return 5", 5.0)

	# A protected call contains the error and lets the script keep going.
	_check_number("protected_call_contains_an_error", """
		function boom() error('inner') end
		local r = call(boom, {}, 'x')
		return 5
	""", 5.0)

	_check_number("protected_call_error_handler_runs", """
		function boom() error('inner') end
		local msg = call(boom, {}, 'x', function(m) return 77 end)
		return msg
	""", 77.0)

	_check_number("call_passes_arguments_from_a_table", """
		function add(a, b) return a + b end
		return call(add, {3, 4})
	""", 7.0)

	# The host survives, and a second run on the SAME sandbox still works.
	var box := _box()
	var first := box.do_string("error('one')", "a")
	var second := box.do_string("return 5", "b")
	_check("sandbox_still_usable_after_an_error",
		not bool(first["ok"]) and bool(second["ok"])
			and float(second["values"][0]) == 5.0,
		"first=%s second=%s" % [str(first["error"]), str(second["values"])])

	# A traceback names the frames.
	var deep := _run_chunk("function a() error('x') end function b() a() end b()")
	_check("error_carries_a_traceback",
		not bool(deep["ok"]) and deep["err"] != null
			and not deep["err"].traceback.is_empty(),
		str(deep["err"].traceback) if deep["err"] != null else "<no err>")

	# Recursion is stopped by the depth cap, not by a GDScript stack crash.
	var runaway := _run_chunk("function f() return f() end f()")
	_check("runaway_recursion_is_a_contained_error",
		not bool(runaway["ok"]) and String(runaway["error"]).contains("stack overflow"),
		String(runaway["error"]))


# --- loud refusals of Lua 5.x constructs -------------------------------------

func _test_loud_refusals() -> void:
	_check_refused("local_function_is_named",
		"local function f() end", "local function", "unsupported")
	_check_refused("vararg_expression_is_named",
		"function f(...) return ... end", "'...' as an expression", "unsupported")
	_check_refused("length_operator_is_named",
		"local t = {} return #t", "'#' is not an operator")
	_check_refused("goto_is_named", "goto continue", "goto", "unsupported")
	_check_refused("label_is_named", "::top::", "::label::")
	_check_refused("floor_division_is_named", "return 7 // 2", "floor division")
	_check_refused("modulo_operator_is_named", "return 7 % 2", "upvalue prefix", "unsupported")
	_check_refused("long_comment_is_named", "--[[ block ]]\nreturn 1", "long comment")
	_check_refused("hex_escape_is_named", "return \"\\x41\"", "hex escapes")
	_check_refused("mixed_table_constructor_is_named",
		"local t = {1, 2, x = 3}", "separate positional and keyed", "unsupported")
	_check_refused("upvalue_assignment_is_named",
		"function f() local x = 1 local g = function() %x = 2 end end",
		"read-only", "unsupported")
	_check_refused("upvalue_at_top_level_is_named",
		"local x = 1 return %x", "at top level")
	_check_refused("break_must_end_its_block",
		"while 1 do break return 1 end", "last statement in its block")
	_check_refused("return_must_end_its_block",
		"function f() return 1 return 2 end", "last statement in its block")
	_check_refused("iterator_triple_for_is_named",
		"local t = {} for k, v in next, t do end", "introduced in Lua 5.0", "unsupported")
	_check_refused("generic_for_over_a_non_table_is_named",
		"for k, v in next do end", "expects a table", "unsupported")
	_check_refused("unfinished_string_is_named", "return 'abc", "unfinished string")
	_check_refused("malformed_number_is_named", "return 3abc", "malformed number")
	_check_refused("missing_end_is_named", "if 1 then return 2", "'end' expected")

	# 5.x library names are tripwires, not nil - each one names its 4.0 form.
	_check_refused("setmetatable_tripwire", "setmetatable({}, {})", "TAGS, not metatables")
	_check_refused("pairs_tripwire", "for k, v in pairs({}) do end", "no pairs")
	_check_refused("ipairs_tripwire", "for i, v in ipairs({}) do end", "no ipairs")
	_check_refused("pcall_tripwire", "pcall(function() end)", "no pcall")
	_check_refused("string_module_tripwire", "return string.sub('abc', 1, 2)",
		"the string functions are GLOBALS")
	_check_refused("table_module_tripwire", "table.insert({}, 1)", "tinsert")
	_check_refused("math_module_tripwire", "return math.floor(1.5)", "no `math` table")
	_check_refused("true_is_not_a_keyword", "if true then return 1 end",
		"no boolean type")
	_check_refused("false_is_not_a_keyword", "return false", "no boolean type")


# --- review regressions: one test per source-verified finding ----------------
##
## Every expected value below is derived from the lua-4.0.1 REFERENCE (the
## manual, or the named C function in the 4.0.1 sources), never from running
## this VM and recording what it did. Several of these findings - repeat-until
## scope, {f()} multret, the unm argument list - are exactly the kind a test
## written against the implementation would have confirmed rather than caught.

func _test_review_regressions() -> void:
	# ltm/lvm: a table consults `gettable`/`settable` only when it carries a
	# NON-DEFAULT tag that has the method. luaV_gettable takes the raw path
	# when (htag == LUA_TTABLE || gettm(GETTABLE) == NULL).
	_check_number("gettable_tag_method_fires_on_a_custom_tagged_table", """
		local mytag = newtag()
		settagmethod(mytag, 'gettable', function(t, k) return 55 end)
		local t = {}
		settag(t, mytag)
		t.present = 1
		return t.present
	""", 55.0)
	_check_number("settable_tag_method_fires_on_a_custom_tagged_table", """
		seen = nil
		local mytag = newtag()
		settagmethod(mytag, 'settable', function(t, k, v) seen = k .. '=' .. v end)
		local t = {}
		settag(t, mytag)
		t.hp = 7
		if rawget(t, 'hp') then return -1 end
		if seen == 'hp=7' then return 1 end
		return 0
	""", 1.0)
	# ...and a DEFAULT-tagged table never fires it, however the method is set.
	_check_number("gettable_tag_method_does_not_fire_on_a_plain_table", """
		settagmethod(tag({}), 'gettable', function(t, k) return 55 end)
		local t = {}
		t.present = 1
		return t.present
	""", 1.0)

	# The 4.0 manual's unm_event pseudo-code is tm(op, nil, "unm"): operand,
	# NIL, event name. Passing (op, op) looks harmless until a tag method
	# inspects its second argument.
	_check_string("unm_tag_method_receives_operand_nil_and_event", """
		local mytag = newtag()
		settagmethod(mytag, 'unm', function(a, b, event)
			if b then return 'second-arg-not-nil' end
			return event
		end)
		local x = {} settag(x, mytag)
		return -x
	""", "unm")
	# Arithmetic events DO get the event name as a third argument.
	_check_string("arithmetic_tag_method_receives_the_event_name", """
		local mytag = newtag()
		settagmethod(mytag, 'add', function(a, b, event) return event end)
		local x = {} settag(x, mytag)
		return x + x
	""", "add")

	# llex.c read_long_string switches on each character and its '\n' case does
	# save(L, '\n', l) - the newline right after [[ is KEPT. The skip is Lua
	# 5.0's (it added an explicit inclinenumber before the loop).
	_check_number("long_string_keeps_the_leading_newline",
		"return strlen([[\nab]])", 3.0)
	_check_string("long_string_leading_newline_is_the_first_character",
		"return strsub([[\nab]], 1, 1)", "\n")

	# read_string's `default: save_and_next(...)` takes an unrecognised escape
	# literally - the backslash is dropped and the character kept.
	_check_string("unknown_escape_is_taken_literally", "return \"a\\qb\"", "aqb")
	_check_string("escaped_percent_is_just_a_percent", "return \"100\\%\"", "100%")
	# \x and \z stay refused on purpose: their 4.0 reading ("x41", "z") is
	# silently wrong rather than merely different.
	_check_refused("hex_escape_stays_refused", "return \"\\x41\"", "hex escapes")

	# read_numeral has no 0x branch; hex literals are Lua 5.1.
	_check_refused("hex_literal_is_refused", "return 0x1F",
		"hexadecimal literals", "unsupported")

	# The NaN guard must hold on EVERY write path, not just assignment.
	_check_refused("nan_key_refused_in_a_constructor",
		"local t = {[0/0] = 1}", "NaN")
	_check_refused("nan_key_refused_through_rawset",
		"rawset({}, 0/0, 1)", "NaN")
	_check_refused("nil_key_refused_through_rawset",
		"rawset({}, nil, 1)", "index is nil")

	# Concatenation is the cheapest memory bomb in the language; the cap is
	# checked before allocating.
	var bomb := LuaSandbox.new("bomb")
	bomb.max_string_length = 4096
	var bombed := bomb.do_string(
		"local s = 'x' for i = 1, 60 do s = s .. s end return strlen(s)", "bomb")
	_check("runaway_concatenation_is_stopped",
		not bool(bombed["ok"]) and String(bombed["error"]).contains("character limit"),
		String(bombed["error"]))
	_check_number("ordinary_concatenation_still_works",
		"return strlen('abc' .. 'de')", 5.0)

func _finish() -> void:
	print("LUA_VM_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
