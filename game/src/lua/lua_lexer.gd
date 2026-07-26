extends RefCounted

## Lexer for Lua 4.0.1.
##
## Deliberate differences from a Lua 5.x lexer, all of them loud:
##   * `--[[ ... ]]` long comments do not exist in 4.0 (added in 5.0). A stock
##     4.0 lexer would treat the line as an ordinary `--` comment and then try
##     to PARSE the following lines, producing a baffling error a dozen lines
##     later. We name the construct instead.
##   * `#` is not the length operator. Only a `#!` shebang on line 1 is skipped.
##   * `::label::`, `//`, `\x` and `\z` escapes are all 5.x; each is rejected
##     by name.
##   * `%` is the UPVALUE prefix, not modulo. The lexer emits it as a symbol
##     and the parser explains the difference when it appears in binary
##     position (see LuaParser._binary_operator_error).
##
## Long strings [[ ... ]] nest, and a newline directly after the opening [[ is
## dropped, both per the 4.0 manual.

const LuaValue := preload("res://src/lua/lua_value.gd")
const LuaNumber := preload("res://src/lua/lua_number.gd")

const TK_EOF := 0
const TK_NAME := 1
const TK_NUMBER := 2
const TK_STRING := 3
const TK_KEYWORD := 4
const TK_SYMBOL := 5

const KEYWORDS := [
	"and", "break", "do", "else", "elseif", "end", "for", "function", "if",
	"in", "local", "nil", "not", "or", "repeat", "return", "then", "until",
	"while",
]

## Longest-match-first, so "..." beats ".." beats "." and "==" beats "=".
const SYMBOLS := [
	"...", "..", "==", "~=", "<=", ">=",
	"+", "-", "*", "/", "^", "=", "<", ">", "(", ")", "{", "}", "[", "]",
	";", ":", ",", ".", "%",
]

var chunk: String = "?"
var error: LuaValue.Err = null

var _src: String = ""
var _pos: int = 0
var _line: int = 1
var _len: int = 0


func _init(p_chunk: String = "?") -> void:
	chunk = p_chunk


## Tokenises the whole source. Returns an Array of token dictionaries
## {"t": kind, "v": value, "line": line}, or null with `error` set.
func tokenize(source: String) -> Variant:
	_src = source
	_pos = 0
	_line = 1
	_len = source.length()
	error = null
	var tokens: Array = []

	# A leading "#!..." line is dropped, matching the reference lua binary.
	if _len >= 1 and _src.unicode_at(0) == 35:  # '#'
		while _pos < _len and _src.unicode_at(_pos) != 10:
			_pos += 1

	while true:
		if not _skip_space_and_comments():
			return null
		if _pos >= _len:
			tokens.append({"t": TK_EOF, "v": "<eof>", "line": _line})
			return tokens
		var token: Variant = _next_token()
		if token == null:
			return null
		tokens.append(token)
	# Unreachable: the loop only exits by returning. GDScript's analyser does
	# not treat `while true` as diverging, so it needs a trailing return.
	return tokens


func _fail(message: String, kind: String = LuaValue.Err.KIND_SYNTAX) -> void:
	if error == null:
		error = LuaValue.Err.new(kind, message, chunk, _line)


func _peek(offset: int = 0) -> int:
	var at := _pos + offset
	if at >= _len:
		return -1
	return _src.unicode_at(at)


func _matches(text: String) -> bool:
	if _pos + text.length() > _len:
		return false
	return _src.substr(_pos, text.length()) == text


## Returns false only when a rejected comment form was found.
func _skip_space_and_comments() -> bool:
	while _pos < _len:
		var c := _peek()
		if c == 10:  # \n
			_line += 1
			_pos += 1
		elif c == 32 or c == 9 or c == 13 or c == 11 or c == 12:
			_pos += 1
		elif c == 45 and _peek(1) == 45:  # "--"
			if _matches("--[["):
				_fail("long comment '--[[' is a Lua 5.0 construct; Lua 4.0.1 "
					+ "comments run to the end of the line only")
				return false
			while _pos < _len and _peek() != 10:
				_pos += 1
		else:
			return true
	return true


func _next_token() -> Variant:
	var line := _line
	var c := _peek()

	if _is_name_start(c):
		var start := _pos
		while _pos < _len and _is_name_part(_peek()):
			_pos += 1
		var word := _src.substr(start, _pos - start)
		if KEYWORDS.has(word):
			return {"t": TK_KEYWORD, "v": word, "line": line}
		return {"t": TK_NAME, "v": word, "line": line}

	if _is_digit(c) or (c == 46 and _is_digit(_peek(1))):  # digit or ".5"
		return _read_number(line)

	if c == 34 or c == 39:  # " or '
		return _read_quoted_string(line)

	if _matches("[["):
		return _read_long_string(line)

	if c == 35:  # '#'
		_fail("'#' is not an operator in Lua 4.0.1 (the '#' length operator "
			+ "was added in Lua 5.1); use getn(t) for tables and strlen(s) "
			+ "for strings")
		return null
	if _matches("::"):
		_fail("'::label::' is a Lua 5.2 construct and has no Lua 4.0.1 equivalent")
		return null
	if _matches("//"):
		_fail("'//' floor division is a Lua 5.3 construct; Lua 4.0.1 has only '/'")
		return null

	for symbol in SYMBOLS:
		if _matches(symbol):
			_pos += symbol.length()
			return {"t": TK_SYMBOL, "v": symbol, "line": line}

	_fail("unexpected character '%s'" % _src.substr(_pos, 1))
	return null


func _read_number(line: int) -> Variant:
	var start := _pos
	if _matches("0x") or _matches("0X"):
		_pos += 2
		while _pos < _len and _is_hex_digit(_peek()):
			_pos += 1
	else:
		while _pos < _len and _is_digit(_peek()):
			_pos += 1
		if _peek() == 46:  # '.'
			_pos += 1
			while _pos < _len and _is_digit(_peek()):
				_pos += 1
		if _peek() == 101 or _peek() == 69:  # e E
			_pos += 1
			if _peek() == 43 or _peek() == 45:
				_pos += 1
			if not _is_digit(_peek()):
				_fail("malformed number near '%s'" % _src.substr(start, _pos - start))
				return null
			while _pos < _len and _is_digit(_peek()):
				_pos += 1
	var text := _src.substr(start, _pos - start)
	# A name character glued to a number ("3abc") is malformed, not two tokens.
	if _pos < _len and _is_name_part(_peek()):
		while _pos < _len and _is_name_part(_peek()):
			_pos += 1
		_fail("malformed number near '%s'" % _src.substr(start, _pos - start))
		return null
	var value: Variant = LuaNumber.parse(text)
	if value == null:
		_fail("malformed number near '%s'" % text)
		return null
	return {"t": TK_NUMBER, "v": float(value), "line": line}


func _read_quoted_string(line: int) -> Variant:
	var quote := _peek()
	_pos += 1
	var out := ""
	while true:
		if _pos >= _len:
			_fail("unfinished string")
			return null
		var c := _peek()
		if c == quote:
			_pos += 1
			break
		if c == 10:
			_fail("unfinished string (newline inside a quoted string)")
			return null
		if c != 92:  # not backslash
			out += _src.substr(_pos, 1)
			_pos += 1
			continue
		_pos += 1
		var esc := _peek()
		match esc:
			97: out += String.chr(7); _pos += 1    # bell
			98: out += String.chr(8); _pos += 1    # backspace
			102: out += String.chr(12); _pos += 1  # form feed
			110: out += String.chr(10); _pos += 1  # newline
			114: out += String.chr(13); _pos += 1  # carriage return
			116: out += String.chr(9); _pos += 1   # tab
			118: out += String.chr(11); _pos += 1  # vertical tab
			92: out += "\\"; _pos += 1
			34: out += "\""; _pos += 1
			39: out += "'"; _pos += 1
			10:                              # backslash-newline continuation
				out += "\n"
				_line += 1
				_pos += 1
			120, 88:  # x X
				_fail("'\\x' hex escapes were added in Lua 5.2; Lua 4.0.1 "
					+ "supports only '\\ddd' decimal escapes")
				return null
			122:  # z
				_fail("'\\z' whitespace escapes were added in Lua 5.2 and do "
					+ "not exist in Lua 4.0.1")
				return null
			_:
				if _is_digit(esc):
					var code := 0
					var digits := 0
					while digits < 3 and _is_digit(_peek()):
						code = code * 10 + (_peek() - 48)
						_pos += 1
						digits += 1
					if code > 255:
						_fail("escape sequence too large ('\\%d' exceeds 255)" % code)
						return null
					out += String.chr(code)
				else:
					_fail("invalid escape sequence '\\%s'" % _src.substr(_pos, 1))
					return null
	return {"t": TK_STRING, "v": out, "line": line}


## Long strings nest: [[ a [[ b ]] c ]] is one string containing "a [[ b ]] c".
func _read_long_string(line: int) -> Variant:
	_pos += 2
	var depth := 1
	# "If the first character after [[ is a newline it is not included."
	if _peek() == 10:
		_line += 1
		_pos += 1
	var out := ""
	while true:
		if _pos >= _len:
			_fail("unfinished long string")
			return null
		if _matches("[["):
			depth += 1
			out += "[["
			_pos += 2
			continue
		if _matches("]]"):
			depth -= 1
			_pos += 2
			if depth == 0:
				break
			out += "]]"
			continue
		if _peek() == 10:
			_line += 1
		out += _src.substr(_pos, 1)
		_pos += 1
	return {"t": TK_STRING, "v": out, "line": line}


static func _is_digit(c: int) -> bool:
	return c >= 48 and c <= 57


static func _is_hex_digit(c: int) -> bool:
	return _is_digit(c) or (c >= 97 and c <= 102) or (c >= 65 and c <= 70)


static func _is_name_start(c: int) -> bool:
	return c == 95 or (c >= 97 and c <= 122) or (c >= 65 and c <= 90)


static func _is_name_part(c: int) -> bool:
	return _is_name_start(c) or _is_digit(c)
