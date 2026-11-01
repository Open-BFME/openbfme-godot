## Lane L10N-1: the text catalog's runtime (scripts/l10n/l10n.gd) on the generated catalog and on synthetic patterns. Run by
## tools/l10n/test_l10n.py:
##   godot --headless --path launcher --script res://tests/l10n_test.gd -- --dir=<an empty folder>   prints L10N OK
##   ... -- --errors   each misuse once (an unknown id, a missing or mistyped argument, a plural without a count): the runner
##                     checks every one is reported (push_error) and still visible in the text
extends SceneTree

const L10n := preload("res://scripts/l10n/l10n.gd")
const Catalog := preload("res://scripts/l10n/catalog.gd")

var failures := 0


func check(ok: bool, what: String) -> void:
	if not ok:
		failures += 1
		printerr("L10N FAIL: ", what)


func _init() -> void:
	_run.call_deferred()


func _run() -> void:
	var args := OS.get_cmdline_user_args()
	if "--errors" in args:
		print("ERRCASE unknown=", L10n.t("launcher.no_such_id"))
		print("ERRCASE missing=", L10n.format("a {name} b", {}, "en", "test.missing"))
		print("ERRCASE type=", L10n.format("{n:d} files", {"n": "many"}, "en", "test.type"))
		print("ERRCASE language=", L10n.set_language("xx"), " ", L10n.language())
		quit(0)
		return
	var dir := ""
	for a in args:
		if a.begins_with("--dir="):
			dir = a.substr(6)

	# the generated catalog: English is the source of every id, a language translates a subset
	check(L10n.language() == "en", "the default language is the source")
	check(L10n.languages()[0].code == "en" and L10n.languages().size() == Catalog.LANGUAGES.size(), "languages(): source first")
	check(L10n.t("common.cancel") == "Cancel", "an English message")
	check(L10n.has("common.cancel") and not L10n.has("common.no_such_id"), "has()")
	check(L10n.text_in("de", "common.cancel") == "Abbrechen", "a German message")
	check(not L10n.automatic_ok("de") and L10n.automatic_ok("en"), "Automatic never picks a sample translation")
	for code in Catalog.TEXT:
		for id in Catalog.TEXT[code]:
			check(Catalog.TEXT["en"].has(id), "%s translates %s, which English does not have" % [code, id])
	check(L10n.set_language("de") and L10n.language() == "de", "set_language(de)")
	check(L10n.t("launcher.settings.language_automatic", {"language": "Deutsch"}) == "Automatisch (Deutsch)", "a German message with a placeholder")
	var untranslated := ""
	for id in Catalog.TEXT["en"]:
		if not Catalog.TEXT["de"].has(id):
			untranslated = id
			break
	if untranslated != "" and not Catalog.TEXT["en"][untranslated] is Dictionary and not String(Catalog.TEXT["en"][untranslated]).contains("{"):
		check(L10n.t(untranslated) == Catalog.TEXT["en"][untranslated], "an id German has not translated shows the English source")
	check(L10n.set_language("en"), "back to English")

	# placeholders: named, typed, read once
	check(L10n.format("{a} lists {b} twice.", {"a": "X", "b": "Y"}) == "X lists Y twice.", "text placeholders")
	check(L10n.format("{b} before {a}", {"a": 1, "b": 2}) == "2 before 1", "a translation may reorder placeholders")
	check(L10n.format("{n:d} files", {"n": 3}) == "3 files", "{n:d}")
	check(L10n.format("{n:d}", {"n": 3.9}) == "3", "{n:d} of a float truncates like %d")
	check(L10n.format("{x:.1f} GB", {"x": 1.25}, "en") == "%.1f GB" % 1.25, "{x:.1f} in English")
	check(L10n.format("{x:.2f} GB", {"x": 1.5}, "de") == "1,50 GB", "{x:.2f} in German: a decimal comma")
	check(L10n.format("{x:.0f}%", {"x": 99.6}) == "100%", "{x:.0f}")
	check(L10n.format("{{literal}} {a}", {"a": "{b}"}) == "{literal} {b}", "{{ }} are braces; an argument's text is not read again")
	check(L10n.format("{a}", {"a": "{a}{a}"}) == "{a}{a}", "no recursion")
	check(L10n.format("100% {a}", {"a": "%s"}) == "100% %s", "percent signs are plain text")

	# plural categories (CLDR, whole numbers)
	for c in [["en", 0, "other"], ["en", 1, "one"], ["en", 2, "other"], ["de", 1, "one"], ["de", 5, "other"], ["fr", 0, "one"],
			["fr", 1, "one"], ["fr", 2, "other"], ["pl", 1, "one"], ["pl", 3, "few"], ["pl", 5, "many"], ["pl", 22, "few"],
			["pl", 12, "many"], ["ru", 21, "one"], ["ru", 11, "many"], ["ru", 23, "few"], ["cs", 3, "few"], ["cs", 5, "other"],
			["zh", 1, "other"]]:
		check(L10n.plural_category(c[0], c[1]) == c[2], "plural_category(%s, %d) = %s" % c)

	# the retail install's language: lang/<Language>.big
	if dir != "":
		for g in [["german", ["German.big", "GermanAudio.big", "germanpatch201.big"]], ["english", ["English.big", "englishpatch201.big"]],
				["two", ["English.big", "French.big"]], ["none", ["Splash.big"]]]:
			var lang := dir.path_join(g[0]).path_join("lang")
			DirAccess.make_dir_recursive_absolute(lang)
			for f in g[1]:
				FileAccess.open(lang.path_join(f), FileAccess.WRITE).close()
		var de := L10n.install_language(dir.path_join("german"))
		check(de.code == "de" and de.name == "German" and de.error == "", "a German install: %s" % de)
		check(L10n.install_language(dir.path_join("english")).code == "en", "an English install")
		var two := L10n.install_language(dir.path_join("two"))
		check(two.code == "" and two.error.contains("several languages"), "two languages are not guessed: %s" % two)
		check(L10n.install_language(dir.path_join("none")).code == "", "no language archive: unknown")
		check(L10n.install_language(dir.path_join("missing")).error.contains("no lang folder"), "no lang folder")

	if failures == 0:
		print("L10N OK")
	quit(1 if failures else 0)
