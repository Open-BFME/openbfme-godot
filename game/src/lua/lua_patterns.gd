extends RefCounted

## Lua 4.0.1 pattern matching, backing strfind() and gsub().
##
## Lua patterns are NOT regular expressions: no alternation, no unbounded
## backtracking groups, no {n,m}. The 4.0 feature set implemented here is
##
##   classes     . %a %c %d %l %p %s %u %w %x %z  (uppercase = complement)
##   sets        [abc] [^abc] [a-z] with %-escapes inside
##   quantifiers *  (greedy)  +  (greedy, >=1)  -  (lazy)  ?  (optional)
##   anchors     ^ at the start of the pattern, $ at the end
##   captures    ( ... ), back-references %1 .. %9. NOT position captures:
##               "()" is a 5.0 feature; in 4.0 it is an empty capture.
##   balance     %bxy
##
## Explicitly NOT implemented, because they postdate 4.0.1:
##   %f frontier patterns (Lua 5.1) - rejected by name, not ignored.
##   %g printable-except-space class (Lua 5.2) - likewise.
##
## The matcher is a direct expression of lstrlib.c's ALGORITHM (single-char
## match plus greedy/lazy expansion with recursion for the rest of the
## pattern), written from the documented semantics rather than transliterated,
## and carrying two safeguards the C original does not need:
##   * a step counter, because a pathological pattern such as ("a"):rep(30)
##     against "(a*)*b" is exponential and this VM runs inside a sim tick;
##   * a recursion cap, because GDScript's stack is smaller than C's and a
##     blown stack is a host crash rather than a script error.
## Both surface as loud, named errors.

const MAX_CAPTURES := 32
const CAP_UNFINISHED := -1
## NOTE: there is deliberately no CAP_POSITION. Position captures - the pattern
## "()" yielding a 1-based index rather than a string - are a Lua 5.0 feature.
## 4.0's lstrlib.c has no such case: "(" followed immediately by ")" simply
## starts an ordinary capture that closes at once, so it yields the empty
## string. strfind("abc", "()") is 1, 0, "" in 4.0, not 1, 0, 1.

## Character code constants, spelled out so the matching logic reads as
## characters rather than magic numbers.
const CH_ESCAPE := 37    # %
const CH_OPEN := 40      # (
const CH_CLOSE := 41     # )
const CH_DOLLAR := 36    # $
const CH_CARET := 94     # ^
const CH_DOT := 46       # .
const CH_LBRACKET := 91  # [
const CH_RBRACKET := 93  # ]
const CH_DASH := 45      # -
const CH_STAR := 42      # *
const CH_PLUS := 43      # +
const CH_QUESTION := 63  # ?
const CH_B := 98         # b

var error_message: String = ""

var _src: String = ""
var _pattern: String = ""
var _slen: int = 0
var _plen: int = 0
var _level: int = 0
var _capture_init: PackedInt32Array = PackedInt32Array()
var _capture_len: PackedInt32Array = PackedInt32Array()
var _steps: int = 0
var _max_steps: int = 1 << 22
var _depth: int = 0
var _max_depth: int = 180


func _init(p_max_steps: int = 1 << 22) -> void:
	_max_steps = p_max_steps
	_capture_init.resize(MAX_CAPTURES)
	_capture_len.resize(MAX_CAPTURES)


func _fail(message: String) -> void:
	if error_message.is_empty():
		error_message = message


func failed() -> bool:
	return not error_message.is_empty()


## Finds `pattern` in `subject` starting at 0-based `init`.
##
## Returns null when there is no match (or on error - check failed()), else
## {"start": int, "end": int, "captures": Array} with 0-based, end-exclusive
## indices. Callers convert to Lua's 1-based inclusive convention.
func find(subject: String, pattern: String, init: int, plain: bool) -> Variant:
	error_message = ""
	_src = subject
	_pattern = pattern
	_slen = subject.length()
	_plen = pattern.length()
	_steps = 0
	_depth = 0

	if plain:
		var at := subject.find(pattern, init)
		if at < 0:
			return null
		return {"start": at, "end": at + pattern.length(), "captures": []}

	var anchored := _plen > 0 and _pattern.unicode_at(0) == CH_CARET
	var pattern_start := 1 if anchored else 0
	var s := init
	while true:
		_level = 0
		var stop := _match(s, pattern_start)
		if failed():
			return null
		if stop != -1:
			return {
				"start": s, "end": stop,
				"captures": _collect_captures(s, stop, false),
			}
		s += 1
		if anchored or s > _slen:
			return null
	return null


## Lua's gsub(). `replace` is a Callable taking (whole_match: String,
## captures: Array) and returning either a String, or null to keep the original
## match, or a LuaValue.Err-shaped abort signalled through `abort`.
##
## Returns {"text": String, "count": int} or null on error.
func gsub(subject: String, pattern: String, max_count: int, replace: Callable) -> Variant:
	error_message = ""
	_src = subject
	_pattern = pattern
	_slen = subject.length()
	_plen = pattern.length()
	_steps = 0
	_depth = 0

	var anchored := _plen > 0 and _pattern.unicode_at(0) == CH_CARET
	var pattern_start := 1 if anchored else 0
	var out := ""
	var s := 0
	var count := 0

	while count < max_count:
		_level = 0
		var stop := _match(s, pattern_start)
		if failed():
			return null
		if stop != -1:
			count += 1
			var whole := _src.substr(s, stop - s)
			var captures := _collect_captures(s, stop, false)
			var piece: Variant = replace.call(whole, captures)
			if failed():
				return null
			if piece == null:
				# "If the value returned is nil the match is kept unchanged."
				out += whole
			else:
				out += String(piece)
			if stop > s:
				s = stop
			elif s < _slen:
				# Empty match: emit one character and step past it, otherwise
				# a pattern like "x*" would spin forever on the same index.
				out += _src.substr(s, 1)
				s += 1
			else:
				break
		elif s < _slen:
			out += _src.substr(s, 1)
			s += 1
		else:
			break
		if anchored:
			break
	if s < _slen:
		out += _src.substr(s)
	return {"text": out, "count": count}


# --- core matcher ------------------------------------------------------------

## Returns the index just past the match, or -1 for no match.
func _match(s_in: int, p_in: int) -> int:
	var s := s_in
	var p := p_in
	_depth += 1
	if _depth > _max_depth:
		_depth -= 1
		_fail("pattern is too complex: matcher recursion exceeded %d levels" % _max_depth)
		return -1

	while true:
		_steps += 1
		if _steps > _max_steps:
			_fail("pattern matching exceeded %d steps and was stopped (the "
				% _max_steps + "pattern is probably exponential on this subject)")
			_depth -= 1
			return -1
		if p >= _plen:
			_depth -= 1
			return s

		var pc := _pattern.unicode_at(p)
		if pc == CH_OPEN:
			# No special case for "()" - see the CAP_POSITION note at the top.
			# 4.0 starts a normal capture here; a ")" at p + 1 is then handled by
			# the CH_CLOSE branch below on the very next step, closing it with
			# length zero and yielding "".
			var result := _start_capture(s, p + 1, CAP_UNFINISHED)
			_depth -= 1
			return result
		if pc == CH_CLOSE:
			var closed := _end_capture(s, p + 1)
			_depth -= 1
			return closed
		if pc == CH_ESCAPE and p + 1 < _plen:
			var nxt := _pattern.unicode_at(p + 1)
			if nxt == CH_B:
				var balanced := _match_balance(s, p + 2)
				if balanced == -1:
					_depth -= 1
					return -1
				s = balanced
				p += 4
				continue
			if nxt == 102:  # 'f'
				_fail("'%f' frontier patterns were added in Lua 5.1 and do not "
					+ "exist in Lua 4.0.1")
				_depth -= 1
				return -1
			if nxt == 103:  # 'g'
				_fail("'%g' character class was added in Lua 5.2 and does not "
					+ "exist in Lua 4.0.1")
				_depth -= 1
				return -1
			if nxt >= 49 and nxt <= 57:  # '1'..'9'
				var back := _match_capture(s, nxt)
				if back == -1:
					_depth -= 1
					return -1
				s = back
				p += 2
				continue
		if pc == CH_DOLLAR and p + 1 == _plen:
			_depth -= 1
			return s if s == _slen else -1

		var ep := _class_end(p)
		if failed():
			_depth -= 1
			return -1
		var single := s < _slen and _single_match(_src.unicode_at(s), p, ep)

		if ep < _plen:
			var suffix := _pattern.unicode_at(ep)
			if suffix == CH_QUESTION:
				if single:
					var attempt := _match(s + 1, ep + 1)
					if attempt != -1:
						_depth -= 1
						return attempt
				p = ep + 1
				continue
			if suffix == CH_PLUS:
				var plus := _max_expand(s + 1, p, ep) if single else -1
				_depth -= 1
				return plus
			if suffix == CH_STAR:
				var star := _max_expand(s, p, ep)
				_depth -= 1
				return star
			if suffix == CH_DASH:
				var lazy := _min_expand(s, p, ep)
				_depth -= 1
				return lazy
		if not single:
			_depth -= 1
			return -1
		s += 1
		p = ep
	_depth -= 1
	return -1


## Index just past a single character class starting at `p`.
func _class_end(p_in: int) -> int:
	var p := p_in
	if p >= _plen:
		_fail("malformed pattern (ends unexpectedly)")
		return p
	var c := _pattern.unicode_at(p)
	p += 1
	if c == CH_ESCAPE:
		if p >= _plen:
			_fail("malformed pattern (ends with '%')")
			return p
		return p + 1
	if c == CH_LBRACKET:
		if p < _plen and _pattern.unicode_at(p) == CH_CARET:
			p += 1
		# A ']' immediately after '[' or '[^' is a literal, so the scan always
		# consumes at least one character before testing for the terminator.
		while true:
			if p >= _plen:
				_fail("malformed pattern (missing ']')")
				return p
			var cc := _pattern.unicode_at(p)
			p += 1
			if cc == CH_ESCAPE and p < _plen:
				p += 1
			if p < _plen and _pattern.unicode_at(p) == CH_RBRACKET:
				return p + 1
			if p >= _plen:
				_fail("malformed pattern (missing ']')")
				return p
	return p


func _single_match(c: int, p: int, ep: int) -> bool:
	var pc := _pattern.unicode_at(p)
	if pc == CH_DOT:
		return true
	if pc == CH_ESCAPE:
		return _match_class(c, _pattern.unicode_at(p + 1))
	if pc == CH_LBRACKET:
		return _match_bracket_class(c, p, ep - 1)
	return pc == c


func _match_class(c: int, cl: int) -> bool:
	var lower := cl | 0x20 if (cl >= 65 and cl <= 90) else cl
	var result := false
	match lower:
		97: result = _is_alpha(c)                       # %a
		99: result = c < 32 or c == 127                 # %c control
		100: result = c >= 48 and c <= 57               # %d digit
		108: result = c >= 97 and c <= 122              # %l lower
		112: result = _is_punct(c)                      # %p punctuation
		115: result = _is_space(c)                      # %s space
		117: result = c >= 65 and c <= 90               # %u upper
		119: result = _is_alpha(c) or (c >= 48 and c <= 57)  # %w alphanumeric
		120: result = _is_hex(c)                        # %x hex digit
		122: result = c == 0                            # %z the zero byte
		_:
			# Not a class letter: '%' escapes the character itself, which is
			# how %., %%, %( and friends are written.
			return cl == c
	if cl >= 65 and cl <= 90:
		return not result
	return result


## `p` indexes the '[' and `ec` indexes its matching ']'. The three arms are,
## in order: a %class escape, an a-z range, and a literal character. Order
## matters - "%a-z" must read as the %a class followed by a literal '-' and
## 'z', not as a range starting at '%'.
func _match_bracket_class(c: int, p_in: int, ec: int) -> bool:
	var sig := true
	var p := p_in
	if p + 1 < _plen and _pattern.unicode_at(p + 1) == CH_CARET:
		sig = false
		p += 1
	p += 1
	while p < ec:
		var pc := _pattern.unicode_at(p)
		if pc == CH_ESCAPE:
			p += 1
			if p < _plen and _match_class(c, _pattern.unicode_at(p)):
				return sig
		elif p + 1 < _plen and _pattern.unicode_at(p + 1) == CH_DASH and p + 2 < ec:
			p += 2
			if _pattern.unicode_at(p - 2) <= c and c <= _pattern.unicode_at(p):
				return sig
		elif pc == c:
			return sig
		p += 1
	return not sig


func _max_expand(s: int, p: int, ep: int) -> int:
	var i := 0
	while s + i < _slen and _single_match(_src.unicode_at(s + i), p, ep):
		i += 1
	while i >= 0:
		_steps += 1
		if _steps > _max_steps:
			_fail("pattern matching exceeded %d steps and was stopped" % _max_steps)
			return -1
		var attempt := _match(s + i, ep + 1)
		if attempt != -1:
			return attempt
		i -= 1
	return -1


func _min_expand(s_in: int, p: int, ep: int) -> int:
	var s := s_in
	while true:
		_steps += 1
		if _steps > _max_steps:
			_fail("pattern matching exceeded %d steps and was stopped" % _max_steps)
			return -1
		var attempt := _match(s, ep + 1)
		if attempt != -1:
			return attempt
		if s < _slen and _single_match(_src.unicode_at(s), p, ep):
			s += 1
		else:
			return -1
	return -1


func _start_capture(s: int, p: int, what: int) -> int:
	if _level >= MAX_CAPTURES:
		_fail("too many captures in pattern (limit is %d)" % MAX_CAPTURES)
		return -1
	_capture_init[_level] = s
	_capture_len[_level] = what
	_level += 1
	var attempt := _match(s, p)
	if attempt == -1:
		_level -= 1
	return attempt


func _end_capture(s: int, p: int) -> int:
	var l := _capture_to_close()
	if l < 0:
		return -1
	_capture_len[l] = s - _capture_init[l]
	var attempt := _match(s, p)
	if attempt == -1:
		_capture_len[l] = CAP_UNFINISHED
	return attempt


func _capture_to_close() -> int:
	for level in range(_level - 1, -1, -1):
		if _capture_len[level] == CAP_UNFINISHED:
			return level
	_fail("invalid pattern capture (unbalanced parentheses)")
	return -1


func _match_capture(s: int, digit: int) -> int:
	var l := digit - 49  # '1' -> 0
	if l < 0 or l >= _level or _capture_len[l] == CAP_UNFINISHED:
		_fail("invalid capture index '%%%s' in pattern" % String.chr(digit))
		return -1
	var length := _capture_len[l]
	if _slen - s >= length \
			and _src.substr(_capture_init[l], length) == _src.substr(s, length):
		return s + length
	return -1


func _match_balance(s: int, p: int) -> int:
	if p + 1 >= _plen:
		_fail("malformed pattern (missing arguments to '%b')")
		return -1
	if s >= _slen or _src.unicode_at(s) != _pattern.unicode_at(p):
		return -1
	var open := _pattern.unicode_at(p)
	var close := _pattern.unicode_at(p + 1)
	var depth := 1
	var i := s + 1
	while i < _slen:
		var c := _src.unicode_at(i)
		if c == close:
			depth -= 1
			if depth == 0:
				return i + 1
		elif c == open:
			depth += 1
		i += 1
	return -1


## Builds the capture list for a completed match. With no captures, Lua returns
## the whole match only where the API asks for it (gsub replacement, and
## strfind's %0); `whole_when_empty` selects that behaviour.
func _collect_captures(s: int, e: int, whole_when_empty: bool) -> Array:
	if _level == 0:
		return [_src.substr(s, e - s)] if whole_when_empty else []
	var out: Array = []
	for i in range(_level):
		if _capture_len[i] == CAP_UNFINISHED:
			_fail("unfinished capture in pattern")
			return out
		else:
			out.append(_src.substr(_capture_init[i], _capture_len[i]))
	return out


static func _is_alpha(c: int) -> bool:
	return (c >= 65 and c <= 90) or (c >= 97 and c <= 122)


static func _is_space(c: int) -> bool:
	return c == 32 or (c >= 9 and c <= 13)


static func _is_hex(c: int) -> bool:
	return (c >= 48 and c <= 57) or (c >= 97 and c <= 102) or (c >= 65 and c <= 70)


static func _is_punct(c: int) -> bool:
	if c <= 32 or c >= 127:
		return false
	return not _is_alpha(c) and not (c >= 48 and c <= 57)
