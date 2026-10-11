## Lane UI-5: the "Parchment & Steel" textures, made by shaders at run time: the misty backdrop with its left-to-right darkening, the
## left rail's dark steel and the parchment of the notes cards. No image files: the public tree and the launcher's pack stay text (the
## release audit, tools/release/public_binaries.txt: the only binaries are the OFL fonts). They are still pictures, drawn once (the
## launcher runs in low-processor mode: nothing animates, so nothing redraws while it sits idle).
##   var r := Textures.surface("mist")   # a ColorRect that fills its parent and keeps the shader's pixel size up to date
extends RefCounted

const NOISE := """
float hash(vec2 p) {
	p = fract(p * vec2(123.34, 456.21));
	p += dot(p, p + 45.32);
	return fract(p.x * p.y);
}
float noise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
	return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x), mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);
}
float fbm(vec2 p) {
	float v = 0.0;
	float a = 0.5;
	mat2 r = mat2(vec2(0.8, 0.6), vec2(-0.6, 0.8));
	for (int i = 0; i < 6; i++) {
		v += a * noise(p);
		p = r * p * 2.02 + vec2(1.7, 9.2);
		a *= 0.5;
	}
	return v;
}
float fbm4(vec2 p) {
	float v = 0.0;
	float a = 0.5;
	mat2 r = mat2(vec2(0.8, 0.6), vec2(-0.6, 0.8));
	for (int i = 0; i < 4; i++) {
		v += a * noise(p);
		p = r * p * 2.02 + vec2(1.7, 9.2);
		a *= 0.48;
	}
	return v / 0.93;
}
"""

## the backdrop: soft billows of dark smoke, denser low and at the sides, lit warm from below the middle (the prototype's mist), under
## the --shade gradient rgba(8,8,10,.9) -> .35 at 70 % -> .6; a little noise against banding
const MIST := """
shader_type canvas_item;
uniform vec2 px_size = vec2(1280.0, 720.0);
//NOISE
void fragment() {
	vec2 uv = UV;
	float asp = px_size.x / max(px_size.y, 1.0);
	vec2 p = vec2(uv.x * asp, uv.y) * 1.5;
	vec2 q = vec2(fbm4(p * 0.7), fbm4(p * 0.7 + vec2(5.2, 1.3)));
	float n = fbm4(p + 0.9 * q);
	// Sol r2: finer detail (six octaves, warped along the billows) and a little more contrast, still soft at the largest scale
	float detail = fbm(p * 3.2 + 1.6 * q + vec2(3.1, 7.7));
	float wisp = fbm(vec2(p.x * 5.5, p.y * 4.0) + 2.4 * q + vec2(8.2, 1.9));
	float smoke = smoothstep(0.3, 0.72, n * 0.6 + detail * 0.3 + wisp * 0.1);
	smoke = smoke * (0.82 + 0.36 * smoothstep(0.35, 0.75, detail));
	float side = 0.5 + 0.5 * smoothstep(0.0, 0.7, uv.y) + 0.35 * smoothstep(0.5, 1.0, abs(uv.x - 0.5) * 2.0);
	smoke *= side;
	vec2 d = (uv - vec2(0.5, 0.68)) * vec2(asp * 0.55, 1.3);
	float glow = exp(-dot(d, d) * 3.6);
	vec3 col = vec3(0.035, 0.035, 0.042);
	col += vec3(0.3, 0.29, 0.31) * smoke * 0.5;
	col += vec3(0.92, 0.52, 0.22) * glow * (0.1 + 0.75 * smoke);
	col += vec3(1.0, 0.82, 0.55) * pow(glow, 10.0) * 0.3 * (0.5 + smoke);
	float a = uv.x < 0.7 ? mix(0.9, 0.35, uv.x / 0.7) : mix(0.35, 0.6, (uv.x - 0.7) / 0.3);
	col = mix(col, vec3(8.0, 8.0, 10.0) / 255.0, a);
	col += (hash(UV * px_size) - 0.5) / 255.0;
	COLOR = vec4(col, 1.0);
}
"""

## the rail: brushed dark steel (fine horizontal grain, a few longer scratches, a soft light from the upper left) under --navbg's
## rgba(10,10,12,.82) -> .92 (a little lighter here, so the grain reads as in the prototype)
const STEEL := """
shader_type canvas_item;
uniform vec2 px_size = vec2(200.0, 720.0);
//NOISE
void fragment() {
	vec2 pp = UV * px_size;
	float brush = 0.5 * noise(vec2(pp.x / 240.0, pp.y * 0.55)) + 0.3 * noise(vec2(pp.x / 95.0 + 13.0, pp.y * 1.3))
		+ 0.2 * noise(vec2(pp.x / 38.0 + 7.0, pp.y * 2.7));
	float scratch = smoothstep(0.86, 1.0, noise(vec2(pp.x / 150.0 + 3.0, pp.y * 0.4 + 11.0)))
		* smoothstep(0.35, 0.8, noise(pp / 70.0 + vec2(4.0, 2.0)));
	float broad = fbm4(pp / 300.0);
	float light = clamp(1.15 - 0.9 * length((UV - vec2(0.2, 0.0)) * vec2(1.0, 0.75)), 0.35, 1.0);
	float v = (0.15 + 0.1 * broad) * (0.65 + 0.7 * brush) * light + 0.1 * scratch * light;
	vec3 col = vec3(v * 0.97, v, v * 1.06);
	col = mix(col, vec3(10.0, 10.0, 12.0) / 255.0, mix(0.5, 0.82, UV.y));
	COLOR = vec4(col, 1.0);
}
"""

## a parchment sheet: even warm paper with a fine grain and fibres, faint stains, an irregular darker margin and a thin scorched rim,
## sparse specks; corners rounded (6 px, .parch)
const PARCHMENT := """
shader_type canvas_item;
uniform vec2 px_size = vec2(320.0, 360.0);
//NOISE
void fragment() {
	vec2 pp = UV * px_size;
	float big = fbm4(pp / 150.0);
	float mid = fbm4(pp / 38.0 + vec2(3.0, 5.0));
	float grain = 0.5 * noise(pp / 1.5) + 0.5 * noise(pp / 3.2 + vec2(9.0, 2.0));
	float fibre = noise(vec2(pp.x / 11.0, pp.y / 1.3) + vec2(4.0, 0.0));
	vec3 col = mix(vec3(0.93, 0.865, 0.715), vec3(0.84, 0.71, 0.5), smoothstep(0.45, 0.85, big) * 0.55 + smoothstep(0.6, 0.9, mid) * 0.18);
	col *= 0.975 + 0.04 * grain + 0.02 * fibre;
	vec2 e = min(pp, px_size - pp);
	float edge = min(e.x, e.y) + (fbm4(pp / 22.0 + vec2(7.0, 1.0)) - 0.5) * 16.0;
	col = mix(col, vec3(0.72, 0.54, 0.31), (1.0 - smoothstep(0.0, 34.0, edge)) * 0.5);
	col = mix(col, vec3(0.47, 0.31, 0.15), (1.0 - smoothstep(0.0, 5.0, edge)) * 0.55);
	col = mix(col, vec3(0.52, 0.36, 0.18), smoothstep(0.975, 0.997, noise(pp / 2.4)) * 0.45);
	vec2 h = px_size * 0.5;
	vec2 q = abs(pp - h) - (h - vec2(6.0));
	float dist = length(max(q, vec2(0.0))) - 6.0;
	COLOR = vec4(col, clamp(0.5 - dist, 0.0, 1.0));
}
"""

static var _shaders := {}


static func material(kind: String) -> ShaderMaterial:
	if not _shaders.has(kind):
		var s := Shader.new()
		s.code = String({"mist": MIST, "steel": STEEL, "parchment": PARCHMENT}[kind]).replace("//NOISE", NOISE)
		_shaders[kind] = s
	var m := ShaderMaterial.new()
	m.shader = _shaders[kind]
	return m


## a ColorRect drawn by the `kind` shader ("mist", "steel", "parchment"), filling its parent; it ignores the mouse
static func surface(kind: String) -> ColorRect:
	var r := ColorRect.new()
	r.material = material(kind)
	r.mouse_filter = Control.MOUSE_FILTER_IGNORE
	r.set_anchors_preset(Control.PRESET_FULL_RECT)
	r.resized.connect(func() -> void: (r.material as ShaderMaterial).set_shader_parameter("px_size", r.size))
	return r
