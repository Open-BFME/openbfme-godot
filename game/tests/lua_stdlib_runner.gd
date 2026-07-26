extends SceneTree

## Lua 4.0.1 VM - standard library contract.
##
## The BFME2 modding surface is the FLAT 4.0 library (strsub / tinsert / getn /
## sin), not the 5.x module tables, so this runner exercises the flat names and
## the exact 4.0 behaviours behind them: the `n` field that tinsert maintains,
## fmod's truncated remainder, Lua's own pattern language, and the printf
## subset in format().

const LuaSandbox := preload("res://src/lua/lua_sandbox.gd")
const LuaTable := preload("res://src/lua/lua_table.gd")

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_basic_library()
	_test_string_library()
	_test_patterns()
	_test_format()
	_test_table_library()
	_test_deterministic_iteration()
	_test_math_library()
	_test_random()
	_finish()


# --- helpers -----------------------------------------------------------------

## See lua_vm_runner._value: probe the "return <source>" form, else run the
## source as its own chunk.
func _value(source: String) -> Variant:
	var box := LuaSandbox.new("test")
	var probe := box.compile("return " + source, "expr")
	var outcome := box.call_closure(probe["closure"], []) if bool(probe["ok"]) \
		else box.do_string(source, "expr")
	if not bool(outcome["ok"]):
		return "<error:%s>" % outcome["error"]
	var values: Array = outcome["values"]
	return values[0] if not values.is_empty() else null


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("LUA_STDLIB PASS %s" % label)
	else:
		failed += 1
		printerr("LUA_STDLIB FAIL %s%s" % [label, " (%s)" % detail if detail != "" else ""])


func _check_number(label: String, source: String, expected: float) -> void:
	var got: Variant = _value(source)
	_check(label, got is float and is_equal_approx(float(got), expected),
		"%s -> %s, wanted %s" % [source, str(got), str(expected)])


func _check_string(label: String, source: String, expected: String) -> void:
	var got: Variant = _value(source)
	_check(label, got is String and String(got) == expected,
		"%s -> %s, wanted '%s'" % [source, str(got), expected])


func _check_nil(label: String, source: String) -> void:
	var got: Variant = _value(source)
	_check(label, got == null, "%s -> %s, wanted nil" % [source, str(got)])


func _check_refused(label: String, source: String, fragment: String) -> void:
	var box := LuaSandbox.new("test")
	var outcome := box.do_string(source, "chunk")
	if bool(outcome["ok"]):
		failed += 1
		printerr("LUA_STDLIB FAIL %s (chunk was ACCEPTED, expected refusal)" % label)
		return
	if String(outcome["error"]).containsn(fragment):
		passed += 1
		print("LUA_STDLIB PASS %s" % label)
	else:
		failed += 1
		printerr("LUA_STDLIB FAIL %s (message did not name '%s': %s)"
			% [label, fragment, String(outcome["error"])])


# --- basic library -----------------------------------------------------------

func _test_basic_library() -> void:
	_check_string("type_of_nil", "type(nil)", "nil")
	_check_string("type_of_number", "type(1)", "number")
	_check_string("type_of_string", "type('a')", "string")
	_check_string("type_of_table", "type({})", "table")
	_check_string("type_of_function", "type(print)", "function")

	_check_number("tonumber_decimal", "tonumber('42')", 42.0)
	_check_number("tonumber_trims_whitespace", "tonumber('  7  ')", 7.0)
	_check_number("tonumber_float", "tonumber('3.5')", 3.5)
	_check_number("tonumber_exponent", "tonumber('2e3')", 2000.0)
	_check_number("tonumber_hex", "tonumber('0x1F')", 31.0)
	_check_number("tonumber_with_base", "tonumber('ff', 16)", 255.0)
	_check_number("tonumber_binary_base", "tonumber('1011', 2)", 11.0)
	_check_nil("tonumber_rejects_words", "tonumber('hello')")
	_check_nil("tonumber_rejects_bad_digit_for_base", "tonumber('2', 2)")

	_check_string("tostring_number", "tostring(12)", "12")
	_check_string("tostring_nil", "tostring(nil)", "nil")
	_check_string("tostring_string_is_identity", "tostring('x')", "x")
	_check_number("tostring_of_a_table_is_stable",
		"local t = {} if tostring(t) == tostring(t) then return 1 end return 0", 1.0)
	_check_number("distinct_tables_stringify_differently",
		"if tostring({}) ~= tostring({}) then return 1 end return 0", 1.0)

	# print() is captured by the host, not written to a terminal the sandbox
	# cannot see.
	var box := LuaSandbox.new("print_test")
	var printed := box.do_string("print('a', 1) print('b')", "p")
	var lines := box.output_lines()
	_check("print_is_captured_by_the_host",
		bool(printed["ok"]) and lines.size() == 2 and lines[0] == "a\t1" and lines[1] == "b",
		str(lines))

	_check_number("dostring_runs_and_returns", "return dostring('return 6 * 7')", 42.0)
	_check_number("dostring_shares_the_globals",
		"shared = 3 dostring('shared = shared + 1') return shared", 4.0)
	_check_number("dostring_returns_nil_on_a_bad_chunk",
		"local r = dostring('this is not lua') if r then return 1 end return 0", 0.0)
	_check_number("collectgarbage_is_a_harmless_noop",
		"collectgarbage() return 1", 1.0)


# --- string library (flat names) --------------------------------------------

func _test_string_library() -> void:
	_check_number("strlen", "strlen('hello')", 5.0)
	_check_number("strlen_of_empty", "strlen('')", 0.0)
	_check_string("strsub_range", "strsub('hello', 2, 4)", "ell")
	_check_string("strsub_to_end_by_default", "strsub('hello', 3)", "llo")
	_check_string("strsub_negative_start", "strsub('hello', -3)", "llo")
	_check_string("strsub_negative_range", "strsub('hello', -3, -2)", "ll")
	_check_string("strsub_clamps_out_of_range", "strsub('hello', 0, 100)", "hello")
	_check_string("strsub_empty_when_reversed", "strsub('hello', 4, 2)", "")
	_check_string("strlower", "strlower('MiXeD')", "mixed")
	_check_string("strupper", "strupper('MiXeD')", "MIXED")
	_check_string("strrep", "strrep('ab', 3)", "ababab")
	_check_string("strrep_zero_is_empty", "strrep('ab', 0)", "")
	_check_number("strbyte_default_index", "strbyte('A')", 65.0)
	_check_number("strbyte_at_index", "strbyte('ABC', 3)", 67.0)
	_check_nil("strbyte_out_of_range", "strbyte('ABC', 9)")
	_check_string("strchar", "strchar(72, 105)", "Hi")
	_check_string("strchar_of_nothing", "strchar()", "")

	# A script must not be able to exhaust host memory with one call.
	_check_refused("strrep_is_bounded", "return strrep('x', 99999999)",
		"character limit")


# --- patterns ----------------------------------------------------------------

func _test_patterns() -> void:
	_check_number("strfind_literal", "strfind('hello world', 'world')", 7.0)
	_check_number("strfind_returns_end_too",
		"local s, e = strfind('hello world', 'world') return e", 11.0)
	_check_nil("strfind_miss_returns_nil", "strfind('hello', 'zebra')")
	_check_number("strfind_from_init", "strfind('abcabc', 'b', 3)", 5.0)
	_check_number("strfind_negative_init", "strfind('abcabc', 'b', -3)", 5.0)
	_check_number("strfind_plain_ignores_magic",
		"strfind('a.c', '.', 1, 1)", 2.0)
	_check_number("strfind_anchor_start", "strfind('hello', '^he')", 1.0)
	_check_nil("strfind_anchor_start_fails", "strfind('ahello', '^he')")
	_check_number("strfind_anchor_end",
		"local s, e = strfind('hello', 'lo$') return s", 4.0)

	_check_number("class_digit", "strfind('abc123', '%d+')", 4.0)
	_check_number("class_alpha", "strfind('123abc', '%a+')", 4.0)
	_check_number("class_space", "strfind('ab cd', '%s')", 3.0)
	_check_number("class_complement", "strfind('   x', '%S')", 4.0)
	_check_number("set_range", "strfind('zzz7', '[0-9]')", 4.0)
	_check_number("set_negated", "strfind('aaab', '[^a]')", 4.0)
	_check_number("set_with_escape", "strfind('a]b', '[%]]')", 2.0)
	_check_number("quantifier_star_matches_empty", "strfind('abc', 'x*')", 1.0)
	_check_number("quantifier_question", "strfind('color', 'colou?r')", 1.0)
	_check_number("lazy_quantifier_stops_early",
		"local s, e = strfind('<a><b>', '<.->') return e", 3.0)
	_check_number("greedy_quantifier_runs_on",
		"local s, e = strfind('<a><b>', '<.*>') return e", 6.0)
	_check_string("capture_returned", "local s, e, c = strfind('key=value', '(%w+)=') return c",
		"key")
	_check_string("two_captures", """
		local s, e, a, b = strfind('key=value', '(%w+)=(%w+)')
		return a .. '/' .. b
	""", "key/value")
	_check_number("position_capture",
		"local s, e, p = strfind('abcd', 'b()') return p", 3.0)
	_check_number("back_reference",
		"strfind('abcabc', '(abc)%1')", 1.0)
	_check_number("balanced_match",
		"local s, e = strfind('x(a(b)c)y', '%b()') return e", 8.0)
	_check_number("escaped_magic_character", "strfind('a.b', '%.')", 2.0)

	_check_string("gsub_replaces_all", "gsub('hello world', 'o', '0')", "hell0 w0rld")
	_check_number("gsub_returns_a_count",
		"local s, n = gsub('hello world', 'o', '0') return n", 2.0)
	_check_string("gsub_respects_max_count", "gsub('aaaa', 'a', 'b', 2)", "bbaa")
	_check_string("gsub_capture_reference",
		"gsub('hello world', '(%w+)', '<%1>')", "<hello> <world>")
	_check_string("gsub_whole_match_reference",
		"gsub('abc', '%a', '[%0]')", "[a][b][c]")
	_check_string("gsub_escaped_percent", "gsub('a', 'a', '100%%')", "100%")
	_check_string("gsub_function_replacement",
		"gsub('abc', '%a', function(c) return strupper(c) end)", "ABC")
	_check_string("gsub_function_returning_nil_keeps_the_match",
		"gsub('abc', '%a', function(c) if c == 'b' then return 'B' end end)", "aBc")
	_check_string("gsub_empty_pattern_inserts_between",
		"gsub('abc', '', '-')", "-a-b-c-")
	_check_string("gsub_balanced", "gsub('f(a(b))g', '%b()', 'X')", "fXg")
	_check_string("gsub_anchored_only_hits_the_start",
		"gsub('aaa', '^a', 'X')", "Xaa")

	# Table replacement is Lua 5.0; refusing it by name beats silently
	# producing the wrong string.
	_check_refused("gsub_table_replacement_is_named",
		"return gsub('abc', '%a', {})", "introduced in Lua 5.0")
	_check_refused("frontier_pattern_is_named",
		"return strfind('abc', '%f[%a]')", "added in Lua 5.1")
	_check_refused("malformed_pattern_is_named",
		"return strfind('abc', '[abc')", "missing ']'")
	_check_refused("dangling_escape_is_named",
		"return strfind('abc', 'a%')", "ends with '%'")


# --- format ------------------------------------------------------------------

func _test_format() -> void:
	_check_string("format_integer", "format('%d', 42)", "42")
	_check_string("format_negative_integer", "format('%d', -42)", "-42")
	_check_string("format_truncates_toward_zero", "format('%d', 3.9)", "3")
	_check_string("format_width", "format('%5d', 42)", "   42")
	_check_string("format_left_align", "format('%-5d|', 42)", "42   |")
	_check_string("format_zero_pad", "format('%05d', 42)", "00042")
	_check_string("format_zero_pad_keeps_sign_first", "format('%05d', -42)", "-0042")
	_check_string("format_plus_flag", "format('%+d', 42)", "+42")
	_check_string("format_hex_lower", "format('%x', 255)", "ff")
	_check_string("format_hex_upper", "format('%X', 255)", "FF")
	_check_string("format_hex_alternate", "format('%#x', 255)", "0xff")
	_check_string("format_octal", "format('%o', 8)", "10")
	_check_string("format_char", "format('%c', 65)", "A")
	_check_string("format_fixed_default_precision", "format('%f', 1.5)", "1.500000")
	_check_string("format_fixed_precision", "format('%.2f', 3.14159)", "3.14")
	_check_string("format_fixed_zero_precision", "format('%.0f', 2.7)", "3")
	_check_string("format_fixed_rounds_up", "format('%.2f', 0.999)", "1.00")
	_check_string("format_fixed_small_value", "format('%.3f', 0.0004)", "0.000")
	_check_string("format_fixed_small_value_rounds", "format('%.3f', 0.0006)", "0.001")
	_check_string("format_scientific", "format('%.3e', 1234.56)", "1.235e+03")
	_check_string("format_scientific_upper", "format('%.2E', 0.00123)", "1.23E-03")
	_check_string("format_scientific_of_zero", "format('%.2e', 0)", "0.00e+00")
	_check_string("format_general_strips_zeros", "format('%g', 1.5)", "1.5")
	_check_string("format_general_integer", "format('%g', 100000)", "100000")
	_check_string("format_general_switches_to_exponent", "format('%g', 1000000)", "1e+06")
	_check_string("format_general_small", "format('%g', 0.0001)", "0.0001")
	_check_string("format_general_smaller_uses_exponent", "format('%g', 0.00001)", "1e-05")
	_check_string("format_string", "format('%s and %s', 'a', 'b')", "a and b")
	_check_string("format_string_precision_truncates", "format('%.3s', 'abcdef')", "abc")
	_check_string("format_string_width", "format('%6s|', 'ab')", "    ab|")
	_check_string("format_percent_literal", "format('100%%')", "100%")
	_check_string("format_number_through_s", "format('%s', 3.5)", "3.5")
	_check_string("format_quoted", "format('%q', 'a\"b')", "\"a\\\"b\"")
	_check_string("format_quoted_backslash", "format('%q', 'a\\\\b')", "\"a\\\\b\"")
	_check_string("format_coerces_numeric_strings", "format('%d', '17')", "17")
	_check_refused("format_rejects_a_bad_conversion", "return format('%y', 1)",
		"invalid conversion")
	_check_refused("format_rejects_a_missing_argument", "return format('%d')",
		"no value")
	_check_refused("format_rejects_a_non_number", "return format('%d', {})",
		"number expected")


# --- table library (flat names) ---------------------------------------------

func _test_table_library() -> void:
	# With no `n` field, getn counts the 1..k run (Lua 4.0's luaL_getn).
	_check_number("getn_counts_the_border", "return getn({10, 20, 30})", 3.0)
	_check_number("getn_of_empty", "return getn({})", 0.0)
	_check_number("getn_stops_at_a_hole",
		"local t = {} t[1] = 1 t[2] = 2 t[4] = 4 return getn(t)", 2.0)
	# An explicit `n` field WINS over the border - this is 4.0, not 5.x.
	_check_number("getn_prefers_an_explicit_n_field",
		"local t = {1, 2, 3} t.n = 99 return getn(t)", 99.0)

	# tinsert maintains `n`, which is why 4.0 tables grow an `n` and 5.x ones
	# do not.
	_check_number("tinsert_appends", "local t = {} tinsert(t, 'a') return getn(t)", 1.0)
	_check_string("tinsert_appends_at_the_end",
		"local t = {'a'} tinsert(t, 'b') return t[2]", "b")
	_check_number("tinsert_writes_the_n_field",
		"local t = {} tinsert(t, 1) return t.n", 1.0)
	_check_string("tinsert_at_a_position_shifts_up",
		"local t = {'a', 'b'} tinsert(t, 1, 'z') return t[1] .. t[2] .. t[3]", "zab")
	_check_number("tinsert_beyond_the_end_grows",
		"local t = {} tinsert(t, 5, 'x') return getn(t)", 5.0)

	_check_string("tremove_returns_the_last", "local t = {'a', 'b'} return tremove(t)", "b")
	_check_number("tremove_shrinks_n",
		"local t = {'a', 'b'} tremove(t) return getn(t)", 1.0)
	_check_string("tremove_at_a_position_shifts_down",
		"local t = {'a', 'b', 'c'} tremove(t, 1) return t[1] .. t[2]", "bc")
	_check_nil("tremove_on_empty_returns_nil", "return tremove({})")

	_check_string("sort_numbers_ascending", """
		local t = {3, 1, 2}
		sort(t)
		return t[1] .. t[2] .. t[3]
	""", "123")
	_check_string("sort_strings", """
		local t = {'pear', 'apple', 'fig'}
		sort(t)
		return t[1] .. '/' .. t[2] .. '/' .. t[3]
	""", "apple/fig/pear")
	_check_string("sort_with_a_comparator", """
		local t = {1, 3, 2}
		sort(t, function(a, b) return a > b end)
		return t[1] .. t[2] .. t[3]
	""", "321")
	_check_string("sort_is_stable", """
		local t = {}
		tinsert(t, {k = 1, tag = 'a'})
		tinsert(t, {k = 1, tag = 'b'})
		tinsert(t, {k = 0, tag = 'c'})
		sort(t, function(x, y) return x.k < y.k end)
		return t[1].tag .. t[2].tag .. t[3].tag
	""", "cab")
	_check_number("sort_of_one_element_is_a_noop",
		"local t = {5} sort(t) return t[1]", 5.0)
	_check_refused("sort_without_a_comparator_on_mixed_types_is_named",
		"local t = {} t.n = 2 t[1] = 1 t[2] = {} sort(t)", "attempt to compare")
	_check_refused("a_comparator_error_propagates",
		"local t = {2, 1} sort(t, function(a, b) error('boom') end)", "boom")

	# NOTE the globals. A Lua 4.0 callback cannot see the enclosing chunk's
	# LOCALS - there is no lexical capture, only %upvalues (which are frozen
	# copies and so cannot accumulate). Accumulating through a global is how
	# 4.0 code actually does this.
	_check_number("foreach_visits_every_pair", """
		local t = {a = 1, b = 2, c = 3}
		total = 0
		foreach(t, function(k, v) total = total + v end)
		return total
	""", 6.0)
	_check_number("foreach_stops_on_a_non_nil_return", """
		local t = {5, 6, 7}
		return foreach(t, function(k, v) if v == 6 then return v end end)
	""", 6.0)
	_check_number("foreachi_walks_1_to_getn", """
		local t = {10, 20, 30}
		total = 0
		foreachi(t, function(i, v) total = total + i * v end)
		return total
	""", 140.0)

	_check_string("next_starts_a_traversal",
		"local t = {} t.only = 'v' local k, v = next(t) return k .. '=' .. v", "only=v")
	_check_nil("next_past_the_end_is_nil",
		"local t = {} t.only = 1 return next(t, 'only')")
	_check_refused("next_with_a_foreign_key_is_named",
		"local t = {} return next(t, 'absent')", "invalid key")

	_check_number("rawget_and_rawset_bypass_tag_methods", """
		local mytag = newtag()
		local t = {}
		settag(t, mytag)
		settagmethod(mytag, 'index', function() return 999 end)
		rawset(t, 'x', 1)
		if rawget(t, 'missing') then return 0 end
		return rawget(t, 'x')
	""", 1.0)


# --- deterministic iteration -------------------------------------------------

func _test_deterministic_iteration() -> void:
	# The order is INSERTION order and must be identical every run, on every
	# platform. A lockstep sim cannot tolerate a hash walk here.
	var source := """
		local t = {}
		t.zulu = 1
		t.alpha = 2
		t[7] = 3
		t.mike = 4
		t[1] = 5
		local order = ''
		for k, v in t do order = order .. tostring(k) .. ',' end
		return order
	"""
	_check_string("generic_for_uses_insertion_order", source, "zulu,alpha,7,mike,1,")

	var box := LuaSandbox.new("order")
	var first := box.do_string(source, "a")
	var second := LuaSandbox.new("order").do_string(source, "b")
	_check("iteration_order_is_reproducible_across_sandboxes",
		bool(first["ok"]) and bool(second["ok"])
			and String(first["values"][0]) == String(second["values"][0]),
		"%s vs %s" % [str(first["values"]), str(second["values"])])

	_check_string("foreach_uses_the_same_order", """
		local t = {}
		t.z = 1 t.a = 2 t.m = 3
		order = ''
		foreach(t, function(k, v) order = order .. k end)
		return order
	""", "zam")

	# Removing a key does not disturb the surviving order, and re-adding it
	# puts it back in its original slot rather than at the end.
	_check_string("removal_and_reinsertion_keep_the_slot", """
		local t = {}
		t.a = 1 t.b = 2 t.c = 3
		t.b = nil
		t.b = 9
		local order = ''
		for k, v in t do order = order .. k end
		return order
	""", "abc")

	_check_string("removal_alone_leaves_the_rest_in_order", """
		local t = {}
		t.a = 1 t.b = 2 t.c = 3
		t.b = nil
		local order = ''
		for k, v in t do order = order .. k end
		return order
	""", "ac")

	# Clearing the current key mid-traversal is legal in Lua and must not
	# derail the walk.
	_check_string("clearing_during_traversal_is_safe", """
		local t = {}
		t.a = 1 t.b = 2 t.c = 3
		local order = ''
		for k, v in t do order = order .. k t[k] = nil end
		return order
	""", "abc")


# --- math library (flat names) ----------------------------------------------

func _test_math_library() -> void:
	_check_number("abs", "abs(-3.5)", 3.5)
	_check_number("floor", "floor(3.7)", 3.0)
	_check_number("floor_negative", "floor(-3.2)", -4.0)
	_check_number("ceil", "ceil(3.2)", 4.0)
	_check_number("sqrt", "sqrt(16)", 4.0)
	_check_number("max_of_several", "max(1, 5, 3)", 5.0)
	_check_number("min_of_several", "min(4, 2, 9)", 2.0)
	_check_number("PI_is_available", "floor(PI * 100)", 314.0)
	_check_number("power_operator", "2 ^ 10", 1024.0)
	_check_number("fractional_power", "4 ^ 0.5", 2.0)

	# mod is C's fmod: TRUNCATED, so the sign follows the dividend. Lua 5.x's
	# '%' floors instead, which flips the sign of this case.
	_check_number("mod_positive", "mod(7, 3)", 1.0)
	_check_number("mod_takes_the_dividend_sign", "mod(-7, 3)", -1.0)

	_check_number("frexp_mantissa", "local m, e = frexp(8) return m", 0.5)
	_check_number("frexp_exponent", "local m, e = frexp(8) return e", 4.0)
	_check_number("ldexp_inverts_frexp", "ldexp(0.5, 4)", 8.0)
	_check_number("ldexp_negative_exponent", "ldexp(8, -2)", 2.0)
	_check_number("deg_and_rad_round_trip", "floor(deg(rad(180)) + 0.5)", 180.0)
	_check_number("sin_of_zero", "sin(0)", 0.0)
	_check_number("cos_of_zero", "cos(0)", 1.0)
	_check_number("log_and_exp_round_trip", "floor(log(exp(3)) + 0.5)", 3.0)
	_check_number("log10", "floor(log10(1000) + 0.5)", 3.0)
	_check_number("atan2_quadrant", "floor(deg(atan2(1, 1)) + 0.5)", 45.0)


# --- deterministic random ----------------------------------------------------

func _test_random() -> void:
	var source := "local s = '' for i = 1, 5 do s = s .. format('%.6f,', random()) end return s"
	var first := LuaSandbox.new("r1").do_string(source, "r")
	var second := LuaSandbox.new("r2").do_string(source, "r")
	_check("random_is_identical_across_fresh_sandboxes",
		bool(first["ok"]) and bool(second["ok"])
			and String(first["values"][0]) == String(second["values"][0]),
		"%s vs %s" % [str(first["values"]), str(second["values"])])

	var seeded := "randomseed(1234) local s = '' for i = 1, 3 do s = s .. random(100) .. ',' end return s"
	var a := LuaSandbox.new("r3").do_string(seeded, "r")
	var b := LuaSandbox.new("r4").do_string(seeded, "r")
	_check("randomseed_produces_a_reproducible_stream",
		bool(a["ok"]) and String(a["values"][0]) == String(b["values"][0]),
		"%s vs %s" % [str(a["values"]), str(b["values"])])

	var other_seed := LuaSandbox.new("r5").do_string(
		"randomseed(99) local s = '' for i = 1, 3 do s = s .. random(100) .. ',' end return s", "r")
	_check("a_different_seed_gives_a_different_stream",
		String(other_seed["values"][0]) != String(a["values"][0]),
		"%s vs %s" % [str(other_seed["values"]), str(a["values"])])

	_check_number("random_with_one_bound_stays_in_range", """
		randomseed(7)
		local ok = 1
		for i = 1, 200 do
			local r = random(6)
			if r < 1 then ok = 0 end
			if r > 6 then ok = 0 end
			if r ~= floor(r) then ok = 0 end
		end
		return ok
	""", 1.0)
	_check_number("random_with_two_bounds_stays_in_range", """
		randomseed(7)
		local ok = 1
		for i = 1, 200 do
			local r = random(10, 20)
			if r < 10 then ok = 0 end
			if r > 20 then ok = 0 end
		end
		return ok
	""", 1.0)
	_check_number("random_unit_stays_below_one", """
		randomseed(3)
		local ok = 1
		for i = 1, 200 do
			local r = random()
			if r < 0 then ok = 0 end
			if r >= 1 then ok = 0 end
		end
		return ok
	""", 1.0)
	_check_refused("random_rejects_an_empty_interval", "return random(5, 1)",
		"interval is empty")


func _finish() -> void:
	print("LUA_STDLIB_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
