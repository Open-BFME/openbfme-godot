## Pixel regressions for the FX renderer (lane FX-1 review round). Needs a real display and the retail files:
##
##   DISPLAY=:1 godot --path godot --script res://tests/fx_render_test.gd
##
## A headless run prints SKIP (exit 77): the dummy renderer produces no pixels. Every case builds particle systems from INI text through
## FXPlayer.define_ini (unknown ParticleName files draw retail's 1x1 white default texture) and reads the centre pixel of a cell in the
## window. The camera is orthographic and looks down -Z; the particle colours and alphas are chosen so that a wrong compositing order,
## a missing depth write or a wrong alpha reference gives a different, far apart pixel.
extends SceneTree

const EXIT_PASS := 0
const EXIT_FAIL := 1
const EXIT_SKIP := 77
const ORTHO_SIZE := 400.0

var _failures := 0
var _fx: Node3D
var _camera: Camera3D
var _defined := 0


func _initialize() -> void:
	var code: int = await _run()
	print("FX RENDER %s" % ["PASS" if code == EXIT_PASS else ("SKIP" if code == EXIT_SKIP else "FAIL")])
	quit(code)


func _check(ok: bool, what: String) -> void:
	if ok:
		print("  ok   ", what)
	else:
		print("  FAIL ", what)
		_failures += 1


func _run() -> int:
	if DisplayServer.get_name() == "headless":
		print("SKIP: the FX render test needs a display (run it with DISPLAY=:1, not --headless)")
		return EXIT_SKIP
	var rotwk := OS.get_environment("ROTWK_INSTALL")
	var bfme2 := OS.get_environment("BFME2_INSTALL")
	if rotwk.is_empty() or bfme2.is_empty():
		print("SKIP: set ROTWK_INSTALL and BFME2_INSTALL")
		return EXIT_SKIP
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = fs.mount_retail()
	if not mount.ok:
		print("FAIL: mount_retail: ", mount.errors)
		return EXIT_FAIL
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0, 0, 0)
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	var we := WorldEnvironment.new()
	we.environment = env
	root.add_child(we)
	_camera = Camera3D.new()
	_camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	_camera.size = ORTHO_SIZE
	_camera.position = Vector3(0, 0, 600)
	_camera.far = 5000.0
	root.add_child(_camera)
	_camera.current = true
	_fx = ClassDB.instantiate("FXPlayer")
	root.add_child(_fx)
	var setup: Dictionary = _fx.setup(fs)
	_check(setup.ok, "FXPlayer.setup %s" % [setup.errors])
	# lane FX-3 round 2: soft particles are on by default (project setting openbfme/rendering/soft_particles); every case below but the last pins the
	# retail look, so they run with the retail mode
	_check(_fx.soft_particles == true, "soft particles are on by default")
	_fx.soft_particles = false
	await _case_aba()
	await _case_deferred()
	await _case_alpha_test_depth()
	await _case_alpha_reference()
	await _case_many_batches()
	await _case_gamma_alpha()
	await _case_saturation()
	await _case_dust_soft_edges()
	await _case_soft_ground()
	return EXIT_PASS if _failures == 0 else EXIT_FAIL


## One particle (burst of 1 at the first step), alive for the whole test, at the origin of the played position.
func _define(shader: String, texture: String, color: Color, alpha: float, sort_level := 0, size := 60.0) -> String:
	_defined += 1
	var name := "RT%d" % _defined
	var text := "FXParticleSystem %s\n  System\n    Priority = ALWAYS_RENDER\n    Shader = %s\n    ParticleName = %s\n    Lifetime = 900 900\n    Size = %s %s\n" % [name, shader, texture, size, size]
	text += "    BurstCount = 1 1\n    BurstDelay = 1000 1000\n    SortLevel = %d\n  End\n" % sort_level
	text += "  EmissionVolume = PointEmissionVolume\n  End\n  EmissionVelocity = OrthoEmissionVelocity\n  End\n"
	text += "  Color = DefaultColor\n    Color1 = R:%d G:%d B:%d 0\n  End\n" % [roundi(color.r * 255.0), roundi(color.g * 255.0), roundi(color.b * 255.0)]
	text += "  Alpha = DefaultAlpha\n    Alpha1 = %s %s 0\n  End\n  Update = DefaultUpdate\n  End\n  Draw = DefaultDraw\n  End\nEnd\n" % [alpha, alpha]
	var errors: Array = _fx.define_ini(name + ".ini", text)
	if not errors.is_empty():
		print("  FAIL define ", name, " ", errors)
		_failures += 1
	return name


func _play(name: String, at: Vector3) -> void:
	if _fx.play_particle_system(name, at) == 0:
		print("  FAIL play ", name)
		_failures += 1


func _settle() -> void:
	_fx.step_once()
	_fx.rebuild()
	for i in 3:
		await process_frame
	await RenderingServer.frame_post_draw


func _pixel(world_x: float, world_y: float) -> Color:
	var img: Image = root.get_texture().get_image()
	var scale := float(img.get_height()) / ORTHO_SIZE
	var px := int(img.get_width() * 0.5 + world_x * scale)
	var py := int(img.get_height() * 0.5 - world_y * scale)
	# lane RENDER-3 (S-831): the displayed value itself. Retail blends the gamma-encoded values the display shows (D3D9, no sRGB conversion), so the
	# expected pixels below are retail's gamma-space results
	return img.get_pixel(px, py)


func _near(c: Color, r: float, g: float, b: float, tol := 0.08) -> bool:
	return absf(c.r - r) <= tol and absf(c.g - g) <= tol and absf(c.b - b) <= tol


func _fmt(c: Color) -> String:
	return "(%.4f, %.4f, %.4f)" % [c.r, c.g, c.b]


## smoke / fire / smoke: A1 and A2 share a texture and a state, B (additive) sits between them; alpha and additive do not commute.
## Correct order A1, B, A2: blue -> +red = (1,0,1) -> half green over it = (.5,.5,.5). Merging A1 with A2 gives (1,.5,.5).
func _case_aba() -> void:
	_fx.clear()
	_play(_define("ALPHA", "TexA.tga", Color(0, 0, 1), 1.0), Vector3.ZERO)
	_play(_define("ADDITIVE", "TexB.tga", Color(1, 0, 0), 1.0), Vector3.ZERO)
	_play(_define("ALPHA", "TexA.tga", Color(0, 1, 0), 0.5), Vector3.ZERO)
	await _settle()
	var c := _pixel(0, 0)
	_check(_near(c, 0.5, 0.5, 0.5), "smoke / fire / smoke composites in submission order: %s (merged would be (1, .5, .5))" % _fmt(c))
	var children := 0
	for n in _fx.get_children():
		if n is MultiMeshInstance3D or n is MeshInstance3D:
			children += 1
	_check(children == 3, "the three systems are three nodes, not two (%d)" % children)


## Two deferred (SortLevel 1) systems: drawn in reverse list order, never depth sorted. D1 is far, D2 near; reverse order draws D2 then D1.
func _case_deferred() -> void:
	_fx.clear()
	_play(_define("ALPHA", "TexD.tga", Color(1, 0, 0), 0.5, 1), Vector3(0, 0, -50))
	_play(_define("ALPHA", "TexD.tga", Color(0, 1, 0), 0.5, 1), Vector3(0, 0, 50))
	await _settle()
	var c := _pixel(0, 0)
	_check(_near(c, 0.5, 0.25, 0.0, 0.06), "deferred systems draw in reverse list order without depth sorting: %s (depth sorted would be (.25, .5, 0))" % _fmt(c))
	var unverified: PackedStringArray = _fx.get_unverified()
	_check("\n".join(unverified).contains("S-198"), "the S-198 lines are reported")


## ALPHA_TEST (ONE/ZERO, reference 96, depth write): the front test particle must hide the additive particle behind it that is drawn after it.
func _case_alpha_test_depth() -> void:
	_fx.clear()
	_play(_define("ALPHA_TEST", "TexT.tga", Color(0, 1, 0), 1.0), Vector3(0, 0, 50))
	_play(_define("ADDITIVE", "TexU.tga", Color(1, 0, 0), 1.0), Vector3(0, 0, -50))
	await _settle()
	var c := _pixel(0, 0)
	_check(_near(c, 0.0, 1.0, 0.0, 0.05), "an alpha-test particle writes depth and hides the later additive particle behind it: %s" % _fmt(c))


## The alpha references: 96 passes and 95 is discarded for ALPHA_TEST; 1 passes and 0 is discarded for ADDITIVE_ALPHA_TEST.
func _case_alpha_reference() -> void:
	_fx.clear()
	_play(_define("ALPHA_TEST", "TexV.tga", Color(0, 1, 0), 0.3765), Vector3(-150, 0, 0)) # byte 96
	_play(_define("ALPHA_TEST", "TexW.tga", Color(0, 1, 0), 0.3733), Vector3(0, 0, 0)) # byte 95
	_play(_define("ADDITIVE_ALPHA_TEST", "TexX.tga", Color(1, 1, 1), 0.00393), Vector3(150, 0, 0)) # byte 1
	_play(_define("ADDITIVE_ALPHA_TEST", "TexY.tga", Color(1, 1, 1), 0.0039), Vector3(0, 100, 0)) # byte 0
	await _settle()
	var c96 := _pixel(-150, 0)
	var c95 := _pixel(0, 0)
	var c1 := _pixel(150, 0)
	var c0 := _pixel(0, 100)
	_check(_near(c96, 0.0, 1.0, 0.0, 0.05), "alpha byte 96 passes the ALPHA_TEST reference: %s" % _fmt(c96))
	_check(_near(c95, 0.0, 0.0, 0.0, 0.02), "alpha byte 95 is discarded by the ALPHA_TEST reference: %s" % _fmt(c95))
	_check(_near(c1, 1.0, 1.0, 1.0, 0.05), "alpha byte 1 passes the additive alpha test: %s" % _fmt(c1))
	_check(_near(c0, 0.0, 0.0, 0.0, 0.02), "alpha byte 0 is discarded by the additive alpha test: %s" % _fmt(c0))


## 300 batches (more than the 120 material priorities): ten overlapping pairs (first red, second green, opaque ALPHA) among filler systems that are
## off screen; the second of every pair must end on top. Each system has its own texture name, so each is its own batch.
func _case_many_batches() -> void:
	_fx.clear()
	var pair_first := [10, 118, 120, 122, 150, 200, 254, 256, 258, 298]
	var cells := []
	var first_of := {}
	for k in pair_first:
		first_of[k] = true
	var cell := 0
	var i := 0
	while i < 300:
		var tex := "TexMany%d.tga" % i
		if first_of.has(i):
			var x := -280.0 + float(cell % 5) * 140.0
			var y := 90.0 if cell < 5 else -90.0
			cells.append(Vector2(x, y))
			_play(_define("ALPHA", tex, Color(1, 0, 0), 1.0, 0, 50.0), Vector3(x, y, 0))
			_play(_define("ALPHA", "TexMany%d.tga" % (i + 1), Color(0, 1, 0), 1.0, 0, 50.0), Vector3(x, y, 0))
			cell += 1
			i += 2
		else:
			_play(_define("ALPHA", tex, Color(0, 0, 1), 1.0, 0, 5.0), Vector3(20000, 0, 0))
			i += 1
	await _settle()
	var stats: Dictionary = _fx.get_stats()
	_check(stats.batches >= 290, "about 300 batches reach the device layer (%d)" % stats.batches)
	var wrong := []
	for n in cells.size():
		var c := _pixel(cells[n].x, cells[n].y)
		if not _near(c, 0.0, 1.0, 0.0, 0.05):
			wrong.append("pair %d at rank %d: %s" % [n, pair_first[n], _fmt(c)])
	_check(wrong.is_empty(), "the later system of every pair is on top, also above rank 120 %s" % [wrong])


## Lane RENDER-3 (S-831): retail composites in gamma space. White ALPHA at alpha 0.25 over black displays 0.25 (D3D9 SRCALPHA / INVSRCALPHA on the
## displayed values; the vertex alpha is the byte 64 -> 0.251, or 63 -> 0.247); a linear-space blend displays 0.5333. The construction dust
## (BuildingContructDust: white, Shader ALPHA, Alpha2 0.15..0.35) is drawn this way.
func _case_gamma_alpha() -> void:
	_fx.clear()
	_play(_define("ALPHA", "TexG.tga", Color(1, 1, 1), 0.25), Vector3.ZERO)
	await _settle()
	var c := _pixel(0, 0)
	_check(_near(c, 0.25, 0.25, 0.25, 1.0 / 255.0), "white ALPHA at 0.25 over black displays retail's gamma-space 0.25 (+-1/255; linear blending gives 0.5333): %s" % _fmt(c))
	var unverified: PackedStringArray = _fx.get_unverified()
	_check("\n".join(unverified).contains("S-831: the transparent pass blends in retail's gamma space"), "the S-831 remainder line is reported")


## Review r2 (S-831 remainder): D3D9's frame buffer saturates every blend result to [0, 1]; the RGBA16F buffer does not, so additive overbright survives a
## later alpha or multiply draw. Pinned at the CURRENT output (+-2/255); retail's prediction is stated beside it. Submission order as in _case_aba.
func _case_saturation() -> void:
	_fx.clear()
	_play(_define("ADDITIVE", "TexS1.tga", Color(1, 0, 0), 1.0), Vector3.ZERO)
	_play(_define("ADDITIVE", "TexS2.tga", Color(1, 0, 0), 1.0), Vector3.ZERO)
	_play(_define("ALPHA", "TexS3.tga", Color(0, 0, 0), 0.5), Vector3.ZERO)
	await _settle()
	var c := _pixel(0, 0)
	_check(_near(c, 1.0, 0.0, 0.0, 2.0 / 255.0), "S-831 pin: additive red + additive red + half black displays %s (now (1, 0, 0); retail saturates the red to 1 first: about (0.5, 0, 0))" % _fmt(c))
	_fx.clear()
	_play(_define("ALPHA", "TexS4.tga", Color(1, 1, 1), 1.0), Vector3.ZERO)
	_play(_define("ADDITIVE", "TexS5.tga", Color(1, 0, 0), 1.0), Vector3.ZERO)
	_play(_define("MULTIPLY", "TexS6.tga", Color(0.5, 0.5, 0.5), 1.0), Vector3.ZERO)
	await _settle()
	c = _pixel(0, 0)
	_check(_near(c, 1.0, 0.502, 0.502, 2.0 / 255.0), "S-831 pin: white + additive red + multiply grey 0.5 displays %s (now (1, 0.502, 0.502); retail: about (0.5, 0.5, 0.5))" % _fmt(c))
	var unverified: PackedStringArray = _fx.get_unverified()
	_check(unverified.has("S-831: transparent blending uses an HDR buffer without retail's per-draw [0,1] saturation; additive overbright can survive later alpha or multiply draws"),
		"the per-draw saturation remainder is reported (exact S-831 line)")


## Lane FX-3 (QA-1 U7): the construction dust's texture (BuildingContructDust: ParticleName EXsnowcloud02.tga -> art/compiledtextures/ex/exsnowcloud02.dds,
## DXT5 whose alpha falls to 0 at the border) draws a soft puff, not a square: one white ALPHA billboard of edge 300 at alpha 1 shows the black background in
## the quad's corners and the texture's opaque middle in its centre. A hard-edged square is the 1x1 white default texture (an unresolved name), as _define's
## other cases draw it.
func _case_dust_soft_edges() -> void:
	_fx.clear()
	_play(_define("ALPHA", "EXsnowcloud02.tga", Color(1, 1, 1), 1.0, 0, 300.0), Vector3.ZERO)
	await _settle()
	var missing: Array = _fx.get_stats().get("missing_textures", [])
	_check(not missing.has("EXsnowcloud02.tga"), "the dust texture EXsnowcloud02.tga resolves (not among the %d missing test names)" % missing.size())
	var centre := _pixel(0, 0)
	_check(centre.r > 0.6, "the puff's centre is the texture's opaque middle: %s" % _fmt(centre))
	for corner in [Vector2(140, 140), Vector2(-140, 140), Vector2(140, -140), Vector2(-140, -140)]:
		var c := _pixel(corner.x, corner.y)
		_check(_near(c, 0, 0, 0, 2.0 / 255.0), "the billboard's corner (%d, %d) shows the background, no square edge: %s" % [corner.x, corner.y, _fmt(c)])
	var edge := _pixel(145, 0)
	_check(edge.r < 0.1, "the middle of the billboard's right edge is nearly transparent: %s" % _fmt(edge))
	var unverified: PackedStringArray = _fx.get_unverified()
	_check("\n".join(unverified).contains("S-1440: the composite of DefaultDraw sprites"), "the S-1440 line (the composite is not compared with a retail capture) is reported")


## Lane FX-3 round 2 (owner decision 2026-10-07): soft particles. A white ALPHA billboard (1x1 white default texture, alpha 1, edge 300) is crossed by an
## opaque black plane tilted about the vertical axis: Godot z = 0.2 * x, so at screen x < 0 the plane lies 0.2 |x| world units behind the billboard and at
## x > 0 in front of it. Retail (soft off) shows the hard cut: full white right up to the crossing. Soft mode fades by the depth gap over the distance
## 0.25 * size clamped to [2, 16] = 16: gap 20 (x = -100) stays white, gap 8 (x = -40) displays 0.5, gap 2 (x = -10) 0.125; in front (x = 20) the plane hides it.
func _case_soft_ground() -> void:
	var plane := MeshInstance3D.new()
	var quad := QuadMesh.new()
	quad.size = Vector2(400, 400)
	plane.mesh = quad
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.albedo_color = Color(0, 0, 0)
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	plane.material_override = mat
	plane.rotation = Vector3(0, -atan(0.2), 0)
	root.add_child(plane)
	for soft in [false, true]:
		_fx.soft_particles = soft
		_fx.clear()
		_play(_define("ALPHA", "TexSoft.tga", Color(1, 1, 1), 1.0, 0, 300.0), Vector3.ZERO)
		await _settle()
		var far := _pixel(-100, 0)
		var mid := _pixel(-40, 0)
		var near := _pixel(-10, 0)
		var front := _pixel(20, 0)
		_check(_near(front, 0, 0, 0, 2.0 / 255.0), "soft %s: where the plane is in front the particle is hidden: %s" % [soft, _fmt(front)])
		_check(_near(far, 1, 1, 1, 2.0 / 255.0), "soft %s: 20 units in front of the plane the particle is unfaded: %s" % [soft, _fmt(far)])
		if soft:
			_check(_near(mid, 0.5, 0.5, 0.5, 0.04), "soft: 8 units in front of the plane the alpha is halved (displays 0.5): %s" % _fmt(mid))
			_check(_near(near, 0.125, 0.125, 0.125, 0.04), "soft: 2 units in front of the plane the alpha is 1/8: %s" % _fmt(near))
			_check(_fx.get_stats().soft_particles == true, "the stats report soft particles on")
		else:
			_check(_near(mid, 1, 1, 1, 2.0 / 255.0) and _near(near, 1, 1, 1, 2.0 / 255.0), "retail: the cut is hard, full white up to the plane: %s %s" % [_fmt(mid), _fmt(near)])
	# ADDITIVE (ONE / ONE) fades its colour the same way
	_fx.soft_particles = true
	_fx.clear()
	_play(_define("ADDITIVE", "TexSoftAdd.tga", Color(1, 1, 1), 1.0, 0, 300.0), Vector3.ZERO)
	await _settle()
	var add_mid := _pixel(-40, 0)
	_check(_near(add_mid, 0.5, 0.5, 0.5, 0.04), "soft: an additive particle 8 units in front of the plane adds half its colour: %s" % _fmt(add_mid))
	plane.queue_free()
	_fx.soft_particles = false
