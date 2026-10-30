## Lane LAUNCHERUI-1: the launcher's layout check. Lays the real window out in every state a player meets (scripts/main.gd _show_demo) at
## every window size, walks the scene tree and measures: buttons in a row share height, every cell of a row / column sits where its size flags put it, gaps equal the box's separation, panels' cells sit on the theme's padding, the screens / bottom bar / PLAY share the window's margins,  nothing
## overlaps, nothing sits outside its parent or the window, text is not cut (a truncated line must carry its full text as the tooltip),
## icons are centred, and blocks whose edges miss each other by a few pixels are reported as "not lined up all the way".
##
##   godot --headless --path launcher --script res://tests/layout_test.gd -- --ui-demo=main
##
## (the --ui-demo=main only switches the window to its made-up demo state: no network, nothing installed or written). Prints
## "LAYOUT finding" lines and "LAYOUT OK <cases> cases x <sizes> sizes, <n> controls measured" / "LAYOUT FAIL ..."; exit 0 only when
## clean. It fails closed: a state that could not be laid out or walked, a state whose screen is not the expected one, a missing
## state / size, and a checker that does not find the defects of its own fixture (selftest) are failures too.
##
## Sizes are the window's size in pixels; the launcher lays out in the content-scale base (project.godot: canvas_items stretch, expand
## aspect, 1280 x 720: the window divided by min(w / 1280, h / 720)), which this test sets directly on the root. That covers the LOGICAL
## stretch of a DPI scale factor (125 % = 1600 x 900, 150 % = 1920 x 1080, 200 % = 2560 x 1440 all give the 1280 x 720 base; checked against
## the project's own stretch in the review of this lane). It does not run the OS's DPI start-up path (main.gd _fit_window returns early
## headless) nor render pixels: real DPI behaviour and font rasterisation at those scales are untested here.
##
## Coverage is asserted, not assumed: every case lists the layers it must have (window, dialog, folder picker) and the texts each must show;
## a layer that measures no control, a layer that is missing or extra, a missing text, an empty case or size list, are failures. It prints a
## `LAYOUT case <name>@<WxH> main=<n> [dialog=<n>] [picker=<n>]` line per case and size, which tools/release/test_launcher.py checks against
## its own inventory.
extends SceneTree

const MainScene := preload("res://scenes/main.tscn")
const WidgetsScript := preload("res://scripts/ui/widgets.gd")
const StyleScript := preload("res://scripts/ui/style.gd")

## the windows (pixels): the 720p base, the Deck, 1080p / 1440p, ultra-wide, the window's minimum size (main.gd _fit_window: 960 x 540),
## and a tall narrow one; MACPLAY-1: the Mac windows main.gd fit_size makes on Retina screens (a MacBook Air at its default 1470 x 956
## points: 2704 x 1521; the owner's M5 at 1710 x 1008 points, measured: 3146 x 1769) and the 960 x 540-point minimum there (1920 x 1080)
const WINDOWS := [Vector2i(1280, 720), Vector2i(1280, 800), Vector2i(1920, 1080), Vector2i(2560, 1440), Vector2i(3440, 1440),
	Vector2i(960, 540), Vector2i(960, 1000),
	Vector2i(2704, 1521), Vector2i(3146, 1769)]
const BASE := Vector2(1280, 720)
const EPS := 0.75        # px: layout rounds to half pixels
const NEAR := 8.0        # px: edges closer than this but not equal are "almost lined up"
const INK_NEAR := 4.0    # px: the same for the first ink of text
## the one button allowed to be taller than the others of its row: the hero button (scripts/ui/gold_button.gd: it has a `word`)
const HERO := []
## the screens whose cards run from the left to the right margin
const FULL_WIDTH := ["play", "news", "multiplayer", "mods", "games"]
## the real players-online chip of the Play card (play_screen.set_players; its text is "●  <n> players online"; the largest count its lobby may report)
const CHIPS := ["●  123 players online", "●  10000000 players online"]


## One case: the demo state, the screen it shows, and the texts each layer must show (a layer is expected exactly when it has texts: a
## state that did not apply, or a dialog / picker that did not open, fails here). A text ending in * is a prefix.
static func _case(state: String, screen: String, window: Array, dialog := [], picker := [], chip := "") -> Dictionary:
	return {"state": state, "screen": screen, "main": window, "dialog": dialog, "picker": picker, "chip": chip}


static func _cases() -> Dictionary:
	var c := {
		"main": _case("main", "play", ["Up to date", "PLAY", "Check for updates", "In ~/Games/BFME", "What's New"]),
		"news": _case("news", "news", ["News & patch notes", "PLAY"]),
		"multiplayer": _case("multiplayer", "multiplayer", ["Multiplayer", "Sign in", "Use a recovery code on this PC"]),
		"mods": _case("mods", "mods", ["PLAY WITH", "Example Realms", "Installed: ready", "Refresh", "Realms No Intro 1.0.0 (ready)", "Needs engine features:*"]),
		"games": _case("games", "games", ["NOT FOUND", "Download"]),
		"games-bfme1": _case("games-bfme1", "games", ["BATTLE FOR MIDDLE-EARTH I (OPTIONAL)", "Downloaded from BFME Arena (bfmeladder.com)*", "Download"]),
		"games-downloading": _case("games-downloading", "games", ["Pause", "Cancel", "RotWK: game files 412 of 1 830*"]),
		"settings": _case("settings", "settings", ["Channel", "Free camera", "Offer to download the games"]),
		"display": _case("display", "settings", ["Window mode", "Resolution", "Automatic", "More display settings"]),
		"about": _case("about", "settings", ["Installed versions", "Play this version", "Install"]),
		"update": _case("update", "play", ["Update to*", "Check for updates"]),
		"downloading": _case("downloading", "play", ["Pause", "Cancel", "Updating to*"]),
		"error": _case("error", "play", ["Update to*"], ["Update not installed", "Copy details", "Open log folder", "Close"]),
		"glibc": _case("glibc", "play", ["PLAY"], ["The game did not start", "Close"]),
		"first-run": _case("first-run", "play", ["Not installed", "Download the games", "No nightly release yet"]),
		"offline": _case("offline", "play", ["Could not check for updates"]),
		"verifying": _case("verifying", "play", ["Checking*", "Pause", "Cancel"]),
		"paused": _case("paused", "play", ["Resume", "Cancel"]),
		"update-launcher": _case("update-launcher", "play", ["Update launcher to*"]),
		"warning": _case("warning", "play", ["Warning:*"]),
		"ask": _case("ask", "play", ["PLAY"], ["Install an older version?", "Install", "Cancel"]),
		"playing": _case("playing", "play", ["Playing*", "Pause"]),
		"long": _case("long", "play", ["Warning:*", "Pause", "Cancel"]),
		"picker": _case("picker", "games", ["DOWNLOAD THE GAMES", "Change…"], [], ["Cancel", "Select Current Folder"]),
	}
	# the online-players chip on the Play card (the demo shows "12345 players online" everywhere): two states, both extreme counts
	for state in ["update-launcher", "long"]:
		for chip in CHIPS:
			var base: Dictionary = c[state]
			c["%s+%s" % [state, chip.get_slice("  ", 1).split(" ")[0]]] = _case(state, "play", base.main + [chip], [], [], chip)
	return c


var _found := {}         # "kind | detail" -> ["state@WxH", ...]
var _controls := 0
var _failures := PackedStringArray()


func _initialize() -> void:
	_run.call_deferred()


func _fail(text: String) -> void:
	_failures.append(text)
	print("LAYOUT fail: " + text)


func _run() -> void:
	if not "--ui-demo=main" in OS.get_cmdline_user_args():
		_fail("start it with `-- --ui-demo=main` (the window's demo state; no network)")
		_end()
		return
	await _selftest()
	var cases := _cases()
	if cases.is_empty() or WINDOWS.is_empty():
		_fail("nothing to measure: %d cases, %d windows" % [cases.size(), WINDOWS.size()])
	root.content_scale_mode = Window.CONTENT_SCALE_MODE_DISABLED
	var done := 0
	for name in cases:
		var c: Dictionary = cases[name]
		var main: Control = MainScene.instantiate()
		root.add_child(main)
		await process_frame
		if not main.has_method("_show_demo") or main._demo != "main" or main._play == null:
			_fail("%s: the window did not start in its demo state" % name)
			main.queue_free()
			await process_frame
			continue
		main._show_demo(c.state)
		var nav: Control = main._nav["play"].get_parent()
		var footer: Label = nav.get_child(nav.get_child_count() - 1).get_child(0)
		footer.text = main.L10n.t("launcher.main.footer_version", {"version": main.Semver.display("v0.4.12-nightly.261024100")})
		if c.state == "long":
			footer.text += "\n" + main.L10n.t("launcher.main.footer_local_trial")
		if c.chip != "":
			main._play_screen.set_players(c.chip.trim_prefix("●  "))   # the real chip (lane ONLINECOUNT-1), at the widest counts
		await _frames(3)
		_check_applied(c.state, main)
		if main._screen != c.screen:
			_fail("%s: shows the screen '%s', expected '%s'" % [name, main._screen, c.screen])
		for win in WINDOWS:
			var k := minf(win.x / BASE.x, win.y / BASE.y)
			var base := Vector2i((Vector2(win) / k).round())
			root.size = base
			await _frames(3)
			var where := "%s@%dx%d" % [name, win.x, win.y]
			if Vector2i(main.size) != base:
				_fail("%s: the window did not take its base size %s (is %s)" % [where, base, main.size])
				continue
			if c.state == "picker":
				main._games_screen._dialog.size = Vector2i(Vector2(base) * 0.75)
				await _frames(3)
			var layers := _layers(main)
			var names := layers.map(func(l): return l.name)
			var want := ["main"] + (["dialog"] if not c.dialog.is_empty() else []) + (["picker"] if not c.picker.is_empty() else [])
			if names != want:
				_fail("%s: the layers are %s, expected %s" % [where, names, want])
				continue
			var counts := []
			for layer in layers:
				var r := _check_layer(layer.root, layer.bounds, where, layer.skip, layer.name != "picker")
				counts.append("%s=%d" % [layer.name, r.count])
				if r.count == 0:
					_fail("%s: the %s layer measured no control" % [where, layer.name])
				for text in c[layer.name]:
					if not _has_text(r.texts, text):
						_fail("%s: the %s layer does not show '%s'" % [where, layer.name, text])
				if layer.name == "main":
					_check_window(main, where)
			print("LAYOUT case %s %s" % [where, " ".join(counts)])
		done += 1
		main.queue_free()
		await process_frame
	if done != cases.size():
		_fail("only %d of %d cases were walked" % [done, cases.size()])
	var keys := _found.keys()
	keys.sort()
	for key in keys:
		var where: Array = _found[key]
		print("LAYOUT finding: %s  [%d: %s%s]" % [key, where.size(), ", ".join(where.slice(0, 4)), ", ..." if where.size() > 4 else ""])
	if not _found.is_empty():
		_fail("%d misalignments (distinct) found" % _found.size())
	if _failures.is_empty():
		print("LAYOUT OK %d cases x %d sizes, %d controls measured" % [done, WINDOWS.size(), _controls])
	_end()


## the state's own effects (what _show_demo is meant to have done), beyond the texts each layer must show
func _check_applied(state: String, main: Control) -> void:
	var statuses := {
		"offline": "Could not check for updates. Check your internet connection; retry later.",
		"paused": "Paused. Resume continues where it stopped.",
	}
	if statuses.has(state) and main._status.text != statuses[state]:
		_fail("%s: unexpected status '%s'" % [state, main._status.text])
	if state in ["settings", "display", "about"] and main._settings.tab != {"settings": "general", "display": "display", "about": "about"}[state]:
		_fail("%s: Settings shows the tab '%s'" % [state, main._settings.tab])
	if state in ["error", "glibc", "ask"] and not main._message.visible:
		_fail("%s: its dialog is not shown" % state)
	if state in ["update", "error"] and not main._update.visible:
		_fail("%s: the Update button is not shown" % state)
	if state == "warning" and not main._banner.visible:
		_fail("%s: the warning banner is not shown" % state)
	if state in ["downloading", "verifying", "paused", "playing", "long", "games-downloading"] and not main._progress.visible:
		_fail("%s: the progress bar is not shown" % state)
	if (state == "games-bfme1") != main._games_screen.offer_bfme1:
		_fail("%s: the Game files screen %s the optional BFME1 download" % [state, "offers" if main._games_screen.offer_bfme1 else "does not offer"])
	if state == "picker" and not main._games_screen._dialog.visible:
		_fail("%s: the folder picker is not open" % state)


static func _has_text(texts: PackedStringArray, want: String) -> bool:
	for t in texts:
		if t == want or (want.ends_with("*") and t.begins_with(want.trim_suffix("*"))):
			return true
	return false


func _end() -> void:
	if not _failures.is_empty():
		print("LAYOUT FAIL %d: %s" % [_failures.size(), "; ".join(_failures.slice(0, 6))])
	quit(1 if not _failures.is_empty() else 0)


func _frames(n: int) -> void:
	for i in n:
		await process_frame


## what to measure at once: the window (without its dialogs), the error / question dialog while shown, the folder picker while shown
func _layers(main: Control) -> Array:
	var out := [{"name": "main", "root": main, "bounds": Rect2(Vector2.ZERO, main.size), "skip": [main._message]}]
	if main._message.visible:
		out.append({"name": "dialog", "root": main._message, "bounds": Rect2(Vector2.ZERO, main.size), "skip": []})
	var dialog: Window = main._games_screen._dialog
	if dialog.visible:
		out.append({"name": "picker", "root": dialog, "bounds": Rect2(Vector2.ZERO, Vector2(dialog.size)), "skip": []})
	return out


# ---- the measurements --------------------------------------------------------------------------------------------------------------------

func _record(where: String, kind: String, what: String) -> void:
	var key := "%s | %s" % [kind, what]
	if not _found.has(key):
		_found[key] = []
	_found[key].append(where)


static func _name(c: Node) -> String:
	var text := ""
	if c is Label or c is BaseButton or c is LineEdit:
		text = c.text
	if text == "" and c is Control:
		text = _first_text(c)
	return "%s '%s'" % [c.get_class(), text.replace("\n", " ").left(32)]


static func _first_text(c: Node) -> String:
	for k in c.get_children():
		if (k is Label or k is BaseButton) and k.text != "":
			return k.text
		var t := _first_text(k)
		if t != "":
			return t
	return ""


static func _is_decor(c: Control) -> bool:
	return c is ColorRect or c is Separator or (c is TextureRect and c.expand_mode == TextureRect.EXPAND_IGNORE_SIZE) \
		or (c.get_script() != null and "kind" in c and c.get("kind") != null) or c.get_class() == "Control" and c.get_child_count() == 0


## controls that show something: text, a button, a bar, a list, a picture, a panel
static func _is_content(c: Control) -> bool:
	if _is_decor(c):
		return false
	if c is Label:
		return c.text != ""
	return c is BaseButton or c is LineEdit or c is ItemList or c is ProgressBar or c is RichTextLabel or c is PanelContainer \
		or (c is TextureRect and c.texture != null)


## `c` with the part a scroll area or clipping ancestor shows (empty when it is scrolled away)
static func _shown(c: Control, top: Node) -> Rect2:
	var r := c.get_global_rect()
	var p := c.get_parent()
	while p != null and p != top.get_parent():
		if p is Control and (p is ScrollContainer or p.clip_contents):
			r = r.intersection(p.get_global_rect())
		p = p.get_parent()
	return r


static func _walk(node: Node, skip: Array, out: Array, internal: bool) -> void:
	for k in node.get_children(internal):
		if k in skip or k is Window:
			continue
		if k is Control and k.is_visible_in_tree():
			out.append(k)
		_walk(k, skip, out, internal)


## every check of one layer: `top` is its root (the window's Launcher, a dialog, a Window), `bounds` the area that is on screen; `ours`: the
## layout is the launcher's own (the folder picker is Godot's dialog: it is only measured for overflow, cut text and overlaps).
## Returns the number of controls measured and the texts they show.
func _check_layer(top: Node, bounds: Rect2, where: String, skip := [], ours := true) -> Dictionary:
	var all := []
	if top is Control:
		all.append(top)
	_walk(top, skip, all, top is Window)   # a dialog's own controls are internal children of the Window
	_controls += all.size()
	var texts := _check_nodes(all, top, bounds, where, ours)
	return {"count": all.size(), "texts": texts}


func _check_nodes(all: Array, top: Node, bounds: Rect2, where: String, ours := true) -> PackedStringArray:
	var content := all.filter(func(c): return _is_content(c))
	var texts := PackedStringArray()
	for c: Control in content:
		if c is Label or c is BaseButton:
			texts.append(c.text)
	# 1. inside the window and inside the parent
	for c: Control in all:
		if _is_decor(c) or c == top:
			continue
		var r := c.get_global_rect()
		var p := c.get_parent()
		var scrolled := false
		var q := p
		while q != null and q != top:
			if q is ScrollContainer:
				scrolled = true
				if r.end.x > q.get_global_rect().end.x + EPS or r.position.x < q.get_global_rect().position.x - EPS:
					_record(where, "wider than its scroll area", _name(c))
				break
			q = q.get_parent()
		if not scrolled and not bounds.grow(EPS).encloses(r) and _shown(c, top).has_area():
			_record(where, "outside the window", "%s %s" % [_name(c), r])
		if p is Control and not p is ScrollContainer and p != top and not scrolled and not _is_decor(p):
			if not p.get_global_rect().grow(EPS).encloses(r) and _is_content(c):
				_record(where, "sticks out of its parent", "%s out of %s" % [_name(c), _name(p)])
	# 2. text and boxes: nothing smaller than its content, nothing cut
	for c: Control in content:
		var r := c.get_global_rect()
		if r.size.x + EPS < c.get_combined_minimum_size().x or r.size.y + EPS < c.get_combined_minimum_size().y:
			_record(where, "smaller than its content", "%s is %s, needs %s" % [_name(c), r.size, c.get_combined_minimum_size()])
		if c is Label:
			var font: Font = c.get_theme_font("font")
			var fs: int = c.get_theme_font_size("font_size")
			if c.autowrap_mode != TextServer.AUTOWRAP_OFF:
				if c.get_visible_line_count() < c.get_line_count():
					_record(where, "wrapped text cut", _name(c))
			else:
				var wide := font.get_string_size(c.text, HORIZONTAL_ALIGNMENT_LEFT, -1, fs).x
				if wide > r.size.x + EPS and not _tip_of(c).contains(c.text.strip_edges()):
					_record(where, "text cut without a tooltip", "%s is %d px wide in %d" % [_name(c), wide, r.size.x])
		if ours and c is Button and c.text != "" and c.icon != null and c.alignment == HORIZONTAL_ALIGNMENT_CENTER \
				and c.icon_alignment == HORIZONTAL_ALIGNMENT_LEFT and r.size.x - c.get_minimum_size().x > 4.0:
			_record(where, "icon apart from its text", "%s is %d px wider than its content" % [_name(c), r.size.x - c.get_minimum_size().x])
		if ours and c is Button and c.text == "" and c.icon != null:
			if c.icon_alignment != HORIZONTAL_ALIGNMENT_CENTER or c.vertical_icon_alignment != VERTICAL_ALIGNMENT_CENTER:
				_record(where, "icon not centred in its button", _name(c))
	# 2b. a block placed by hand (a parent that is not a container) sits on one of its parent's edges on each axis, or is centred in it:
	# any other offset, of any size, is a margin nobody chose
	for c: Control in content:
		var p := c.get_parent()
		if ours and c is PanelContainer and p is Control and not p is Container and p != top:
			var rp: Rect2 = p.get_global_rect()
			var rc := c.get_global_rect()
			for axis in [["horizontal", rc.position.x, rc.end.x, rp.position.x, rp.end.x], ["vertical", rc.position.y, rc.end.y, rp.position.y, rp.end.y]]:
				if absf(axis[1] - axis[3]) > EPS and absf(axis[2] - axis[4]) > EPS and absf((axis[1] + axis[2]) / 2.0 - (axis[3] + axis[4]) / 2.0) > EPS:
					_record(where, "placed by hand off its parent's edges (%s)" % axis[0], "%s in %s" % [_name(c), _name(p)])
	# 3. overlaps
	for i in content.size():
		var a: Control = content[i]
		var ra := _shown(a, top)
		if not ra.has_area():
			continue
		for j in range(i + 1, content.size()):
			var b: Control = content[j]
			if a.is_ancestor_of(b) or b.is_ancestor_of(a):
				continue
			var inter := ra.intersection(_shown(b, top))
			if inter.has_area() and inter.get_area() > 1.0:
				_record(where, "overlap", "%s and %s" % [_name(a), _name(b)])
	if not ours:
		return texts
	# 4. rows and columns: explicit expectations (cross axis by the kids' own size flags, gaps equal the box's separation, a row with an
	# expanding cell ends on its box's edges), then the panels' and margin boxes' padding
	for c: Control in all:
		if c is BoxContainer:
			var vis := []
			for k in c.get_children():
				if k is Control and k.is_visible_in_tree() and not k.top_level:
					vis.append(k)
			_check_box(c, vis, where)
		elif c is PanelContainer or c is MarginContainer:
			_check_padding(c, where)
	# 5. blocks that nearly line up: stacked blocks (one column) by their left / right edges, blocks side by side in one row by their top /
	# bottom edges; a miss of at most NEAR px that is not a hit is what the eye reads as "not lined up all the way". The edge of a button
	# with no box of its own (a toggle) is its drawing's edge. Across the window's regions (rail, screens, bottom bar) only wide blocks count.
	var blocks := content.filter(func(c): return c is BaseButton or c is PanelContainer or c is ProgressBar or c is ItemList or c is LineEdit)
	for i in blocks.size():
		for j in range(i + 1, blocks.size()):
			var a: Control = blocks[i]
			var b: Control = blocks[j]
			if a.is_ancestor_of(b) or b.is_ancestor_of(a) or _panel_depth(a) != _panel_depth(b):
				continue
			if a.get_parent() == b.get_parent() and a.get_parent() is FlowContainer:
				continue   # the lines of a wrapping row have ragged ends by design
			if not (_shown(a, top).has_area() and _shown(b, top).has_area()):
				continue
			var ra := _ink(a)
			var rb := _ink(b)
			var x_overlap := ra.position.x < rb.end.x - EPS and rb.position.x < ra.end.x - EPS
			var y_overlap := minf(ra.end.y, rb.end.y) - maxf(ra.position.y, rb.position.y)
			var edges := []
			if x_overlap and y_overlap <= EPS and (_region(a, top) == _region(b, top) or minf(ra.size.x, rb.size.x) >= 150.0):
				edges = [["left", ra.position.x, rb.position.x], ["right", ra.end.x, rb.end.x]]
				# lane NIGHTLY-2: a wrapping row's line ends where its last item ends, ragged by design, also against a block above or
				# below it (the Play card's chips over YOUR GAMES: a long version name fills a line to a few px short of the card's edge)
				if a.get_parent() is FlowContainer or b.get_parent() is FlowContainer:
					edges = [edges[0]]
			elif not x_overlap and y_overlap >= 0.5 * minf(ra.size.y, rb.size.y) and a.get_class() == b.get_class() \
					and _lca(a, b) is HBoxContainer:
				edges = [["top", ra.position.y, rb.position.y], ["bottom", ra.end.y, rb.end.y]]
			for edge in edges:
				# Independent columns with hand-sized cards do not promise equal bottom edges.
				if edge[0] == "bottom" and a.get_parent() != b.get_parent():
					continue
				var d := absf(edge[1] - edge[2])
				if d > EPS and d <= NEAR:
					_record(where, "almost lined up (%s edge)" % edge[0], "%s and %s differ by %.1f px" % [_name(a), _name(b), d])
	# 6. text that starts a column: the first ink of left-aligned labels and rail entries stacked in one column
	var inks := content.filter(func(c): return _ink_left(c) != null and _starts_line(c))
	for i in inks.size():
		for j in range(i + 1, inks.size()):
			var a: Control = inks[i]
			var b: Control = inks[j]
			if a.is_ancestor_of(b) or b.is_ancestor_of(a) or _region(a, top) != _region(b, top) or _panel_depth(a) != _panel_depth(b):
				continue
			var ra := a.get_global_rect()
			var rb := b.get_global_rect()
			if ra.position.y < rb.end.y - EPS and rb.position.y < ra.end.y - EPS:
				continue
			var d := absf(_ink_left(a) - _ink_left(b))
			if d > EPS and d <= INK_NEAR:
				_record(where, "text starts almost in line", "%s and %s differ by %.1f px" % [_name(a), _name(b), d])
	return texts


## how many panels (cards, chips) hold `c`: a block inside a padded card is not meant to line up with one outside it
static func _panel_depth(c: Node) -> int:
	var n := 0
	var p := c.get_parent()
	while p != null:
		n += 1 if p is PanelContainer else 0
		p = p.get_parent()
	return n


## true when `c` begins its line: not a later cell of a row
static func _starts_line(c: Control) -> bool:
	var p := c.get_parent()
	if not p is HBoxContainer:
		return true
	for k in p.get_children():
		if k is Control and k.is_visible_in_tree():
			return k == c
	return true


## where a left-aligned label's or entry's first ink is (null: not one of those)
static func _ink_left(c: Control) -> Variant:
	if c is Label and c.horizontal_alignment == HORIZONTAL_ALIGNMENT_LEFT and c.autowrap_mode == TextServer.AUTOWRAP_OFF:
		return c.get_global_rect().position.x
	if c is Button and c.alignment == HORIZONTAL_ALIGNMENT_LEFT and c.theme_type_variation == "NavButton":
		return c.get_global_rect().position.x + c.get_theme_stylebox("normal").get_margin(SIDE_LEFT)
	return null


## the part of a block that shows: its rectangle, or for a button with no box (a toggle) the inside of its margins
static func _ink(c: Control) -> Rect2:
	var r := c.get_global_rect()
	if c is BaseButton and not c is OptionButton:
		var box := c.get_theme_stylebox("normal")
		if box is StyleBoxFlat and box.bg_color.a == 0.0 and box.border_width_left == 0:
			return Rect2(r.position + Vector2(box.get_margin(SIDE_LEFT), box.get_margin(SIDE_TOP)),
				r.size - Vector2(box.get_margin(SIDE_LEFT) + box.get_margin(SIDE_RIGHT), box.get_margin(SIDE_TOP) + box.get_margin(SIDE_BOTTOM)))
	return r


## the child of the layer's root that holds `c` (the rail, the screens, the bottom bar)
static func _region(c: Node, top: Node) -> Node:
	var boundary := top
	for child in top.get_children():
		if child is ScrollContainer and child.get_child_count() == 1 and child.get_child(0).is_ancestor_of(c):
			boundary = child.get_child(0)
	var p := c
	while p.get_parent() != null and p.get_parent() != boundary:
		p = p.get_parent()
	return p


static func _lca(a: Node, b: Node) -> Node:
	var p := a.get_parent()
	while p != null and not p.is_ancestor_of(b):
		p = p.get_parent()
	return p


## does [lo, hi] sit where a kid with these size flags sits in [box_lo, box_hi]?
static func _axis_ok(lo: float, hi: float, box_lo: float, box_hi: float, flags: int) -> bool:
	if flags & Control.SIZE_FILL:
		return absf(lo - box_lo) <= EPS and absf(hi - box_hi) <= EPS
	if flags & Control.SIZE_SHRINK_CENTER:
		return absf((lo + hi) / 2.0 - (box_lo + box_hi) / 2.0) <= EPS
	if flags & Control.SIZE_SHRINK_END:
		return absf(hi - box_hi) <= EPS
	return absf(lo - box_lo) <= EPS


## a row (HBox) or column (VBox) and its visible cells `vis`
func _check_box(box: BoxContainer, vis: Array, where: String) -> void:
	var vertical := box.vertical
	var rb := box.get_global_rect()
	var kids := vis.filter(func(k): return not _is_decor(k) and k.get_class() != "Control")
	# the cross axis: every cell sits where its size flags say (fill: on both edges, begin: the start, centre, end)
	for k in kids:
		var r: Rect2 = k.get_global_rect()
		var ok := _axis_ok(r.position.y, r.end.y, rb.position.y, rb.end.y, k.size_flags_vertical) if not vertical \
			else _axis_ok(r.position.x, r.end.x, rb.position.x, rb.end.x, k.size_flags_horizontal)
		if not ok:
			_record(where, "%s cell off its place" % ("column" if vertical else "row"), "%s in %s: %s in %s" % [_name(k), _name(box), r, rb])
	# the main axis: every gap is the box's separation, exactly
	var sep := box.get_theme_constant("separation")
	for i in range(1, vis.size()):
		var a: Rect2 = vis[i - 1].get_global_rect()
		var b: Rect2 = vis[i].get_global_rect()
		var gap := (b.position.y - a.end.y) if vertical else (b.position.x - a.end.x)
		if absf(gap - sep) > EPS:
			_record(where, "gap is not the separation (%d)" % sep, "%s between %s and %s is %.1f" % [_name(box), _name(vis[i - 1]), _name(vis[i]), gap])
	# a row with an expanding cell runs from its box's left edge to its right edge (a toggle's drawing, not its box)
	if not vertical and not vis.is_empty() and vis.any(func(k): return k.size_flags_horizontal & Control.SIZE_EXPAND):
		var first := _ink(vis[0])
		var last := _ink(vis[vis.size() - 1])
		if absf(first.position.x - rb.position.x) > EPS or absf(last.end.x - rb.end.x) > EPS:
			_record(where, "row does not run edge to edge", "%s: %s .. %s in %s" % [_name(box), first.position.x, last.end.x, rb])
	# buttons of one row: the same height (the hero button excepted)
	if not vertical:
		var heights := []
		for k in kids:
			if k is BaseButton and k.get("word") == null and not k.text in HERO and not (k is CheckButton or k is CheckBox):
				heights.append(k.get_global_rect().size.y)
		if heights.size() >= 2 and _spread(heights) > EPS:
			_record(where, "buttons of a row differ in height", "%s: %s" % [_name(box), heights])


## a panel's / margin box's cells sit inside its padding as their size flags say: left / top at the padding, filling cells to the other
## padding too (the padding is the theme's, read from the stylebox / the margin_* constants)
func _check_padding(c: Control, where: String) -> void:
	var rc := c.get_global_rect()
	var m := [0.0, 0.0, 0.0, 0.0]   # left, top, right, bottom
	if c is PanelContainer:
		var sb := c.get_theme_stylebox("panel")
		m = [sb.get_margin(SIDE_LEFT), sb.get_margin(SIDE_TOP), sb.get_margin(SIDE_RIGHT), sb.get_margin(SIDE_BOTTOM)]
	else:
		m = [c.get_theme_constant("margin_left"), c.get_theme_constant("margin_top"), c.get_theme_constant("margin_right"), c.get_theme_constant("margin_bottom")]
	for k in c.get_children():
		if not (k is Control and k.is_visible_in_tree()) or k.top_level or _is_decor(k):
			continue
		var r: Rect2 = k.get_global_rect()
		var okx := _axis_ok(r.position.x, r.end.x, rc.position.x + m[0], rc.end.x - m[2], k.size_flags_horizontal)
		var oky := _axis_ok(r.position.y, r.end.y, rc.position.y + m[1], rc.end.y - m[3], k.size_flags_vertical)
		if not (okx and oky):
			_record(where, "cell off its padding", "%s in %s: %s in %s with padding %s" % [_name(k), _name(c), r, rc, m])


static func _walk_panels(node: Node, out: Array) -> void:
	for k in node.get_children():
		if k is Control and not k.is_visible_in_tree():
			continue
		if k is PanelContainer and _panel_depth(k) == 0:
			out.append(k)
		elif not k is Window and not k is FlowContainer:
			_walk_panels(k, out)


## the window's own constants: the screens, the bottom bar and PLAY share the margins (main.gd SCREEN_MARGIN, RAIL_W), the rail's logo,
## entries and footer start on one x (Style.NAV_PAD)
func _check_window(main: Control, where: String) -> void:
	var k: Dictionary = main.get_script().get_script_constant_map()
	if not (k.has("RAIL_W") and k.has("SCREEN_MARGIN") and k.has("RAIL_INK")):
		_fail("%s: the window's constants RAIL_W / SCREEN_MARGIN / RAIL_INK are gone" % where)
		return
	var left: float = k.RAIL_W + k.SCREEN_MARGIN
	var right: float = main.size.x - k.SCREEN_MARGIN
	var stack: Control = main._screens["play"].get_parent()
	var bar: Control = main._play.get_parent()
	for item in [["the screens", stack.get_global_rect()], ["the bottom bar", bar.get_global_rect()]]:
		if absf(item[1].position.x - left) > EPS or absf(item[1].end.x - right) > EPS:
			_record(where, "margins are not the window's", "%s run %s .. %s, expected %s .. %s" % [item[0], item[1].position.x, item[1].end.x, left, right])
	var play: Rect2 = main._play.get_global_rect()
	if absf(play.end.x - right) > EPS:
		_record(where, "PLAY is off the right margin", "PLAY ends at %s, expected %s" % [play.end.x, right])
	# the screen's blocks (cards, panels) start on the left margin; on a full-width screen the rightmost ends on the right margin
	var blocks := []
	var screen: Control = main._screens[main._screen]
	_walk_panels(screen, blocks)
	if blocks.is_empty():
		_fail("%s: the screen '%s' has no card to measure" % [where, main._screen])
	else:
		var lo := INF
		var hi := -INF
		for blk in blocks:
			lo = minf(lo, blk.get_global_rect().position.x)
			hi = maxf(hi, blk.get_global_rect().end.x)
		if absf(lo - left) > EPS or (main._screen in FULL_WIDTH and absf(hi - right) > EPS):
			_record(where, "screen blocks do not span the margins", "'%s': blocks run %s .. %s, expected %s .. %s" % [main._screen, lo, hi, left,
				right if main._screen in FULL_WIDTH else "(narrower)"])
	# the gap between a screen's heading, tabs and cards is the theme's (the Play screen is a row of two columns, with its own gaps)
	if main._screen != "play":
		for col in screen.get_children():
			if col is BoxContainer and col.get_theme_constant("separation") != StyleScript.SCREEN_GAP:
				_record(where, "gap is not the theme's SCREEN_GAP", "'%s' column has %d, the theme has %d" % [main._screen,
					col.get_theme_constant("separation"), StyleScript.SCREEN_GAP])
	if main._banner.visible:
		var rbn: Rect2 = main._banner.get_global_rect()
		if absf(rbn.position.x - left) > EPS or absf(rbn.end.x - right) > EPS:
			_record(where, "the banner is off the margins", "%s .. %s, expected %s .. %s" % [rbn.position.x, rbn.end.x, left, right])
	var rv: Control = main._nav["play"].get_parent()
	var rail: Rect2 = rv.get_parent().get_parent().get_global_rect()
	var nav_keys := ["play", "news", "multiplayer", "mods", "games", "settings"]
	for i in nav_keys.size():
		var button: Button = main._nav[nav_keys[i]]
		var rect := button.get_global_rect()
		if not rail.grow(EPS).encloses(rect):
			_record(where, "sidebar button exceeds rail", "%s: %s in %s" % [nav_keys[i], rect, rail])
		if absf(rect.position.x - rail.position.x - 13.0) > EPS or absf(rail.end.x - rect.end.x - 13.0) > EPS:
			_record(where, "sidebar insets are not 13", "%s: %s in %s" % [nav_keys[i], rect, rail])
		if absf(button.size.y - 36.0) > EPS or button.focus_mode != Control.FOCUS_ALL:
			_record(where, "sidebar target is not controller-sized", "%s: height %s, focus %s" % [nav_keys[i], button.size.y, button.focus_mode])
		if i > 0:
			var previous: Button = main._nav[nav_keys[i - 1]]
			var pitch := button.get_global_rect().position.y - previous.get_global_rect().position.y
			if absf(pitch - 38.0) > EPS:
				_record(where, "sidebar vertical pitch is not 38", "%s: pitch %s" % [nav_keys[i], pitch])
			if previous.find_valid_focus_neighbor(SIDE_BOTTOM) != button or button.find_valid_focus_neighbor(SIDE_TOP) != previous:
				_record(where, "sidebar directional focus skips a button", nav_keys[i])
	var logo: Control = rv.get_child(0).get_child(0)
	var foot: Control = rv.get_child(rv.get_child_count() - 1).get_child(0)
	var icon: float = main._nav["play"].get_global_rect().position.x + main._nav["play"].get_theme_stylebox("normal").get_margin(SIDE_LEFT)
	if absf(logo.get_global_rect().position.x - icon) > EPS or absf(foot.get_global_rect().position.x - icon) > EPS:
		_record(where, "the rail does not start on one x", "logo %s, entry icons %s, footer %s" % [logo.get_global_rect().position.x, icon, foot.get_global_rect().position.x])


## the tooltip a player gets over `c`
static func _tip_of(c: Control) -> String:
	if c.tooltip_text != "":
		return c.tooltip_text
	return c._get_tooltip(Vector2.ZERO) if c.has_method("_get_tooltip") else ""


static func _spread(values: Array) -> float:
	return (values.max() - values.min()) if not values.is_empty() else 0.0


# ---- the checker's own fixture (a checker that cannot fail proves nothing) ----------------------------------------------------------------

func _selftest() -> void:
	var host := Control.new()
	host.size = Vector2(500, 420)
	root.add_child(host)
	var row := HBoxContainer.new()
	row.position = Vector2(10, 10)
	host.add_child(row)
	var a := Button.new()
	a.text = "Alpha"
	a.custom_minimum_size.y = 30
	var b := Button.new()
	b.text = "Beta"
	b.custom_minimum_size.y = 50
	b.size_flags_vertical = Control.SIZE_SHRINK_BEGIN
	a.size_flags_vertical = Control.SIZE_SHRINK_END
	row.add_child(a)
	row.add_child(b)
	var lonely := Label.new()
	lonely.text = "Outside the window entirely"
	lonely.position = Vector2(480, 400)
	host.add_child(lonely)
	var l1 := Label.new()
	l1.text = "Overlapping one"
	l1.position = Vector2(20, 100)
	host.add_child(l1)
	var l2 := Label.new()
	l2.text = "Overlapping two"
	l2.position = Vector2(30, 105)
	host.add_child(l2)
	var cut := Label.new()
	cut.text = "A very long line that gets cut off by its box"
	cut.clip_text = true
	cut.custom_minimum_size = Vector2(60, 20)
	cut.position = Vector2(20, 200)
	cut.size = Vector2(60, 20)
	host.add_child(cut)
	var n1 := Button.new()
	n1.text = "Near one"
	n1.position = Vector2(200, 100)
	n1.custom_minimum_size.x = 160
	host.add_child(n1)
	var n2 := Button.new()
	n2.text = "Near two"
	n2.position = Vector2(205, 150)
	n2.custom_minimum_size.x = 160
	host.add_child(n2)
	# a block placed by hand 20 px off every edge of its (non-container) parent
	var stage := Control.new()
	stage.position = Vector2(250, 200)
	stage.size = Vector2(240, 120)
	host.add_child(stage)
	var hand := PanelContainer.new()
	hand.position = Vector2(20, 20)
	hand.custom_minimum_size = Vector2(100, 40)
	var hand_label := Label.new()
	hand_label.text = "By hand"
	hand.add_child(hand_label)
	stage.add_child(hand)
	# containers nudged after layout: a gap, a cell off its column, a row short of its edge, a cell off its padding (20 px each)
	var vb := VBoxContainer.new()
	vb.position = Vector2(10, 250)
	for t in ["One", "Two", "Three"]:
		var bb := Button.new()
		bb.text = t
		vb.add_child(bb)
	host.add_child(vb)
	var hb := HBoxContainer.new()
	hb.position = Vector2(150, 250)
	hb.custom_minimum_size.x = 200
	var grow := Label.new()
	grow.text = "Grows"
	grow.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	hb.add_child(grow)
	var tail := Button.new()
	tail.text = "Tail"
	hb.add_child(tail)
	host.add_child(hb)
	var pc := PanelContainer.new()
	pc.position = Vector2(10, 360)
	var pc_label := Label.new()
	pc_label.text = "Padded"
	pc.add_child(pc_label)
	host.add_child(pc)
	await process_frame
	await process_frame
	vb.get_child(1).position.y += 20
	vb.get_child(2).position.x += 20
	tail.position.x -= 20
	pc_label.position.x += 20
	var saved := _found
	_found = {}
	var before := _controls
	var r := _check_layer(host, Rect2(Vector2.ZERO, host.size), "selftest")
	var kinds := {}
	for key in _found:
		kinds[String(key).get_slice(" | ", 0).get_slice(" (", 0)] = true
	for want in ["buttons of a row differ in height", "outside the window", "overlap", "text cut without a tooltip", "almost lined up",
			"placed by hand off its parent's edges", "gap is not the separation", "column cell off its place", "row does not run edge to edge",
			"cell off its padding"]:
		if not kinds.has(want):
			_fail("the checker's own fixture: it did not find '%s' (found %s)" % [want, kinds.keys()])
	if _controls == before or r.count == 0:
		_fail("the checker's own fixture: nothing measured")
	_found = saved
	_controls = before
	host.queue_free()
