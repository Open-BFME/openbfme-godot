## Lane L10N-1: OpenBFME's own text (docs/L10N.md). Retail text keeps coming from the player's CSF files; this is the text OpenBFME adds
## (the launcher's, later the game's), one catalog with stable ids: tools/l10n/catalog/<language>.json, compiled by tools/l10n/l10n.py into
## catalog.gd beside this file. The launcher's copy is the source; tools/l10n/l10n.py generate copies it to godot/scripts/l10n/.
##
##   L10n.t("launcher.settings.title")                                  -> "Settings"
##   L10n.t("launcher.aio.listed_twice", {"package": p, "file": f})     -> "<p> lists <f> twice."
##   L10n.t("launcher.mods.count", {"count": n})                        -> "1 mod" / "3 mods" (a plural message: CLDR forms)
##
## Placeholders are named and typed: {name} is text (str() of the argument), {name:d} an integer, {name:.1f} a number with 0-3 decimals
## in the language's decimal separator; {{ and }} are literal braces. The message is read once: an argument's text is never read again
## (FMTSTR-2's rule for localized text). There is no silent default: an unknown id, a missing argument or one of the wrong type is a
## push_error (a test fails on it) and stays visible in the text. A message the chosen language has not translated yet shows the
## English source; tools/l10n/l10n.py coverage lists those.
extends RefCounted

const Catalog := preload("res://scripts/l10n/catalog.gd")
const SOURCE := "en"
# languages that write 1,5 instead of 1.5 (CLDR decimal symbol)
const DECIMAL_COMMA := ["de", "fr", "es", "it", "nl", "pl", "ru", "cs", "hu", "sv", "no", "pt"]
# lang/<Name>.big of a retail install (RetailArchivePolicy.cpp: the game mounts lang\<Language>*.big) -> language code
const RETAIL_LANGUAGES := {"english": "en", "german": "de", "french": "fr", "spanish": "es", "italian": "it", "dutch": "nl",
	"polish": "pl", "russian": "ru", "swedish": "sv", "norwegian": "no", "czech": "cs", "hungarian": "hu", "chinese": "zh",
	"korean": "ko", "brazilian": "pt", "portuguese": "pt"}

static var _language := SOURCE


## the language text is shown in (a code of LANGUAGES)
static func language() -> String:
	return _language


## switches the text to `code`; false (and nothing changes) when the catalog has no such language
static func set_language(code: String) -> bool:
	if not Catalog.LANGUAGES.has(code):
		push_error("L10n: no text catalog for the language '%s'" % code)
		return false
	_language = code
	return true


## [{code, name, status}] of every catalog language: the source (English) first, then by code
static func languages() -> Array:
	var out := []
	for code in Catalog.LANGUAGES:
		out.append({"code": code, "name": Catalog.LANGUAGES[code].name, "status": Catalog.LANGUAGES[code].status})
	out.sort_custom(func(a, b): return a.code == SOURCE or (b.code != SOURCE and a.code < b.code))
	return out


## the language's own name ("Deutsch"), or "" when the catalog has no such language
static func language_name(code: String) -> String:
	return String(Catalog.LANGUAGES[code].name) if Catalog.LANGUAGES.has(code) else ""


## whether `code` is a catalog language Automatic may pick: a sample translation (a few strings, to test the path) is chosen only by name
static func automatic_ok(code: String) -> bool:
	return Catalog.LANGUAGES.has(code) and String(Catalog.LANGUAGES[code].status) in ["source", "complete", "partial"]


## whether the catalog defines `id`
static func has(id: String) -> bool:
	return Catalog.TEXT[SOURCE].has(id)


## the text of `id` in the current language, its placeholders filled from `args`
static func t(id: String, args: Dictionary = {}) -> String:
	return text_in(_language, id, args)


## the text of `id` in `code` (English where `code` has not translated it)
static func text_in(code: String, id: String, args: Dictionary = {}) -> String:
	var message = Catalog.TEXT[SOURCE].get(id)
	if message == null:
		push_error("L10n: unknown text id '%s'" % id)
		return id
	var lang := SOURCE
	if code != SOURCE and Catalog.TEXT.has(code) and Catalog.TEXT[code].has(id):
		message = Catalog.TEXT[code][id]
		lang = code
	if message is Dictionary:
		var n = args.get("count")
		if not (n is int or (n is float and n == floorf(n))):
			push_error("L10n: %s is a plural message and needs an integer count (got %s)" % [id, var_to_str(n)])
			n = 0
		var form := plural_category(lang, int(n))
		message = message[form]
	return format(String(message), args, lang, id)


## the CLDR plural category of the whole number `n` in `code` (tools/l10n/l10n.py PLURAL_CATEGORIES; OpenBFMEText.cpp pluralCategory)
static func plural_category(code: String, n: int) -> String:
	var a := absi(n)
	match code:
		"fr", "pt":
			return "one" if a <= 1 else "other"
		"pl":
			if a == 1:
				return "one"
			return "few" if a % 10 in [2, 3, 4] and not a % 100 in [12, 13, 14] else "many"
		"ru":
			if a % 10 == 1 and a % 100 != 11:
				return "one"
			return "few" if a % 10 in [2, 3, 4] and not a % 100 in [12, 13, 14] else "many"
		"cs":
			if a == 1:
				return "one"
			return "few" if a in [2, 3, 4] else "other"
		"zh", "ko":
			return "other"
	return "one" if a == 1 else "other"


## `pattern` with its placeholders filled (see the file's comment); `id` names the message in errors
static func format(pattern: String, args: Dictionary, code: String = SOURCE, id: String = "") -> String:
	var out := ""
	var i := 0
	var n := pattern.length()
	while i < n:
		var ch := pattern[i]
		if ch == "{" and i + 1 < n and pattern[i + 1] == "{":
			out += "{"
			i += 2
			continue
		if ch == "}" and i + 1 < n and pattern[i + 1] == "}":
			out += "}"
			i += 2
			continue
		if ch != "{":
			out += ch
			i += 1
			continue
		var close := pattern.find("}", i)
		if close < 0:
			push_error("L10n: %s: an unclosed '{'" % id)
			return out + pattern.substr(i)
		var spec := pattern.substr(i + 1, close - i - 1)
		var name := spec.get_slice(":", 0)
		var kind := spec.substr(name.length() + 1) if spec.contains(":") else ""
		out += _argument(name, kind, args, code, id, pattern.substr(i, close - i + 1))
		i = close + 1
	return out


static func _argument(name: String, kind: String, args: Dictionary, code: String, id: String, literal: String) -> String:
	if not args.has(name):
		push_error("L10n: %s needs the argument '%s'" % [id, name])
		return literal
	var v = args[name]
	if kind == "":
		return str(v)
	if not (v is int or v is float):
		push_error("L10n: %s: the argument '%s' is a number ({%s:%s}), got %s" % [id, name, name, kind, var_to_str(v)])
		return str(v)
	if kind == "d":
		return str(int(v))
	if kind.length() == 3 and kind[0] == "." and kind[2] == "f" and kind[1] in "0123":
		var s := ("%." + kind[1] + "f") % float(v)
		return s.replace(".", ",") if code in DECIMAL_COMMA else s
	push_error("L10n: %s: unknown placeholder type {%s:%s}" % [id, name, kind])
	return literal


## the language of the retail install in `folder`, from its lang/<Language>.big archive: {code, name, error}. code is "" when the folder
## has none of the known archives (error says why); the name is the archive's ("German"), shown when OpenBFME has no such translation.
static func install_language(folder: String) -> Dictionary:
	if folder == "":
		return {"code": "", "name": "", "error": "no game folder"}
	var dir := DirAccess.open(folder.path_join("lang"))
	if dir == null:
		return {"code": "", "name": "", "error": "%s has no lang folder" % folder}
	var found := {}
	for f in dir.get_files():
		var stem := f.get_basename()
		if f.get_extension().to_lower() == "big" and RETAIL_LANGUAGES.has(stem.to_lower()):
			found[stem] = RETAIL_LANGUAGES[stem.to_lower()]
	if found.size() != 1:
		var why := "no lang/<Language>.big" if found.is_empty() else "several languages (%s)" % ", ".join(PackedStringArray(found.keys()))
		return {"code": "", "name": "", "error": "%s: %s" % [folder.path_join("lang"), why]}
	var stem: String = found.keys()[0]
	return {"code": found[stem], "name": stem, "error": ""}
