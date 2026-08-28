class_name RetailHudStage
extends RefCounted
## The ONE 1024x768 APT stage -> viewport transform for the in-game HUD.
##
## Retail's in-game HUD is a single APT stage. `Palantir.apt`'s root movie
## header declares width 1024 / height 768 and every element in it - the radar
## globe, the command dish, the six command sockets, the resource bar, the hero
## roster movie, the help box - is a PlaceObject at a stage coordinate. Those
## coordinates live in `RetailHudAptRuntime.PALANTIR_STAGE_PLACEMENTS`; this
## class is the only thing that turns them into viewport pixels.
##
## THE MAPPING IS AN EXACT FIT, NOT A UNIFORM SCALE. The stage is stretched
## independently on each axis to fill the viewport. Two independent proofs:
##
##   * The owner's retail RotWK captures (2560x1440) put the radar globe centre
##     at ~(307, 1190) and the command dish centre at ~(696, 1235). Authored
##     stage centres are [129.8, 640.1] and [280.2, 660.9]. 129.8 * 2560/1024 =
##     324.6 and 280.2 * 2560/1024 = 700.5; 640.1 * 1440/768 = 1200 and
##     660.9 * 1440/768 = 1239. A UNIFORM 2560/1024 scale would put the radar
##     centre 70 px too high.
##   * The radar and the command dish are the SAME authored circular globe art
##     placed twice. In every 16:9 retail capture both render as WIDE OVALS.
##     A uniform scale cannot turn a circle into an oval; an exact fit does.
##
## Q37/Q38/Q39 all route through `to_viewport` / `to_dock` so the HUD has one
## coordinate story instead of three sets of hand-measured constants.

const APT_RUNTIME := preload("res://src/retail_slice/retail_hud_apt_runtime.gd")

## Palantir.apt root movie header (sha256 c629e2b6...).
const STAGE_SIZE := Vector2(1024.0, 768.0)
## `PalantirFrame` is authored at stage [0, 512] with an identity matrix, so the
## control-bar band is the bottom 256 stage rows across the full 1024 width.
const DOCK_STAGE_TOP := 512.0
const DOCK_STAGE_SIZE := Vector2(1024.0, 256.0)
## `game/project.godot` display/window/size: the design viewport every HUD
## layout constant in `retail_hud.gd` is expressed in.
const DESIGN_VIEWPORT := Vector2(1920.0, 1080.0)


## Per-axis stage -> viewport scale. Exact fit: see the class comment.
static func scale_for(viewport: Vector2) -> Vector2:
	var width := maxf(1.0, viewport.x)
	var height := maxf(1.0, viewport.y)
	return Vector2(width / STAGE_SIZE.x, height / STAGE_SIZE.y)


## A stage point in viewport pixels.
static func to_viewport(stage_point: Vector2, viewport: Vector2 = DESIGN_VIEWPORT) -> Vector2:
	return stage_point * scale_for(viewport)


## A stage length in viewport pixels (per-axis, so circles become the retail
## ovals rather than being silently rounded back to circles).
static func scale_size(stage_size: Vector2, viewport: Vector2 = DESIGN_VIEWPORT) -> Vector2:
	return stage_size * scale_for(viewport)


## The control-bar dock: full viewport width, the bottom 256 stage rows.
static func dock_rect(viewport: Vector2 = DESIGN_VIEWPORT) -> Rect2:
	var origin := to_viewport(Vector2(0.0, DOCK_STAGE_TOP), viewport)
	return Rect2(origin, to_viewport(Vector2(STAGE_SIZE.x, STAGE_SIZE.y), viewport) - origin)


## A stage point in dock-local pixels (dock origin = stage [0, 512]).
static func to_dock(stage_point: Vector2, viewport: Vector2 = DESIGN_VIEWPORT) -> Vector2:
	return to_viewport(stage_point - Vector2(0.0, DOCK_STAGE_TOP), viewport)


## A named `Palantir.apt` root placement, in dock-local pixels.
static func placement_dock(name: String, viewport: Vector2 = DESIGN_VIEWPORT) -> Vector2:
	var placement: Vector2 = APT_RUNTIME.PALANTIR_STAGE_PLACEMENTS.get(name, Vector2.ZERO)
	return to_dock(placement, viewport)


## MEASURED registration correction for every seat `CommandButtons` (sprite
## character 114) carries. The movie's translations for the glass/subMenu
## children register the imported art's corner, not its centre: blob-centroiding
## the six empty sockets in BOTH retail 2560x1440 captures
## (reference/in game ui.jpg and reference/game.dat_5VsCUnKZ04.jpg — identical
## within 0.2 px of each other) puts every socket centre at authored
## + (32.42, 24.38) dock px with a per-slot spread under 0.7 px. One uniform
## offset across all six slots is a parent-registration correction, so it
## applies to the whole seat family (glass0..5 AND the subMenu0..3 ring), in
## stage units so every viewport recomputes it the same way.
const COMMAND_SEAT_REGISTRATION_STAGE := Vector2(17.29, 17.34)


## Authored command-socket CENTRE `index` (0..5) in dock-local pixels: the
## `CommandButtons` stage placement plus the measured registration correction
## plus the authored local offset.
static func command_slot_center_dock(index: int, viewport: Vector2 = DESIGN_VIEWPORT) -> Vector2:
	var origin: Vector2 = APT_RUNTIME.PALANTIR_STAGE_PLACEMENTS["CommandButtons"]
	var local: Vector2 = APT_RUNTIME.PALANTIR_COMMAND_SLOT_LOCAL[index]
	return to_dock(origin + COMMAND_SEAT_REGISTRATION_STAGE + local, viewport)


## Authored command-socket TOP-LEFT for a button of `button_size`, in
## dock-local pixels.
static func command_slot_dock(
	index: int, button_size: Vector2, viewport: Vector2 = DESIGN_VIEWPORT
) -> Vector2:
	return command_slot_center_dock(index, viewport) - button_size * 0.5


## Authored SUB-MENU ring seat `index` CENTRE in dock-local pixels.
##
## Seats 0..3 are the `subMenu0..subMenu3` placements of `Palantir.apt` sprite
## character 114, used verbatim. Seat 4 and beyond continue the SAME ring: the
## authored mean angular step past the last authored seat, at the authored mean
## radius. Nothing here is measured off a capture - every number is derived from
## `RetailHudAptRuntime.PALANTIR_SUBMENU_SLOT_LOCAL`.
##
## Per-axis dock scaling turns the authored circle into the same wide oval the
## radar and the dish become, which is what retail 16:9 shows.
static func submenu_slot_center_dock(
	index: int, viewport: Vector2 = DESIGN_VIEWPORT
) -> Vector2:
	var origin: Vector2 = APT_RUNTIME.PALANTIR_STAGE_PLACEMENTS["CommandButtons"]
	return to_dock(
		origin + COMMAND_SEAT_REGISTRATION_STAGE + submenu_slot_local(index), viewport
	)


## Authored SUB-MENU ring seat `index` in `CommandButtons`-local STAGE units.
static func submenu_slot_local(index: int) -> Vector2:
	var authored: Array = APT_RUNTIME.PALANTIR_SUBMENU_SLOT_LOCAL
	if index < 0:
		index = 0
	if index < authored.size():
		return authored[index] as Vector2
	# Continue the authored ring. Angles are measured from straight up, the
	# direction `subMenu0` points, and increase clockwise, the direction the
	# authored four run.
	var last_angle := _submenu_slot_angle(authored.size() - 1)
	var angle := last_angle + submenu_ring_step() * float(index - authored.size() + 1)
	var radius := submenu_ring_radius()
	return Vector2(sin(angle) * radius, -cos(angle) * radius)


## Polar angle (radians, clockwise from straight up) of an AUTHORED seat.
static func _submenu_slot_angle(index: int) -> float:
	var local: Vector2 = APT_RUNTIME.PALANTIR_SUBMENU_SLOT_LOCAL[index]
	return atan2(local.x, -local.y)


## Mean angular step of the authored four seats, in radians.
static func submenu_ring_step() -> float:
	var authored: Array = APT_RUNTIME.PALANTIR_SUBMENU_SLOT_LOCAL
	if authored.size() < 2:
		return 0.0
	var first := _submenu_slot_angle(0)
	var last := _submenu_slot_angle(authored.size() - 1)
	return (last - first) / float(authored.size() - 1)


## Mean radius of the authored four seats, in stage units.
static func submenu_ring_radius() -> float:
	var authored: Array = APT_RUNTIME.PALANTIR_SUBMENU_SLOT_LOCAL
	var total := 0.0
	for value in authored:
		total += (value as Vector2).length()
	return total / float(maxi(1, authored.size()))


## Authored SUB-MENU ring seat TOP-LEFT for a button of `button_size`, in
## dock-local pixels.
static func submenu_slot_dock(
	index: int, button_size: Vector2, viewport: Vector2 = DESIGN_VIEWPORT
) -> Vector2:
	return submenu_slot_center_dock(index, viewport) - button_size * 0.5


## The first `count` authored command SEAT centres in dock-local pixels, in the
## order a paged command range fills them: the six `glass0..glass5` sockets, then
## the `subMenu0..subMenu3` ring and its authored continuation.
##
## This is the seat set `retail_hud.gd _radial_button_position` lays a page out
## on, and the set every geometry gate derives its bounds from - so a gate cannot
## hold production to a rule `Palantir.apt` itself breaks.
static func command_seat_centers_dock(
	count: int, viewport: Vector2 = DESIGN_VIEWPORT
) -> Array[Vector2]:
	var sockets: int = APT_RUNTIME.PALANTIR_COMMAND_SLOT_LOCAL.size()
	var seats: Array[Vector2] = []
	for index in maxi(0, count):
		if index < sockets:
			seats.append(command_slot_center_dock(index, viewport))
		else:
			seats.append(submenu_slot_center_dock(index - sockets, viewport))
	return seats


## A `ResourceBar` child (`Resources`, `CommandPoints`, ...) in dock-local
## pixels.
static func resource_child_dock(name: String, viewport: Vector2 = DESIGN_VIEWPORT) -> Vector2:
	var origin: Vector2 = APT_RUNTIME.PALANTIR_STAGE_PLACEMENTS["ResourceBar"]
	var local: Vector2 = APT_RUNTIME.PALANTIR_RESOURCE_BAR_LOCAL.get(name, Vector2.ZERO)
	return to_dock(origin + local, viewport)


## The authored `ResourceIcon` quad, in dock-local pixels. The movie authors the
## SHAPE (18 x 20.04 stage units at the bar's left end); the engine binds the
## art, so this is where the retail currency icon goes and how big it is.
static func resource_icon_rect_dock(viewport: Vector2 = DESIGN_VIEWPORT) -> Rect2:
	var quad: Rect2 = APT_RUNTIME.PALANTIR_RESOURCE_ICON_QUAD
	return Rect2(
		to_dock(quad.position, viewport), scale_size(quad.size, viewport)
	)


## The authored text box of a `ResourceBar` text child, in dock-local pixels.
static func resource_text_rect_dock(name: String, viewport: Vector2 = DESIGN_VIEWPORT) -> Rect2:
	var spec: Dictionary = APT_RUNTIME.PALANTIR_RESOURCE_TEXT[name]
	var bounds: Rect2 = spec["bounds"]
	var origin := resource_child_dock(name, viewport) + to_viewport(
		(spec["local"] as Vector2) + bounds.position, viewport
	) - to_viewport(Vector2.ZERO, viewport)
	return Rect2(origin, scale_size(bounds.size, viewport))


## Hero roster cell `index` (0-based) origin in VIEWPORT pixels: the authored
## `HeroSelectUI` stage placement [375, 700] plus the authored 70-unit pitch.
static func hero_cell_viewport(index: int, viewport: Vector2 = DESIGN_VIEWPORT) -> Vector2:
	var row := index / APT_RUNTIME.HERO_SELECT_ROW_LENGTH
	var column := index % APT_RUNTIME.HERO_SELECT_ROW_LENGTH
	var scale := hero_cell_scale(viewport)
	return hero_cell_anchor(viewport) + Vector2(
		APT_RUNTIME.HERO_SELECT_PITCH * float(column),
		APT_RUNTIME.HERO_SELECT_ROW_OFFSET * float(row)
	) * scale - APT_RUNTIME.HERO_CELL_PORTRAIT_CENTER_LOCAL * scale


## The FIRST cell's portrait CENTRE in viewport pixels — the one point of the
## hero roster that is pinned to the dock's own transform, so the row keeps
## sitting where the owner and the retail capture both have it.
static func hero_cell_anchor(viewport: Vector2 = DESIGN_VIEWPORT) -> Vector2:
	var origin: Vector2 = APT_RUNTIME.PALANTIR_STAGE_PLACEMENTS["HeroSelectUI"]
	return to_viewport(
		origin
		+ APT_RUNTIME.HERO_SELECT_FIRST_LOCAL
		+ APT_RUNTIME.HERO_CELL_PORTRAIT_CENTER_LOCAL,
		viewport
	)


## InGameHeroSelect composes UNIFORMLY, so the hero cell keeps ONE scale on
## both axes.
##
## The movie authors the cell as a CIRCLE: `SelectedHighlight` and
## `AttackedFlash` both centre on local [28, 28] and the highlight art is the
## square [-29.5, -29.5]..[29.5, 29.5] about it, with every `Hero1..Hero16`
## placement at matrix [1, 0, 0, 1] and a 70-unit pitch
## (InGameHeroSelect.apt PlaceObject records at offsets 27240..28200).
## Feeding that circle the dock's PER-AXIS stage scale (1.875 x / 1.40625 y at
## 1920x1080) stretched it into a 111 x 83 ellipse — the owner's 2026-08-27
## playtest, "this ui for heros is broken". Retail's own capture settles which
## axis is right: in reference/game.dat_6rULTVkae1.jpg (2560x1440) the cell-0
## highlight measures ~110 px across, i.e. 59 authored units at the HEIGHT
## scale 1440/768, with the 1024-wide stage centred. So the uniform scale is
## the height scale, and the cell is 83 x 83 at 1920x1080 — exactly retail's
## 110 px scaled to that viewport.
static func hero_cell_scale(viewport: Vector2 = DESIGN_VIEWPORT) -> float:
	return scale_for(viewport).y


## A hero-cell stage length in viewport pixels, uniformly scaled.
static func hero_scale_size(stage_size: Vector2, viewport: Vector2 = DESIGN_VIEWPORT) -> Vector2:
	return stage_size * hero_cell_scale(viewport)


## The `SelectAllHeroesBttn` origin in VIEWPORT pixels.
static func hero_select_all_viewport(viewport: Vector2 = DESIGN_VIEWPORT) -> Vector2:
	# Local to the same cell-0 anchor the portraits use, so the button keeps
	# its authored offset from the first portrait under the uniform scale.
	return hero_cell_anchor(viewport) + (
		APT_RUNTIME.HERO_SELECT_ALL_BUTTON_LOCAL
		- APT_RUNTIME.HERO_SELECT_FIRST_LOCAL
		- APT_RUNTIME.HERO_CELL_PORTRAIT_CENTER_LOCAL
	) * hero_cell_scale(viewport)


## The authored hero cell size in viewport pixels: the portrait circle plus the
## health-bar quad's reach below it (geometry 73.ru, half-height 17 at local
## y 49.85).
static func hero_cell_size(viewport: Vector2 = DESIGN_VIEWPORT) -> Vector2:
	var width := APT_RUNTIME.HERO_CELL_PORTRAIT_RADIUS * 2.0
	var height := (
		APT_RUNTIME.HERO_CELL_HEALTH_BAR_LOCAL.y
		+ APT_RUNTIME.HERO_CELL_HEALTH_BAR_QUAD.y * 0.5
	)
	return hero_scale_size(Vector2(width, height), viewport)


## Half-angle (radians) of the hero health arc.
##
## Retail paints the curve into the health-bar TEXTURE - geometry 73.ru is a
## flat 77 x 34 quad, so there is no authored arc sweep to read. This is the
## documented approximation: the quad is centred at hero-cell local
## [25.35, 49.85], i.e. 21.85 below the portrait centre [28, 28], and its top
## edge is 17 units above that, so the band the retail art occupies starts
## 4.85 units below the portrait centre. The arc is the part of the portrait
## circle (radius 29.5) below that line: half-angle acos(4.85 / 29.5).
static func hero_health_arc_half_angle() -> float:
	return acos(
		clampf(
			(
				APT_RUNTIME.HERO_CELL_HEALTH_BAR_LOCAL.y
				- APT_RUNTIME.HERO_CELL_HEALTH_BAR_QUAD.y * 0.5
				- APT_RUNTIME.HERO_CELL_PORTRAIT_CENTER_LOCAL.y
			) / APT_RUNTIME.HERO_CELL_PORTRAIT_RADIUS,
			-1.0,
			1.0
		)
	)
