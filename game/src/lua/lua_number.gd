extends RefCounted

## Deterministic float <-> decimal conversion for the Lua 4.0.1 VM.
##
## WHY THIS FILE EXISTS. Lua renders numbers with C's printf ("%.14g" for
## tostring, arbitrary specifiers for format()). C's printf routes through libm
## and libc, whose float->decimal rounding is NOT contractually identical
## across platforms or C runtimes, and log10()/pow() are explicitly allowed to
## differ by an ulp between implementations. A lockstep simulation cannot use
## any of that: if a script ever formats a number into a string that feeds back
## into game state, two peers must produce byte-identical text.
##
## So everything here is built from IEEE-754 +, -, *, / and comparisons only,
## which ARE exactly specified and therefore bit-identical everywhere Godot
## runs. No log10, no pow, no snprintf.
##
## The cost is that our output is not guaranteed byte-identical to glibc's
## printf in the last digit of pathological values (e.g. "%.20f" of a number
## needing 20 exact significant digits - we cap at 17, which is all a double
## carries, and zero-fill beyond). That trade is correct for this VM: we need
## agreement between peers, not agreement with a C runtime we do not ship.

## Significant digits Lua 4.0 uses for tostring() - LUA_NUMBER_FMT is "%.14g".
const TOSTRING_PRECISION := 14

## A double carries at most 17 significant decimal digits. Asking for more
## produces zeros, so we never pretend to precision the type does not have.
const MAX_SIGNIFICANT := 17

## Exact doubles for 1e0 .. 1e22. Every one of these literals is representable
## with no rounding error, which is why the ladder stops at 22.
const POW10_POS := [
	1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
	1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22,
]

## Correctly-rounded doubles for 1e-1 .. 1e-22. Written as literals rather than
## computed as 1.0/POW10_POS[k] because a literal is correctly rounded by
## definition while the reciprocal can be a ulp off.
const POW10_NEG := [
	1e0, 1e-1, 1e-2, 1e-3, 1e-4, 1e-5, 1e-6, 1e-7, 1e-8, 1e-9, 1e-10, 1e-11,
	1e-12, 1e-13, 1e-14, 1e-15, 1e-16, 1e-17, 1e-18, 1e-19, 1e-20, 1e-21, 1e-22,
]


## 10^k as a double, for any k a double can represent. Uses the exact ladder
## where possible and chunked multiplication beyond it. Deterministic: the
## chunking order is fixed, so the accumulated rounding is the same everywhere.
static func pow10(k: int) -> float:
	if k >= 0:
		if k <= 22:
			return POW10_POS[k]
		var acc := 1.0
		var rest := k
		while rest > 22:
			acc *= 1e22
			rest -= 22
		return acc * POW10_POS[rest]
	var nk := -k
	if nk <= 22:
		return POW10_NEG[nk]
	var acc_neg := 1.0
	var rest_neg := nk
	while rest_neg > 22:
		acc_neg *= 1e-22
		rest_neg -= 22
	return acc_neg * POW10_NEG[rest_neg]


## 10^k as an exact integer, for k small enough to fit a double's integer range.
static func ipow10(k: int) -> int:
	var acc := 1
	for _i in range(k):
		acc *= 10
	return acc


## Decomposes a finite non-zero magnitude into `sig` significant decimal digits.
##
## Returns {"neg": bool, "digits": String, "exp": int} where the value is
##     0.<digits> * 10^(exp + 1)      i.e.  d1.d2d3... * 10^exp
## `digits` always has exactly `sig` characters.
##
## Zero, infinity and NaN are handled by the callers (format_fixed and friends)
## because their textual forms have nothing to do with digit extraction.
static func decimal_digits(value: float, sig: int) -> Dictionary:
	var want: int = clampi(sig, 1, MAX_SIGNIFICANT)
	var neg := value < 0.0 or (value == 0.0 and is_negative_zero(value))
	var a: float = absf(value)
	if a == 0.0:
		return {"neg": neg, "digits": "0".repeat(want), "exp": 0}

	var exp := _estimate_exponent(a)
	# One scaled multiply, then correct if the estimate or the rounding pushed
	# us out of [10^(want-1), 10^want).
	var lo: int = ipow10(want - 1)
	var hi: int = ipow10(want)
	var n := 0
	for _attempt in range(4):
		var scaled: float = a * pow10(want - 1 - exp)
		n = int(_round_half_away(scaled))
		if n >= hi:
			exp += 1
			continue
		if n < lo:
			exp -= 1
			continue
		break
	if n >= hi:
		# Rounding carried into a new decimal place (e.g. 9.999 -> 10.00).
		n /= 10
		exp += 1
	var digits := str(absi(n))
	if digits.length() < want:
		digits = "0".repeat(want - digits.length()) + digits
	elif digits.length() > want:
		digits = digits.substr(0, want)
	return {"neg": neg, "digits": digits, "exp": exp}


## Brackets `a` so that 10^e <= a < 10^(e+1), using only multiplies and
## compares. May be off by one for values sitting exactly on a power of ten;
## decimal_digits() corrects that from the digit count.
static func _estimate_exponent(a: float) -> int:
	var e := 0
	var x := a
	while x >= 1e16:
		x /= 1e16
		e += 16
	while x >= 1e4:
		x /= 1e4
		e += 4
	while x >= 10.0:
		x /= 10.0
		e += 1
	while x < 1e-16:
		x *= 1e16
		e -= 16
	while x < 1e-4:
		x *= 1e4
		e -= 4
	while x < 1.0 and x > 0.0:
		x *= 10.0
		e -= 1
	return e


## Rounds half away from zero, matching printf's usual behaviour on the
## already-scaled integer. GDScript's round() is half-away-from-zero too; this
## wrapper exists so the intent is stated rather than inherited.
static func _round_half_away(v: float) -> float:
	if v >= 0.0:
		return floorf(v + 0.5)
	return ceilf(v - 0.5)


static func is_negative_zero(v: float) -> bool:
	return v == 0.0 and signf(v) < 0.0


static func _non_finite_text(v: float, upper: bool) -> String:
	var text := ""
	if is_nan(v):
		text = "nan"
	elif v > 0.0:
		text = "inf"
	else:
		text = "-inf"
	return text.to_upper() if upper else text


## printf "%.<prec>f".
static func format_fixed(value: float, prec: int) -> String:
	if is_nan(value) or is_inf(value):
		return _non_finite_text(value, false)
	var p: int = maxi(prec, 0)
	var neg := value < 0.0
	var a: float = absf(value)
	var sign_text := "-" if neg else ""

	if a == 0.0:
		return sign_text + ("0" if p == 0 else "0." + "0".repeat(p))

	# The digit budget comes from the UNROUNDED exponent bracket, not from
	# decimal_digits(a, 1). Asking for one significant digit rounds first
	# (0.999 -> "1" at exponent 0), and using that inflated exponent would ask
	# for one digit too many and then TRUNCATE the extra - printing 0.999 as
	# "0.99" instead of "1.00". The bracket is exact, so `needed` is exact.
	var needed: int = _estimate_exponent(a) + 1 + p
	if needed <= 0:
		# Every significant digit sits below the last printed place, so the
		# only question is whether the value reaches half of that place.
		if a < 0.5 * pow10(-p):
			return sign_text + ("0" if p == 0 else "0." + "0".repeat(p))
		if p == 0:
			return sign_text + "1"
		return sign_text + "0." + "0".repeat(p - 1) + "1"

	var sig: int = mini(needed, MAX_SIGNIFICANT)
	var parts := decimal_digits(a, sig)
	var digits := String(parts["digits"])
	var exp: int = int(parts["exp"])
	# Digits we asked to be dropped for exceeding double precision come back as
	# zeros, which is what printf effectively produces past digit 17 anyway.
	if needed > sig:
		digits += "0".repeat(needed - sig)

	var int_len: int = exp + 1
	var int_part := ""
	var frac_part := ""
	if int_len <= 0:
		int_part = "0"
		frac_part = "0".repeat(-int_len) + digits
	else:
		if digits.length() < int_len:
			digits += "0".repeat(int_len - digits.length())
		int_part = digits.substr(0, int_len)
		frac_part = digits.substr(int_len)
	if frac_part.length() < p:
		frac_part += "0".repeat(p - frac_part.length())
	elif frac_part.length() > p:
		frac_part = frac_part.substr(0, p)
	if p == 0:
		return sign_text + int_part
	return sign_text + int_part + "." + frac_part


## printf "%.<prec>e" / "%.<prec>E".
static func format_scientific(value: float, prec: int, upper: bool) -> String:
	if is_nan(value) or is_inf(value):
		return _non_finite_text(value, upper)
	var p: int = maxi(prec, 0)
	var sign_text := "-" if (value < 0.0 or is_negative_zero(value)) else ""
	var a: float = absf(value)
	var digits := ""
	var exp := 0
	if a == 0.0:
		digits = "0".repeat(p + 1)
	else:
		var parts := decimal_digits(a, p + 1)
		digits = String(parts["digits"])
		exp = int(parts["exp"])
		if digits.length() < p + 1:
			digits += "0".repeat(p + 1 - digits.length())
	var mantissa := digits.substr(0, 1)
	if p > 0:
		mantissa += "." + digits.substr(1, p)
	var exp_sign := "+" if exp >= 0 else "-"
	var exp_digits := str(absi(exp))
	if exp_digits.length() < 2:
		exp_digits = "0" + exp_digits
	var marker := "E" if upper else "e"
	return sign_text + mantissa + marker + exp_sign + exp_digits


## printf "%.<prec>g" / "%.<prec>G", including the trailing-zero stripping that
## makes tostring(3.0) render as "3" rather than "3.0000000000000".
static func format_general(value: float, prec: int, upper: bool, keep_zeros: bool = false) -> String:
	if is_nan(value) or is_inf(value):
		return _non_finite_text(value, upper)
	var p: int = prec if prec > 0 else 1
	p = mini(p, MAX_SIGNIFICANT)
	var exp := 0
	if value != 0.0:
		exp = int(decimal_digits(absf(value), p)["exp"])
	var text := ""
	if exp < -4 or exp >= p:
		text = format_scientific(value, p - 1, upper)
		if not keep_zeros:
			var marker := "E" if upper else "e"
			var split: int = text.find(marker)
			var head := text.substr(0, split)
			var tail := text.substr(split)
			text = _strip_trailing_zeros(head) + tail
	else:
		text = format_fixed(value, p - 1 - exp)
		if not keep_zeros:
			text = _strip_trailing_zeros(text)
	return text


static func _strip_trailing_zeros(text: String) -> String:
	if not text.contains("."):
		return text
	var out := text
	while out.ends_with("0"):
		out = out.substr(0, out.length() - 1)
	if out.ends_with("."):
		out = out.substr(0, out.length() - 1)
	return out


## Lua's tostring() for numbers: "%.14g".
static func to_lua_string(value: float) -> String:
	return format_general(value, TOSTRING_PRECISION, false)


## printf "%d" / "%i" - C truncates toward zero when converting a double to int.
static func to_c_int(value: float) -> int:
	if is_nan(value) or is_inf(value):
		return 0
	# int() on a float truncates toward zero, which is C's double->int rule.
	return int(value)


## Lua 4.0's tonumber(): decimal with optional exponent, surrounding whitespace
## allowed, plus the hex form the reference lexer inherits from strtod. Returns
## null when the text is not a number, which is exactly tonumber's nil.
static func parse(text: String, base: int = 10) -> Variant:
	var trimmed := text.strip_edges()
	if trimmed.is_empty():
		return null
	if base != 10:
		if base < 2 or base > 36:
			return null
		var negative := false
		var body := trimmed
		if body.begins_with("-"):
			negative = true
			body = body.substr(1)
		elif body.begins_with("+"):
			body = body.substr(1)
		if body.is_empty():
			return null
		var acc := 0.0
		for i in range(body.length()):
			var digit := _digit_value(body.unicode_at(i))
			if digit < 0 or digit >= base:
				return null
			acc = acc * float(base) + float(digit)
		return -acc if negative else acc
	var lower := trimmed.to_lower()
	var hex_body := lower
	var hex_negative := false
	if hex_body.begins_with("-"):
		hex_negative = true
		hex_body = hex_body.substr(1)
	elif hex_body.begins_with("+"):
		hex_body = hex_body.substr(1)
	if hex_body.begins_with("0x") and hex_body.length() > 2:
		var value := 0.0
		for i in range(2, hex_body.length()):
			var digit := _digit_value(hex_body.unicode_at(i))
			if digit < 0 or digit >= 16:
				return null
			value = value * 16.0 + float(digit)
		return -value if hex_negative else value
	if not trimmed.is_valid_float():
		return null
	return trimmed.to_float()


static func _digit_value(code: int) -> int:
	if code >= 48 and code <= 57:
		return code - 48
	if code >= 97 and code <= 122:
		return code - 97 + 10
	if code >= 65 and code <= 90:
		return code - 65 + 10
	return -1
