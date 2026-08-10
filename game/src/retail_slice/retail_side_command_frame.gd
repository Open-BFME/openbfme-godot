class_name RetailSideCommandFrame
extends Control
## The ornate vertical container behind the builder's right-edge build column
## (user bug #6). Retail 1.06 wraps the InGameSideCommandBar sockets in a tall
## carved/vine strip pinned to the right screen edge, with a scroll terminal at
## each end; the round building icons ride a column that overhangs the strip to
## the left. This node owns that container: its geometry contract, its
## per-faction art slot, and a repository-authored default frame that is
## generated procedurally here (no retail bytes are copied into the repo).
##
## GEOMETRY CONTRACT (measured off the retail reference crop, 147 x 1074 px):
##   * the frame is right-anchored: frame_rect(viewport).end.x == viewport.x
##   * width / height == FRAME_ASPECT (147 / 1074)
##   * the frame spans FRAME_HEIGHT_RATIO of the viewport height, starting at
##     FRAME_TOP_RATIO from the top
##   * the round icon column centers at ICON_COLUMN_CENTER_RATIO across the
##     frame width, with icons ICON_DIAMETER_RATIO of the frame width across
##   * nine sockets fill the column at native size; longer rosters shrink to fit
##
## DROP-IN ART SLOT (the owner's manual art step). Author a transparent PNG at
## the contract aspect and drop it in one of the search roots under the name
## `sidebar_frame_<faction>.png` (or `sidebar_frame_default.png` for a shared
## frame). Resolution order is: every root scanned for the faction file first,
## then every root scanned for the default file, then the procedural frame.
##   1. user://ui/sidebar/                       (drop-in; no rebuild needed)
##   2. <mounted pack>/assets/ui/hud/sidebar/    (shipped with a content pack)
##   3. res://data/base/assets/ui/hud/sidebar/   (committed in-repo)
## Faction keys are the HUD's faction surface keys, lower-cased with spaces
## folded to underscores: men, elves, dwarves, isengard, mordor, wild, angmar.
##
## RETAIL ART, SURVEYED 2026-08-10 (why the default is generated, not extracted).
## Retail's frame is NOT a strip texture and NOT per-faction. It is composed
## per socket out of apt/libingameimagesmain.big -> the atlas page
## `assets/ui/palantir/atlases/apt-libingameimagesmain-1-1e7e4306fa99.png`
## (1024x512, already converted into the Men/BFME2 packs and already whitelisted
## by data/ui/palantir/scene-contract.json, so exact_atlas_texture() would hand
## it out today). BFME2 sub-rects, top-left origin, pixel-identical to RotWK's
## repacked page:
##   SideCommandBar01 (top cap + volute)  Rect2(747, 258, 46, 63)
##   SideCommandBar02..12 (middle bodies) (723,386) (771,386) (819,386)
##       (867,386) (492,337) (366,358) (492,385) (171,357) (318,340) (1,388)
##       (123,357) - each 46x46, one per socket index (sprite 13 labels _1.._11)
##   spellButtonRing (socket ring)        Rect2(675, 382, 46, 46)
## controlbarscheme.ini's per-faction command-bar images are dead in both
## editions (every image key commented out; SMCommandBar/SGCommandBar/... ship
## in no archive), so retail has exactly one shared frame.
## It is NOT bound here because the retail tile column is 46 authored px wide -
## the vine band ends where the socket ring ends - while the frame the owner
## asked for (and the reference crop) carries a band that runs out to the screen
## edge, roughly a third wider than the socket column. Binding the retail tiles
## would not reproduce the requested look, and the per-faction frames the owner
## is authoring replace it anyway. The rects above are recorded so the retail
## band can be wired in one commit if that decision is ever reversed.

const AUTHORED_RESOLUTION := Vector2(1024.0, 768.0)
const FRAME_NODE_NAME := "SideCommandFrame"

## Native size of the authored/generated frame strip. Drop-in art SHOULD use
## this size (or an integer multiple of it) so the icon column lines up.
const REFERENCE_ART_SIZE := Vector2(147.0, 1074.0)
const FRAME_ASPECT := 147.0 / 1074.0
const FRAME_TOP_RATIO := 0.02
const FRAME_HEIGHT_RATIO := 0.96

## Where the round sockets sit inside the frame, as fractions of frame width.
const ICON_COLUMN_CENTER_RATIO := 0.408
const ICON_DIAMETER_RATIO := 0.70
## Vertical inset of the socket band inside the frame (scroll terminals live in
## the inset), as a fraction of frame height.
const ICON_BAND_INSET_RATIO := 0.055
## Socket pitch as a fraction of socket diameter: retail's sockets nearly touch.
const ICON_PITCH_RATIO := 0.96
const MIN_ICON_DIAMETER := 30.0
## Beyond this many sockets a single column would be unreadable, so the roster
## wraps into extra columns growing leftward (the frame covers the primary
## column, as it does in retail where the roster never exceeds one column).
const MAX_SINGLE_COLUMN := 20

const USER_ART_ROOT := "user://ui/sidebar"
const BASE_ART_ROOT := "res://data/base/assets/ui/hud/sidebar"
const PACK_ART_RELATIVE := "assets/ui/hud/sidebar"
const ART_PREFIX := "sidebar_frame_"
const ART_DEFAULT_NAME := "sidebar_frame_default.png"

## Procedural default palette: the repository's own gold/parchment chrome
## (openbfme_theme language), never a retail texture.
const FRAME_OUTLINE := Color("#1d1810")
const FRAME_SHADOW := Color("#2e2717")
const FRAME_BODY := Color("#7d6f4a")
const FRAME_HIGHLIGHT := Color("#dccd9e")
const BAND_LEFT_RATIO := 0.42
const BAND_RIGHT_INSET := 3.0
## Rope twist: grooves repeat every TWIST_PERIOD px and lean across the band by
## TWIST_LEAN px, so the braid reads diagonally like the retail vine strip.
const TWIST_PERIOD := 40.0
const TWIST_LEAN := 17.0

static var _default_texture: Texture2D = null

var _art: Texture2D = null
var _art_path := ""


func _init() -> void:
	name = FRAME_NODE_NAME
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS
	z_index = -1


# ---------------------------------------------------------------- geometry --


static func frame_rect(viewport: Vector2) -> Rect2:
	## The container rect in bar-local (== screen) coordinates.
	var size := viewport
	if size.x <= 0.0 or size.y <= 0.0:
		size = AUTHORED_RESOLUTION
	var height := size.y * FRAME_HEIGHT_RATIO
	var width := height * FRAME_ASPECT
	return Rect2(size.x - width, size.y * FRAME_TOP_RATIO, width, height)


static func icon_column_center_x(viewport: Vector2) -> float:
	var rect := frame_rect(viewport)
	return rect.position.x + rect.size.x * ICON_COLUMN_CENTER_RATIO


static func icon_diameter(viewport: Vector2) -> float:
	return frame_rect(viewport).size.x * ICON_DIAMETER_RATIO


static func icon_band(viewport: Vector2) -> Vector2:
	## Returns (top_y, bottom_y) of the socket band inside the frame.
	var rect := frame_rect(viewport)
	var inset := rect.size.y * ICON_BAND_INSET_RATIO
	return Vector2(rect.position.y + inset, rect.end.y - inset)


# ------------------------------------------------------------- art lookup --


static func default_search_roots(pack_roots: Array = []) -> Array[String]:
	var roots: Array[String] = [USER_ART_ROOT]
	for value in pack_roots:
		var root := String(value).strip_edges().replace("\\", "/")
		while root.ends_with("/"):
			root = root.substr(0, root.length() - 1)
		if root == "":
			continue
		roots.append("%s/%s" % [root, PACK_ART_RELATIVE])
	roots.append(BASE_ART_ROOT)
	return roots


static func faction_art_key(faction: String) -> String:
	return faction.strip_edges().to_lower().replace(" ", "_").replace("-", "_")


static func resolve_frame_art_path(faction: String, search_roots: Array) -> String:
	## Faction art in ANY root beats default art in a higher-priority root: the
	## per-faction slot is the whole point, a shared default is only a fallback.
	var key := faction_art_key(faction)
	if key != "":
		for root_value in search_roots:
			var path := "%s/%s%s.png" % [String(root_value), ART_PREFIX, key]
			if FileAccess.file_exists(path):
				return path
	for root_value in search_roots:
		var default_path := "%s/%s" % [String(root_value), ART_DEFAULT_NAME]
		if FileAccess.file_exists(default_path):
			return default_path
	return ""


static func load_frame_texture(path: String) -> Texture2D:
	if path == "":
		return null
	if path.begins_with("res://"):
		var imported := load(path) as Texture2D
		if imported != null:
			return imported
		# A PNG dropped into the repo slot without a Godot .import sidecar still
		# loads off disk in a non-exported run; only exported builds need the
		# reimport. Drops into user:// or a pack never need one.
		var globalized := ProjectSettings.globalize_path(path)
		if globalized == "" or not FileAccess.file_exists(globalized):
			return null
		path = globalized
	var image := Image.new()
	if image.load(path) != OK:
		push_warning("[SideCommandFrame] could not load frame art: %s" % path)
		return null
	return ImageTexture.create_from_image(image)


func bind_faction(faction: String, pack_roots: Array = []) -> String:
	## Returns the resolved drop-in art path, or "" when the procedural default
	## frame is used. Safe to call before the node enters the tree.
	var roots := default_search_roots(pack_roots)
	var path := resolve_frame_art_path(faction, roots)
	if path == _art_path:
		return _art_path
	_art_path = path
	_art = load_frame_texture(path)
	if _art == null:
		_art_path = ""
	queue_redraw()
	return _art_path


func frame_art() -> Texture2D:
	return _art


func frame_art_path() -> String:
	return _art_path


# ------------------------------------------------------------------ paint --


static func default_frame_texture() -> Texture2D:
	if _default_texture == null:
		_default_texture = ImageTexture.create_from_image(_build_default_image())
	return _default_texture


func _draw() -> void:
	var texture: Texture2D = _art if _art != null else default_frame_texture()
	if texture == null or size.x <= 0.0 or size.y <= 0.0:
		return
	draw_texture_rect(texture, Rect2(Vector2.ZERO, size), false)


static func _build_default_image() -> Image:
	## Repository-authored vertical frame: a twisted vine/rope band down the
	## right edge with a scroll terminal at each end, and a clear icon column on
	## the left where the round build sockets sit.
	var width := int(REFERENCE_ART_SIZE.x)
	var height := int(REFERENCE_ART_SIZE.y)
	var image := Image.create(width, height, false, Image.FORMAT_RGBA8)
	image.fill(Color(0.0, 0.0, 0.0, 0.0))
	var band_left := float(width) * BAND_LEFT_RATIO
	var band_right := float(width) - BAND_RIGHT_INSET
	var band_width := maxf(1.0, band_right - band_left)
	var body_top := float(height) * 0.05
	var body_bottom := float(height) * 0.95
	for y in height:
		var fy := float(y)
		var end_fade := 1.0
		if fy < body_top or fy > body_bottom:
			continue
		if fy < body_top + 12.0:
			end_fade = (fy - body_top) / 12.0
		elif fy > body_bottom - 12.0:
			end_fade = (body_bottom - fy) / 12.0
		for x in range(int(band_left), width):
			var u := (float(x) - band_left) / band_width
			if u < 0.0 or u > 1.0:
				continue
			# Cylinder shading across the band plus a diagonal rope groove.
			var roundness := sin(clampf(u, 0.0, 1.0) * PI)
			var groove := sin((fy + u * TWIST_LEAN) / TWIST_PERIOD * TAU)
			var lit := clampf(roundness * 0.85, 0.0, 1.0)
			var color := FRAME_SHADOW.lerp(FRAME_HIGHLIGHT, lit).lerp(FRAME_BODY, 0.3)
			if groove < 0.0:
				# Carved groove between the rope strands: darken hard so the
				# braid reads at HUD scale instead of washing into a gradient.
				color = color.lerp(FRAME_SHADOW, absf(groove) * 0.62)
			else:
				color = color.lerp(FRAME_HIGHLIGHT, groove * 0.26)
			# Dark rim down both band edges, like the retail carved strip.
			var edge_distance := minf(u, 1.0 - u)
			if edge_distance < 0.11:
				color = color.lerp(FRAME_OUTLINE, 1.0 - edge_distance / 0.11)
			var alpha := end_fade
			if u < 0.02:
				alpha *= u / 0.02
			elif u > 0.985:
				alpha *= (1.0 - u) / 0.015
			color.a = clampf(alpha, 0.0, 1.0)
			image.set_pixel(x, y, color)
	var spiral_x := band_left + band_width * 0.5
	_stamp_spiral(image, Vector2(spiral_x, 48.0), 1.0)
	_stamp_spiral(image, Vector2(spiral_x, float(height) - 48.0), -1.0)
	return image


static func _stamp_spiral(image: Image, center: Vector2, direction: float) -> void:
	## Scroll terminal: a tapering volute that grows out of the band end, drawn
	## outline-first so it keeps the same carved edge as the strip.
	var turns := 2.35
	var span := turns * TAU
	var steps := 520
	# Outline pass first, then the lit body: stamping them per-step would let a
	# later step's outline eat the previous step's body along the overlap.
	for pass_index in 2:
		for step in steps + 1:
			var t := span * float(step) / float(steps)
			var progress := t / span
			var radius := 5.0 + progress * 34.0
			var point := center + Vector2(
				cos(t) * radius * 0.78,
				sin(t) * radius * 0.92 * direction
			)
			var stroke := 3.0 + progress * 3.4
			if pass_index == 0:
				_stamp_disc(image, point, stroke + 1.7, FRAME_OUTLINE)
			else:
				var body := FRAME_SHADOW.lerp(FRAME_HIGHLIGHT, 0.42 + 0.36 * progress)
				_stamp_disc(image, point, stroke, body.lerp(FRAME_BODY, 0.25))


static func _stamp_disc(image: Image, point: Vector2, radius: float, color: Color) -> void:
	## Alpha-over composite so the body pass covers the outline pass cleanly and
	## the outer edge stays anti-aliased against transparency.
	var min_x := maxi(0, int(floor(point.x - radius)))
	var max_x := mini(image.get_width() - 1, int(ceil(point.x + radius)))
	var min_y := maxi(0, int(floor(point.y - radius)))
	var max_y := mini(image.get_height() - 1, int(ceil(point.y + radius)))
	for y in range(min_y, max_y + 1):
		for x in range(min_x, max_x + 1):
			var distance := Vector2(float(x), float(y)).distance_to(point)
			if distance > radius:
				continue
			var alpha := clampf((radius - distance) / 1.4, 0.0, 1.0)
			if alpha <= 0.0:
				continue
			var existing := image.get_pixel(x, y)
			var out := color
			if existing.a > 0.001:
				out = existing.lerp(color, alpha)
			out.a = maxf(existing.a, alpha)
			image.set_pixel(x, y, out)
