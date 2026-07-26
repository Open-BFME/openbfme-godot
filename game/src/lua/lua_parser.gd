extends RefCounted

## Recursive-descent parser for Lua 4.0.1, producing a Dictionary AST.
##
## PRECEDENCE IS 4.0's, NOT 5.x's. Taken verbatim from lua-4.0.1 lparser.c:
##
##     static const struct { char left; char right; } priority[] = {
##        {5, 5}, {5, 5}, {6, 6}, {6, 6},   /* + - * /            */
##        {9, 8}, {4, 3},                   /* ^ and .. (right)   */
##        {2, 2}, {2, 2},                   /* == ~=              */
##        {2, 2}, {2, 2}, {2, 2}, {2, 2},   /* < <= > >=          */
##        {1, 1}, {1, 1}                    /* and or             */
##     };
##     #define UNARY_PRIORITY 7
##
## Two consequences catch people out, and both are reproduced here on purpose:
##
##   1. `and` and `or` share priority 1 and are LEFT associative, so in Lua 4.0
##      `a or b and c` parses as `(a or b) and c`. Lua 5.0 split them (and=2,
##      or=1) which flips that to `a or (b and c)`. Scripts written for 4.01
##      were compiled against the 4.0 reading, so we must match 4.0.
##   2. Comparison and equality share priority 2, so `a < b == c` parses as
##      `(a < b) == c`.
##
## Constructs this parser REFUSES, each by name and source position, rather
## than accepting a plausible-looking approximation:
##   * `local function f() end`      - Lua 5.0
##   * `...` used as an expression   - Lua 5.1 (4.0 exposes varargs as `arg`)
##   * `goto` / labels               - Lua 5.2
##   * `a % b` as modulo             - `%` is 4.0's upvalue prefix
##   * assignment to an upvalue      - upvalues are frozen by definition
##   * `%x` outside a function body  - nothing to capture from
##   * `{1, 2, x = 3}`               - 4.0 requires `{1, 2; x = 3}`
##   * `break` not last in its block - 4.0 requires it to end the block

const LuaLexer := preload("res://src/lua/lua_lexer.gd")
const LuaValue := preload("res://src/lua/lua_value.gd")

# --- expression node kinds ---------------------------------------------------
const E_NIL := 0
const E_NUMBER := 1
const E_STRING := 2
const E_NAME := 3
const E_UPVALUE := 4
const E_INDEX := 5
const E_CALL := 6
const E_METHCALL := 7
const E_FUNCTION := 8
const E_TABLE := 9
const E_BINOP := 10
const E_UNOP := 11
const E_AND := 12
const E_OR := 13
const E_PAREN := 14

# --- statement node kinds ----------------------------------------------------
const S_LOCAL := 100
const S_ASSIGN := 101
const S_CALL := 102
const S_DO := 103
const S_WHILE := 104
const S_REPEAT := 105
const S_IF := 106
const S_FORNUM := 107
const S_FORIN := 108
const S_RETURN := 109
const S_BREAK := 110

const UNARY_PRIORITY := 7

## op -> [left priority, right priority], exactly lparser.c's table.
const BINARY_PRIORITY := {
	"+": [5, 5], "-": [5, 5], "*": [6, 6], "/": [6, 6],
	"^": [9, 8], "..": [4, 3],
	"==": [2, 2], "~=": [2, 2],
	"<": [2, 2], "<=": [2, 2], ">": [2, 2], ">=": [2, 2],
	"and": [1, 1], "or": [1, 1],
}

var chunk: String = "?"
var error: LuaValue.Err = null

var _tokens: Array = []
var _at: int = 0
## Depth of enclosing `function` bodies. 0 means the main chunk, where `%name`
## has nothing to capture from.
var _function_depth: int = 0


func _init(p_chunk: String = "?") -> void:
	chunk = p_chunk


## Parses a whole chunk. Returns the main function node (E_FUNCTION with a
## vararg parameter list, as Lua treats a chunk), or null with `error` set.
func parse(source: String) -> Variant:
	error = null
	var lexer := LuaLexer.new(chunk)
	var tokens: Variant = lexer.tokenize(source)
	if tokens == null:
		error = lexer.error
		return null
	_tokens = tokens
	_at = 0
	_function_depth = 0
	var body: Variant = _parse_block()
	if body == null:
		return null
	if not _at_eof():
		_unexpected("'<eof>' expected")
		return null
	return {
		"k": E_FUNCTION, "line": 1, "params": PackedStringArray(),
		"is_vararg": true, "body": body, "upvalues": PackedStringArray(),
		"name": "main chunk",
	}


# --- token helpers -----------------------------------------------------------

func _tok() -> Dictionary:
	return _tokens[_at]


func _line() -> int:
	return int(_tokens[_at]["line"])


func _at_eof() -> bool:
	return int(_tokens[_at]["t"]) == LuaLexer.TK_EOF


func _is_symbol(text: String) -> bool:
	var t: Dictionary = _tokens[_at]
	return int(t["t"]) == LuaLexer.TK_SYMBOL and String(t["v"]) == text


func _is_keyword(text: String) -> bool:
	var t: Dictionary = _tokens[_at]
	return int(t["t"]) == LuaLexer.TK_KEYWORD and String(t["v"]) == text


func _is_name() -> bool:
	return int(_tokens[_at]["t"]) == LuaLexer.TK_NAME


func _advance() -> Dictionary:
	var t: Dictionary = _tokens[_at]
	if _at < _tokens.size() - 1:
		_at += 1
	return t


func _accept_symbol(text: String) -> bool:
	if _is_symbol(text):
		_advance()
		return true
	return false


func _accept_keyword(text: String) -> bool:
	if _is_keyword(text):
		_advance()
		return true
	return false


func _expect_symbol(text: String) -> bool:
	if _accept_symbol(text):
		return true
	_unexpected("'%s' expected" % text)
	return false


func _expect_keyword(text: String) -> bool:
	if _accept_keyword(text):
		return true
	_unexpected("'%s' expected" % text)
	return false


func _expect_name() -> Variant:
	if not _is_name():
		_unexpected("<name> expected")
		return null
	return String(_advance()["v"])


func _token_text() -> String:
	var t: Dictionary = _tokens[_at]
	if int(t["t"]) == LuaLexer.TK_EOF:
		return "<eof>"
	return String(t["v"])


func _fail(message: String, kind: String = LuaValue.Err.KIND_SYNTAX, line: int = -1) -> void:
	if error == null:
		error = LuaValue.Err.new(kind, message, chunk, line if line >= 0 else _line())


func _unexpected(expectation: String) -> void:
	_fail("%s near '%s'" % [expectation, _token_text()])


# --- blocks and statements ---------------------------------------------------

## A block is a list of statements. Returns Array, or null on error.
func _parse_block() -> Variant:
	var statements: Array = []
	while true:
		if error != null:
			return null
		if _block_follows():
			return statements
		var terminates := _is_keyword("return") or _is_keyword("break")
		var statement: Variant = _parse_statement()
		if statement == null:
			return null
		if statement is Dictionary:
			statements.append(statement)
		while _accept_symbol(";"):
			pass
		if terminates:
			# Lua 4.0's stat() returns 1 for return/break: both must close the
			# block. Anything after them is unreachable and 4.0 rejects it.
			if not _block_follows():
				_fail("'%s' must be the last statement in its block" %
					("return" if int(statement["k"]) == S_RETURN else "break"))
				return null
			return statements
	# Unreachable: the loop above only exits by returning. GDScript's analyser
	# does not treat `while true` as diverging, so it needs this.
	return statements


func _block_follows() -> bool:
	if _at_eof():
		return true
	return _is_keyword("end") or _is_keyword("else") or _is_keyword("elseif") \
		or _is_keyword("until")


func _parse_statement() -> Variant:
	var line := _line()

	if _is_keyword("if"):
		return _parse_if()
	if _is_keyword("while"):
		return _parse_while()
	if _is_keyword("do"):
		_advance()
		var body: Variant = _parse_block()
		if body == null or not _expect_keyword("end"):
			return null
		return {"k": S_DO, "line": line, "body": body}
	if _is_keyword("for"):
		return _parse_for()
	if _is_keyword("repeat"):
		return _parse_repeat()
	if _is_keyword("function"):
		return _parse_function_statement()
	if _is_keyword("local"):
		return _parse_local()
	if _is_keyword("return"):
		_advance()
		var exprs: Array = []
		if not _block_follows() and not _is_symbol(";"):
			var parsed: Variant = _parse_expression_list()
			if parsed == null:
				return null
			exprs = parsed
		return {"k": S_RETURN, "line": line, "exprs": exprs}
	if _is_keyword("break"):
		_advance()
		return {"k": S_BREAK, "line": line}

	# `goto` is an ordinary identifier in 4.0, so a 5.2 jump would parse as two
	# expressions and produce a confusing error. Name it instead.
	if _is_name() and String(_tok()["v"]) == "goto":
		var after: Dictionary = _tokens[mini(_at + 1, _tokens.size() - 1)]
		if int(after["t"]) == LuaLexer.TK_NAME:
			_fail("'goto' is a Lua 5.2 construct and does not exist in Lua 4.0.1",
				LuaValue.Err.KIND_UNSUPPORTED)
			return null

	return _parse_expression_statement()


func _parse_if() -> Variant:
	var line := _line()
	_advance()
	var clauses: Array = []
	var condition: Variant = _parse_expression()
	if condition == null or not _expect_keyword("then"):
		return null
	var body: Variant = _parse_block()
	if body == null:
		return null
	clauses.append([condition, body])
	while _is_keyword("elseif"):
		_advance()
		var elseif_condition: Variant = _parse_expression()
		if elseif_condition == null or not _expect_keyword("then"):
			return null
		var elseif_body: Variant = _parse_block()
		if elseif_body == null:
			return null
		clauses.append([elseif_condition, elseif_body])
	var else_body: Variant = null
	if _accept_keyword("else"):
		else_body = _parse_block()
		if else_body == null:
			return null
	if not _expect_keyword("end"):
		return null
	return {"k": S_IF, "line": line, "clauses": clauses, "else_body": else_body}


func _parse_while() -> Variant:
	var line := _line()
	_advance()
	var condition: Variant = _parse_expression()
	if condition == null or not _expect_keyword("do"):
		return null
	var body: Variant = _parse_block()
	if body == null or not _expect_keyword("end"):
		return null
	return {"k": S_WHILE, "line": line, "cond": condition, "body": body}


func _parse_repeat() -> Variant:
	var line := _line()
	_advance()
	var body: Variant = _parse_block()
	if body == null or not _expect_keyword("until"):
		return null
	var condition: Variant = _parse_expression()
	if condition == null:
		return null
	return {"k": S_REPEAT, "line": line, "body": body, "cond": condition}


## Both 4.0 for forms:
##     for name = start, stop [, step] do block end
##     for key, value in table do block end
## The generic form iterates a TABLE, not an iterator triple - `for k,v in
## pairs(t)` is Lua 5.0 and is rejected at runtime because `pairs` is a
## tripwire.
func _parse_for() -> Variant:
	var line := _line()
	_advance()
	var first: Variant = _expect_name()
	if first == null:
		return null
	if _accept_symbol("="):
		var start_expr: Variant = _parse_expression()
		if start_expr == null or not _expect_symbol(","):
			return null
		var stop_expr: Variant = _parse_expression()
		if stop_expr == null:
			return null
		var step_expr: Variant = null
		if _accept_symbol(","):
			step_expr = _parse_expression()
			if step_expr == null:
				return null
		if not _expect_keyword("do"):
			return null
		var numeric_body: Variant = _parse_block()
		if numeric_body == null or not _expect_keyword("end"):
			return null
		return {
			"k": S_FORNUM, "line": line, "var": first, "start": start_expr,
			"stop": stop_expr, "step": step_expr, "body": numeric_body,
		}
	if not _expect_symbol(","):
		return null
	var value_name: Variant = _expect_name()
	if value_name == null:
		return null
	if not _expect_keyword("in"):
		return null
	var subject: Variant = _parse_expression()
	if subject == null:
		return null
	# `for k, v in next, t do` is the Lua 5.0 iterator-triple form. Left alone
	# it produces "'do' expected near ','", which tells a modder nothing about
	# WHY their loop is wrong.
	if _is_symbol(","):
		_fail("'for ... in explist' (the iterator/state/control triple, as in "
			+ "`for k, v in next, t do`) was introduced in Lua 5.0; Lua 4.0.1's "
			+ "generic for takes a single TABLE - write `for k, v in t do`",
			LuaValue.Err.KIND_UNSUPPORTED)
		return null
	if not _expect_keyword("do"):
		return null
	var generic_body: Variant = _parse_block()
	if generic_body == null or not _expect_keyword("end"):
		return null
	return {
		"k": S_FORIN, "line": line, "key_var": first, "value_var": value_name,
		"subject": subject, "body": generic_body,
	}


## `function a.b.c:d(...) end` desugars to an assignment of a function
## expression, with `self` prepended for the `:` form.
func _parse_function_statement() -> Variant:
	var line := _line()
	_advance()
	var base_name: Variant = _expect_name()
	if base_name == null:
		return null
	var target: Dictionary = {"k": E_NAME, "line": line, "name": base_name}
	var pretty := String(base_name)
	var is_method := false
	while _is_symbol(".") or _is_symbol(":"):
		var separator := String(_tok()["v"])
		_advance()
		var field: Variant = _expect_name()
		if field == null:
			return null
		pretty += separator + String(field)
		target = {
			"k": E_INDEX, "line": line, "obj": target,
			"key": {"k": E_STRING, "line": line, "v": String(field)},
		}
		if separator == ":":
			is_method = true
			break
	var body: Variant = _parse_function_body(line, is_method, pretty)
	if body == null:
		return null
	return {"k": S_ASSIGN, "line": line, "targets": [target], "exprs": [body]}


func _parse_local() -> Variant:
	var line := _line()
	_advance()
	if _is_keyword("function"):
		_fail("'local function' is a Lua 5.0 construct; Lua 4.0.1 has no local "
			+ "function statement (write `local f` then `f = function() ... end`, "
			+ "noting that the local is NOT visible inside the body - Lua 4.0 "
			+ "closures capture only through %upvalues)",
			LuaValue.Err.KIND_UNSUPPORTED)
		return null
	var names := PackedStringArray()
	while true:
		var name: Variant = _expect_name()
		if name == null:
			return null
		names.append(String(name))
		if not _accept_symbol(","):
			break
	var exprs: Array = []
	if _accept_symbol("="):
		var parsed: Variant = _parse_expression_list()
		if parsed == null:
			return null
		exprs = parsed
	return {"k": S_LOCAL, "line": line, "names": names, "exprs": exprs}


## Either a call used as a statement, or an assignment.
func _parse_expression_statement() -> Variant:
	var line := _line()
	var first: Variant = _parse_suffixed_expression()
	if first == null:
		return null
	if _is_symbol("=") or _is_symbol(","):
		var targets: Array = [first]
		while _accept_symbol(","):
			var extra: Variant = _parse_suffixed_expression()
			if extra == null:
				return null
			targets.append(extra)
		if not _expect_symbol("="):
			return null
		var exprs: Variant = _parse_expression_list()
		if exprs == null:
			return null
		for target in targets:
			var kind := int(target["k"])
			if kind == E_UPVALUE:
				_fail("cannot assign to upvalue '%%%s': Lua 4.0 upvalues are "
					% target["name"] + "frozen at closure creation and are read-only",
					LuaValue.Err.KIND_UNSUPPORTED, line)
				return null
			if kind != E_NAME and kind != E_INDEX:
				_fail("cannot assign to this expression (not a variable)",
					LuaValue.Err.KIND_SYNTAX, line)
				return null
		return {"k": S_ASSIGN, "line": line, "targets": targets, "exprs": exprs}
	var call_kind := int(first["k"])
	if call_kind != E_CALL and call_kind != E_METHCALL:
		_unexpected("syntax error: expression statement must be a call or an assignment")
		return null
	return {"k": S_CALL, "line": line, "call": first}


# --- expressions -------------------------------------------------------------

func _parse_expression_list() -> Variant:
	var exprs: Array = []
	while true:
		var expression: Variant = _parse_expression()
		if expression == null:
			return null
		exprs.append(expression)
		if not _accept_symbol(","):
			break
	return exprs


func _parse_expression() -> Variant:
	return _parse_subexpression(0)


## Precedence climbing over lparser.c's priority table.
func _parse_subexpression(limit: int) -> Variant:
	var left: Variant = null
	var line := _line()
	if _is_keyword("not") or _is_symbol("-"):
		var unary_op := String(_tok()["v"])
		_advance()
		var operand: Variant = _parse_subexpression(UNARY_PRIORITY)
		if operand == null:
			return null
		left = {"k": E_UNOP, "line": line, "op": unary_op, "e": operand}
	else:
		left = _parse_simple_expression()
		if left == null:
			return null

	while true:
		# `%` reaching infix position can only be a 5.x-style modulo: the
		# prefix (upvalue) use is consumed by _parse_simple_expression.
		if _is_symbol("%"):
			_fail("'%' is the Lua 4.0 upvalue prefix, not a modulo operator; "
				+ "Lua 4.0.1 has no '%' operator - use mod(a, b)",
				LuaValue.Err.KIND_UNSUPPORTED)
			return null
		var op := _binary_operator()
		if op == "":
			break
		var priorities: Array = BINARY_PRIORITY[op]
		if int(priorities[0]) <= limit:
			break
		var op_line := _line()
		_advance()
		var right: Variant = _parse_subexpression(int(priorities[1]))
		if right == null:
			return null
		if op == "and":
			left = {"k": E_AND, "line": op_line, "l": left, "r": right}
		elif op == "or":
			left = {"k": E_OR, "line": op_line, "l": left, "r": right}
		else:
			left = {"k": E_BINOP, "line": op_line, "op": op, "l": left, "r": right}
	return left


func _binary_operator() -> String:
	var t: Dictionary = _tokens[_at]
	var kind := int(t["t"])
	if kind != LuaLexer.TK_SYMBOL and kind != LuaLexer.TK_KEYWORD:
		return ""
	var text := String(t["v"])
	if BINARY_PRIORITY.has(text):
		return text
	return ""


func _parse_simple_expression() -> Variant:
	var line := _line()
	var t: Dictionary = _tokens[_at]
	match int(t["t"]):
		LuaLexer.TK_NUMBER:
			_advance()
			return {"k": E_NUMBER, "line": line, "v": float(t["v"])}
		LuaLexer.TK_STRING:
			_advance()
			return {"k": E_STRING, "line": line, "v": String(t["v"])}
		LuaLexer.TK_KEYWORD:
			match String(t["v"]):
				"nil":
					_advance()
					return {"k": E_NIL, "line": line}
				"function":
					_advance()
					return _parse_function_body(line, false, "anonymous")
	if _is_symbol("{"):
		return _parse_table_constructor()
	if _is_symbol("..."):
		_fail("'...' as an expression is a Lua 5.1 construct; Lua 4.0.1 "
			+ "exposes a vararg function's extra arguments through the local "
			+ "table `arg` (arg.n, arg[1], ...)",
			LuaValue.Err.KIND_UNSUPPORTED)
		return null
	return _parse_suffixed_expression()


## primary := NAME | '%' NAME | '(' expr ')'
func _parse_primary_expression() -> Variant:
	var line := _line()
	if _is_name():
		return {"k": E_NAME, "line": line, "name": String(_advance()["v"])}
	if _is_symbol("%"):
		_advance()
		var name: Variant = _expect_name()
		if name == null:
			return null
		if _function_depth == 0:
			_fail("cannot access upvalue '%%%s' at top level: %%name captures a "
				% name + "variable of the ENCLOSING function, and the main "
				+ "chunk has no enclosing function",
				LuaValue.Err.KIND_SYNTAX, line)
			return null
		return {"k": E_UPVALUE, "line": line, "name": String(name)}
	if _accept_symbol("("):
		var inner: Variant = _parse_expression()
		if inner == null or not _expect_symbol(")"):
			return null
		# Parentheses truncate a multiple-value expression to exactly one.
		return {"k": E_PAREN, "line": line, "e": inner}
	_unexpected("unexpected symbol")
	return null


func _parse_suffixed_expression() -> Variant:
	var expression: Variant = _parse_primary_expression()
	if expression == null:
		return null
	while true:
		var line := _line()
		if _accept_symbol("."):
			var field: Variant = _expect_name()
			if field == null:
				return null
			expression = {
				"k": E_INDEX, "line": line, "obj": expression,
				"key": {"k": E_STRING, "line": line, "v": String(field)},
			}
		elif _accept_symbol("["):
			var key: Variant = _parse_expression()
			if key == null or not _expect_symbol("]"):
				return null
			expression = {"k": E_INDEX, "line": line, "obj": expression, "key": key}
		elif _accept_symbol(":"):
			var method: Variant = _expect_name()
			if method == null:
				return null
			var method_args: Variant = _parse_call_arguments()
			if method_args == null:
				return null
			expression = {
				"k": E_METHCALL, "line": line, "obj": expression,
				"method": String(method), "args": method_args,
			}
		elif _is_symbol("(") or _is_symbol("{") \
				or int(_tok()["t"]) == LuaLexer.TK_STRING:
			var args: Variant = _parse_call_arguments()
			if args == null:
				return null
			expression = {"k": E_CALL, "line": line, "fn": expression, "args": args}
		else:
			return expression
	return expression


## args := '(' [explist] ')' | tableconstructor | string
func _parse_call_arguments() -> Variant:
	var line := _line()
	if int(_tok()["t"]) == LuaLexer.TK_STRING:
		return [{"k": E_STRING, "line": line, "v": String(_advance()["v"])}]
	if _is_symbol("{"):
		var constructor: Variant = _parse_table_constructor()
		if constructor == null:
			return null
		return [constructor]
	if not _expect_symbol("("):
		return null
	if _accept_symbol(")"):
		return []
	var args: Variant = _parse_expression_list()
	if args == null or not _expect_symbol(")"):
		return null
	return args


## Lua 4.0's constructor grammar is strictly
##     '{' [ lfieldlist [ ';' ffieldlist ] | ffieldlist ] '}'
## so positional and keyed fields are separated by ';', never by ','. The 5.0
## mixed form `{1, 2, x = 3}` is a syntax error in 4.0, and we say so rather
## than quietly accepting it - a script relying on 5.0 constructor syntax is
## almost certainly relying on other 5.0 behaviour too.
func _parse_table_constructor() -> Variant:
	var line := _line()
	if not _expect_symbol("{"):
		return null
	var array_items: Array = []
	var hash_items: Array = []
	var in_keyed_section := _starts_keyed_field()

	# 4.0's grammar is
	#   fieldlist -> lfieldlist | ffieldlist
	#              | lfieldlist ';' ffieldlist | ffieldlist ';' lfieldlist
	# so BOTH orders are legal. lparser.c's constructor() parses one part, then
	# after an optional ';' parses the other, requiring only that the two differ
	# in kind ("invalid constructor syntax" otherwise). {x = 1; 1, 2} is valid
	# Lua 4.0 and used to be rejected here.
	if not _is_symbol("}"):
		if in_keyed_section:
			if not _parse_keyed_fields(hash_items):
				return null
			if _accept_symbol(";") and not _is_symbol("}"):
				if _starts_keyed_field():
					_fail("Lua 4.0.1 table constructors take at most two "
						+ "sections and they must differ in kind - "
						+ "{1, 2; x = 3} or {x = 3; 1, 2}, never two keyed "
						+ "sections")
					return null
				if not _parse_positional_fields(array_items):
					return null
		else:
			if not _parse_positional_fields(array_items):
				return null
			if _accept_symbol(";") and not _is_symbol("}"):
				if not _starts_keyed_field():
					_fail("Lua 4.0.1 table constructors take at most two "
						+ "sections and they must differ in kind - "
						+ "{1, 2; x = 3} or {x = 3; 1, 2}, never two "
						+ "positional sections")
					return null
				if not _parse_keyed_fields(hash_items):
					return null
	if not _expect_symbol("}"):
		return null
	return {
		"k": E_TABLE, "line": line, "array": array_items, "hash": hash_items,
	}


func _starts_keyed_field() -> bool:
	if _is_symbol("["):
		return true
	if not _is_name():
		return false
	var after: Dictionary = _tokens[mini(_at + 1, _tokens.size() - 1)]
	return int(after["t"]) == LuaLexer.TK_SYMBOL and String(after["v"]) == "="


func _parse_positional_fields(into: Array) -> bool:
	while true:
		if _is_symbol("}") or _is_symbol(";"):
			return true
		if _starts_keyed_field():
			_fail("Lua 4.0.1 table constructors separate positional and keyed "
				+ "fields with ';', not ',' - write {1, 2; x = 3}. The mixed "
				+ "form {1, 2, x = 3} was introduced in Lua 5.0",
				LuaValue.Err.KIND_UNSUPPORTED)
			return false
		var item: Variant = _parse_expression()
		if item == null:
			return false
		into.append(item)
		if not _accept_symbol(","):
			return true
	return true


func _parse_keyed_fields(into: Array) -> bool:
	while true:
		if _is_symbol("}"):
			return true
		var line := _line()
		var key: Variant = null
		if _accept_symbol("["):
			key = _parse_expression()
			if key == null or not _expect_symbol("]"):
				return false
		else:
			var name: Variant = _expect_name()
			if name == null:
				return false
			key = {"k": E_STRING, "line": line, "v": String(name)}
		if not _expect_symbol("="):
			return false
		var value: Variant = _parse_expression()
		if value == null:
			return false
		into.append([key, value])
		if not _accept_symbol(","):
			return true
	return true


## Parses `(params) body end` and collects the %upvalue names its body
## references, so the interpreter can snapshot them at closure-creation time.
func _parse_function_body(line: int, is_method: bool, pretty_name: String) -> Variant:
	if not _expect_symbol("("):
		return null
	var params := PackedStringArray()
	if is_method:
		params.append("self")
	var is_vararg := false
	if not _is_symbol(")"):
		while true:
			if _accept_symbol("..."):
				is_vararg = true
				break
			var name: Variant = _expect_name()
			if name == null:
				return null
			params.append(String(name))
			if not _accept_symbol(","):
				break
	if not _expect_symbol(")"):
		return null

	_function_depth += 1
	var body: Variant = _parse_block()
	_function_depth -= 1
	if body == null:
		return null
	if not _expect_keyword("end"):
		return null

	var node := {
		"k": E_FUNCTION, "line": line, "params": params, "is_vararg": is_vararg,
		"body": body, "name": pretty_name,
	}
	node["upvalues"] = _collect_upvalue_names(body)
	return node


## Gathers every `%name` that belongs to THIS function body - that is, not the
## ones inside a nested function literal. A nested closure resolves its own
## upvalues when IT is created, against the frame of the function that creates
## it, which is exactly this function's frame at run time.
func _collect_upvalue_names(node: Variant) -> PackedStringArray:
	var found := PackedStringArray()
	_walk_for_upvalues(node, found)
	return found


func _walk_for_upvalues(node: Variant, found: PackedStringArray) -> void:
	if node is Array:
		for item in node:
			_walk_for_upvalues(item, found)
		return
	if not (node is Dictionary):
		return
	if not node.has("k"):
		# Plain pair such as an if-clause [cond, body] wrapper.
		for value in node.values():
			_walk_for_upvalues(value, found)
		return
	var kind := int(node["k"])
	if kind == E_UPVALUE:
		var name := String(node["name"])
		if not found.has(name):
			found.append(name)
		return
	if kind == E_FUNCTION:
		# Stop: a nested literal owns its own upvalue list.
		return
	for key in node.keys():
		if key == "k" or key == "line" or key == "op" or key == "name":
			continue
		_walk_for_upvalues(node[key], found)
