extends RefCounted

## Lua 4.0.1 format() - a printf subset implemented on top of LuaNumber.
##
## Godot's own `%` string operator is NOT used: it does not implement %e, %g,
## %q or the full flag set, and where it does overlap it routes floats through
## snprintf, which is the exact cross-platform hazard LuaNumber exists to
## avoid. Every conversion here is built from LuaNumber's deterministic decimal
## conversion.
##
## Supported:  %d %i %u %o %x %X %c %f %e %E %g %G %s %q %%
## Flags:      '-' left align, '+' force sign, ' ' space for positive,
##             '#' alternate form (0/0x prefixes, keep the '.'), '0' zero pad
## Width and precision, including the literal forms only ("%*d" is a C
## extension Lua 4.0 does not accept either).

const LuaValue := preload("res://src/lua/lua_value.gd")
const LuaNumber := preload("res://src/lua/lua_number.gd")


## Renders a format string.
##
## `tostring_fn` converts a Lua value for %s and must be the sandbox's
## tostring, so that tables render with their deterministic serial.
## Returns {"ok": bool, "text": String, "error": String}.
static func format(spec: String, args: Array, tostring_fn: Callable) -> Dictionary:
	var out := ""
	var i := 0
	var arg_index := 0
	var length := spec.length()

	while i < length:
		var c := spec.unicode_at(i)
		if c != 37:  # '%'
			out += spec.substr(i, 1)
			i += 1
			continue
		i += 1
		if i >= length:
			return _error("format string ends with a lone '%'")
		if spec.unicode_at(i) == 37:
			out += "%"
			i += 1
			continue

		var flags := {"minus": false, "plus": false, "space": false,
			"hash": false, "zero": false}
		while i < length:
			match spec.unicode_at(i):
				45: flags["minus"] = true   # -
				43: flags["plus"] = true    # +
				32: flags["space"] = true
				35: flags["hash"] = true    # #
				48: flags["zero"] = true    # 0
				_: break
			i += 1

		var width := 0
		while i < length and _is_digit(spec.unicode_at(i)):
			width = width * 10 + (spec.unicode_at(i) - 48)
			i += 1
		var precision := -1
		if i < length and spec.unicode_at(i) == 46:  # '.'
			i += 1
			precision = 0
			while i < length and _is_digit(spec.unicode_at(i)):
				precision = precision * 10 + (spec.unicode_at(i) - 48)
				i += 1
		if i >= length:
			return _error("incomplete format specifier")

		var conversion := spec.substr(i, 1)
		i += 1

		if arg_index >= args.size():
			return _error("bad argument #%d to `format' (no value)" % (arg_index + 1))
		var argument: Variant = args[arg_index]
		arg_index += 1

		var body := ""
		var sign := ""
		var numeric := true

		match conversion:
			"d", "i", "u":
				var value: Variant = LuaValue.coerce_to_number(argument)
				if value == null:
					return _error(_number_error(arg_index, argument))
				var whole := LuaNumber.to_c_int(float(value))
				if whole < 0:
					sign = "-"
				# NOT absi(): |INT64_MIN| is not representable, so absi leaves
				# it negative and str() then emits a second '-'. Stripping the
				# sign from the decimal text is exact at every magnitude.
				body = str(whole).trim_prefix("-")
				if precision >= 0:
					body = _pad_left_zeros(body, precision)
			"o", "x", "X":
				var value_base: Variant = LuaValue.coerce_to_number(argument)
				if value_base == null:
					return _error(_number_error(arg_index, argument))
				var base_int := LuaNumber.to_c_int(float(value_base))
				var base := 8 if conversion == "o" else 16
				if base_int < 0:
					sign = "-"
				# Same INT64_MIN trap as %d: build the magnitude by halving
				# once rather than negating a value that cannot be negated.
				body = _to_base_magnitude(base_int, base, conversion == "X")
				if precision >= 0:
					body = _pad_left_zeros(body, precision)
				if flags["hash"] and base_int != 0:
					if conversion == "o":
						if not body.begins_with("0"):
							body = "0" + body
					else:
						body = ("0X" if conversion == "X" else "0x") + body
			"c":
				var code: Variant = LuaValue.coerce_to_number(argument)
				if code == null:
					return _error(_number_error(arg_index, argument))
				body = String.chr(LuaNumber.to_c_int(float(code)))
				numeric = false
			"f":
				var value_f: Variant = LuaValue.coerce_to_number(argument)
				if value_f == null:
					return _error(_number_error(arg_index, argument))
				var rendered := LuaNumber.format_fixed(float(value_f),
					6 if precision < 0 else precision)
				var split := _split_sign(rendered)
				sign = split[0]
				body = split[1]
			"e", "E":
				var value_e: Variant = LuaValue.coerce_to_number(argument)
				if value_e == null:
					return _error(_number_error(arg_index, argument))
				var rendered_e := LuaNumber.format_scientific(float(value_e),
					6 if precision < 0 else precision, conversion == "E")
				var split_e := _split_sign(rendered_e)
				sign = split_e[0]
				body = split_e[1]
			"g", "G":
				var value_g: Variant = LuaValue.coerce_to_number(argument)
				if value_g == null:
					return _error(_number_error(arg_index, argument))
				var rendered_g := LuaNumber.format_general(float(value_g),
					6 if precision < 0 else precision, conversion == "G",
					flags["hash"])
				var split_g := _split_sign(rendered_g)
				sign = split_g[0]
				body = split_g[1]
			"s":
				body = String(tostring_fn.call(argument))
				if precision >= 0 and body.length() > precision:
					body = body.substr(0, precision)
				numeric = false
			"q":
				if not LuaValue.is_string(argument) and not LuaValue.is_number(argument):
					return _error("bad argument #%d to `format' (string expected, got %s)"
						% [arg_index, LuaValue.type_name(argument)])
				body = _quote(String(LuaValue.coerce_to_string(argument)))
				numeric = false
			_:
				return _error("invalid conversion '%%%s' to `format'" % conversion)

		if numeric and sign.is_empty():
			if flags["plus"]:
				sign = "+"
			elif flags["space"]:
				sign = " "

		out += _apply_width(sign, body, width, flags, numeric)

	return {"ok": true, "text": out, "error": ""}


static func _apply_width(sign: String, body: String, width: int,
		flags: Dictionary, numeric: bool) -> String:
	var text := sign + body
	if text.length() >= width:
		return text
	var pad := width - text.length()
	if flags["minus"]:
		return text + " ".repeat(pad)
	# Zero padding goes AFTER the sign, space padding before it. Zero padding a
	# non-numeric conversion is undefined in C; we treat it as spaces.
	if flags["zero"] and numeric:
		return sign + "0".repeat(pad) + body
	return " ".repeat(pad) + text


static func _split_sign(rendered: String) -> Array:
	if rendered.begins_with("-"):
		return ["-", rendered.substr(1)]
	return ["", rendered]


static func _pad_left_zeros(body: String, precision: int) -> String:
	if body.length() >= precision:
		return body
	return "0".repeat(precision - body.length()) + body


## Renders |value| in `base`. Handles INT64_MIN, whose magnitude does not fit
## a signed 64-bit int, by peeling one digit off before negating.
static func _to_base_magnitude(value: int, base: int, upper: bool) -> String:
	if value >= 0:
		return _to_base(value, base, upper)
	if value > LuaNumber.INT64_MIN:
		return _to_base(-value, base, upper)
	var last: int = -(value % base)
	var rest: int = -(value / base)
	var digits := "0123456789ABCDEF" if upper else "0123456789abcdef"
	return _to_base(rest, base, upper) + digits.substr(last, 1)


static func _to_base(value: int, base: int, upper: bool) -> String:
	if value == 0:
		return "0"
	var digits := "0123456789abcdef"
	if upper:
		digits = "0123456789ABCDEF"
	var out := ""
	var remaining := value
	while remaining > 0:
		out = digits.substr(remaining % base, 1) + out
		remaining /= base
	return out


## Lua 4.0's luaI_addquoted: only '"', '\' and newline are escaped, and a NUL
## becomes the three-digit "\000". The result is a valid Lua string literal.
static func _quote(text: String) -> String:
	var out := "\""
	for i in range(text.length()):
		var c := text.unicode_at(i)
		if c == 34 or c == 92:
			out += "\\" + text.substr(i, 1)
		elif c == 10:
			out += "\\\n"
		elif c == 0:
			out += "\\000"
		else:
			out += text.substr(i, 1)
	return out + "\""


static func _number_error(position: int, value: Variant) -> String:
	return "bad argument #%d to `format' (number expected, got %s)" \
		% [position, LuaValue.type_name(value)]


static func _error(message: String) -> Dictionary:
	return {"ok": false, "text": "", "error": message}


static func _is_digit(c: int) -> bool:
	return c >= 48 and c <= 57
