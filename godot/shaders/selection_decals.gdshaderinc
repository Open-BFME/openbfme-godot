// OpenBFME. GPL-3.0. Lane SELPERF-1: the selected units' merge decals (SHADOW_MERGE_DECAL, e.g. decal_G_level4 with its cut-out decal_good_CO), drawn
// by the terrain per pixel instead of a CPU-composed texture (that cost seconds a frame for a marching army of 150 hordes); lane SELWALL-1: and by the wall
// top / bridge models (W3DShaderKey::SelectionDecals, sel_merge_at).
//
// TARGET FACTS (engine GameClient/SelectionDecals.h): RotWK draws the list with the projected shadow manager's stencil passes (RW 0x50D46A, the
//   BFME2 donor's RVA 0x10DA71; the stencil states in flushDecals RW 0x50977A):
//   pass 0 every decal's Texture2 into the stencil (ALWAYS / REPLACE, alpha-tested, no colour), pass 1 every decal's Texture where the stencil is not
//   set (opaque, alpha-tested; the last disc drawn wins); with UseSimpleMergeDecals one blended pass, each pixel once (the first disc that passes the
//   alpha test at OpacityOfSimpleMergeDecals).
// INFERENCE (S-2522, as the CPU reference ComposeMergeDecals): the alpha test compares round(texel alpha * opacity) with 0x60; the texture is filtered
//   bilinearly, clamped to its own rectangle; the colour is texel * the decal's colour, in the space the terrain's output decodes (retail_output).
// The data (BuildMergeDecalGrid, the layout in SelectionDecals.h) is set by the HUD each frame (GodotInGameHud.cpp drawSelectionDecals);
// EvaluateMergeDecalGrid is this file's arithmetic in C++ (engine/tests/test_selperf1_merge_grid.cpp compares it with ComposeMergeDecals).

global uniform sampler2D sel_data;  // RGBA32F, 1024 texels a row
global uniform sampler2D sel_atlas; // RGBA8: every merge decal texture of the selection (lane HERORING-1: and every alpha decal's)
global uniform vec4 sel_grid;       // min x, min y, cell size, cells x
global uniform vec4 sel_grid2;      // cells y, cell offset, index offset, mode (0 nothing, 1 the two stencil passes, 2 the simple pass; + 4 with alpha decals)
global uniform float sel_alpha_ref; // the alpha test's reference (0..255)
// lane SELWALL-1: the decal's height range on a wall surface (engine GameClient/SelectionDecals.h kSelectionDecalMaxHeight; RotWK's descriptor + 0x20)
const float SEL_MAX_HEIGHT = 20.0;
// lane SELWALL-1: a wall model's surface takes the decals only where it faces up at least this much (the world normal's up component: about 45 degrees);
// INFERENCE (S-2522): RotWK projects onto the drawable's walkway sub-meshes (RampMesh1 / RampMesh2 / WallBoundsMesh / RaisedWallMesh, RW 0x6729A3 /
// 0x6729E9 / 0x672943 / 0x672973), which the port stands in with the visible faces that face up, not the side faces
const float SEL_WALL_MIN_UP = 0.7;

vec4 sel_texel(int n) {
	return texelFetch(sel_data, ivec2(n % 1024, n / 1024), 0);
}

// the atlas rect's texel at uv (v = 0 the top row), bilinear, clamped to the rect
vec4 sel_bilinear(vec4 rect, vec2 uv) {
	vec2 p = uv * rect.zw - 0.5;
	vec2 f0 = floor(p);
	vec2 fr = p - f0;
	ivec2 lo = ivec2(rect.xy);
	ivec2 hi = lo + ivec2(rect.zw) - ivec2(1);
	ivec2 a = clamp(lo + ivec2(f0), lo, hi);
	ivec2 b = clamp(lo + ivec2(f0) + ivec2(1), lo, hi);
	vec4 top = mix(texelFetch(sel_atlas, ivec2(a.x, a.y), 0), texelFetch(sel_atlas, ivec2(b.x, a.y), 0), fr.x);
	vec4 bottom = mix(texelFetch(sel_atlas, ivec2(a.x, b.y), 0), texelFetch(sel_atlas, ivec2(b.x, b.y), 0), fr.x);
	return mix(top, bottom, fr.y);
}

// lane SELWALL-1: the merge decals at the SAGE world point `w` of a surface: the terrain (`wall` false) leaves the raised decals out (their index entry
// is -(i + 1)), a wall top or bridge model (`wall` true, W3DShaderKey::SelectionDecals) takes a decal only within 20 (kSelectionDecalMaxHeight) of the
// point's height. Their colour (retail space, 0..1) and how much of the surface it covers (0 nothing, 1 opaque)
vec4 sel_merge_at(vec3 w, bool wall) {
	int mode = int(sel_grid2.w) & 3; // lane HERORING-1: + 4 when there are alpha decals (sel_alpha_at)
	if (mode == 0) {
		return vec4(0.0);
	}
	vec2 g = (w.xy - sel_grid.xy) / sel_grid.z;
	if (g.x < 0.0 || g.y < 0.0 || g.x >= sel_grid.w || g.y >= sel_grid2.x) {
		return vec4(0.0);
	}
	vec4 cell = sel_texel(int(sel_grid2.y) + int(g.y) * int(sel_grid.w) + int(g.x));
	int first = int(cell.x);
	int count = int(cell.y);
	bool covered = false;
	bool drawn = false;
	vec4 best = vec4(0.0);
	// the two passes walk the list from its end: every cut-out is tested (any one covers the pixel: nothing is drawn), a disc only until one passes
	// (pass 1's last disc drawn wins); the simple pass walks it from the start (its first disc that passes wins)
	for (int i = 0; i < count; i++) {
		int k = mode == 1 ? count - 1 - i : i;
		vec4 e = sel_texel(int(sel_grid2.z) + first + k); // x, y, size, decal
		float h = e.z * 0.5;
		vec2 uv = vec2((w.x - (e.x - h)) / e.z, ((e.y + h) - w.y) / e.z);
		if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
			continue;
		}
		if (!wall && e.w < 0.0) {
			continue;
		}
		int di = int(e.w < 0.0 ? -e.w - 1.0 : e.w) * 4;
		if (wall && !(abs(w.z - sel_texel(di + 1).w) <= SEL_MAX_HEIGHT)) {
			continue;
		}
		vec4 d = sel_texel(di);
		if (mode == 1 && floor(sel_bilinear(sel_texel(di + 3), uv).a * d.w + 0.5) >= sel_alpha_ref) {
			// pass 0: a cut-out in the stencil here, so pass 1 draws nothing here whatever the other decals are
			covered = true;
			break;
		}
		if (drawn) {
			continue; // the last disc that passes is known: only the remaining cut-outs matter
		}
		vec4 s = sel_bilinear(sel_texel(di + 2), uv);
		float a = floor(s.a * d.w + 0.5);
		if (a >= sel_alpha_ref) {
			best = vec4(s.rgb * sel_texel(di + 1).rgb / 255.0, mode == 2 ? a / 255.0 : 1.0);
			drawn = true;
			if (mode == 2) {
				break;
			}
		}
	}
	return (drawn && !covered) ? best : vec4(0.0);
}

// lane PLACEDECAL-1: a vertex mask's alpha (0 .. 255) at the SAGE point `w`: bilinear between the four map vertices around it (10 apart), clamped to the
// mask (header texel: vx0, vy0, w, h; then a texel a vertex, the alpha in x)
float sel_mask_at(int header, vec2 w) {
	vec4 h = sel_texel(header);
	int mw = int(h.z);
	int mh = int(h.w);
	float fx = clamp(w.x / 10.0 - h.x, 0.0, float(mw - 1));
	float fy = clamp(w.y / 10.0 - h.y, 0.0, float(mh - 1));
	int x0 = min(int(fx), mw - 1);
	int y0 = min(int(fy), mh - 1);
	int x1 = min(x0 + 1, mw - 1);
	int y1 = min(y0 + 1, mh - 1);
	float tx = fx - float(x0);
	float ty = fy - float(y0);
	float a00 = sel_texel(header + 1 + y0 * mw + x0).x;
	float a10 = sel_texel(header + 1 + y0 * mw + x1).x;
	float a01 = sel_texel(header + 1 + y1 * mw + x0).x;
	float a11 = sel_texel(header + 1 + y1 * mw + x1).x;
	return (a00 * (1.0 - tx) + a10 * tx) * (1.0 - ty) + (a01 * (1.0 - tx) + a11 * tx) * ty;
}

// lane HERORING-1: the alpha decals (a hero's ring, SHADOW_ALPHA_DECAL) at the SAGE point `w` of a surface, blended in their list order as RotWK's
// flushDecals draws them before the merge list (RW 0x509A1F: ShaderClass 0x1198B3, SRC_ALPHA / ONE_MINUS_SRC_ALPHA, no alpha test; texture x diffuse,
// MODULATE RW 0x5373FC; the diffuse is the colour with the opacity as alpha, RW 0x73228B): over a surface colour C each one gives C (1 - a) + c a with
// a = texel alpha x opacity, c = texel x colour. Lane ECOCIRCLE-2: an additive decal (rec3.w -2 / -3, SHADOW_ADDITIVE_DECAL 0x40 / 0x800) gives C + c o
// instead (o the opacity, ShaderClass 0x515833 SC_ADDITIVE: ONE / ONE): the surface keeps its colour. Returned premultiplied: rgb the decals' share (retail space, 0..1), a how much of the surface is
// covered, so the surface becomes C (1 - result.a) + result.rgb. The same raised and wall rules as sel_merge_at
vec4 sel_alpha_at(vec3 w, bool wall) {
	if (int(sel_grid2.w) < 4) {
		return vec4(0.0);
	}
	vec2 g = (w.xy - sel_grid.xy) / sel_grid.z;
	if (g.x < 0.0 || g.y < 0.0 || g.x >= sel_grid.w || g.y >= sel_grid2.x) {
		return vec4(0.0);
	}
	vec4 cell = sel_texel(int(sel_grid2.y) + int(g.y) * int(sel_grid.w) + int(g.x));
	int first = int(cell.z);
	int count = int(cell.w);
	vec3 cd = vec3(0.0);
	float keep = 1.0;
	for (int i = 0; i < count; i++) {
		vec4 e = sel_texel(int(sel_grid2.z) + first + i); // x, y, size, decal
		vec2 d = w.xy - e.xy;
		if (dot(d, d) > 0.5 * e.z * e.z) {
			continue; // outside the turned square's circumcircle
		}
		if (!wall && e.w < 0.0) {
			continue;
		}
		int di = int(e.w < 0.0 ? -e.w - 1.0 : e.w) * 4;
		vec4 rec1 = sel_texel(di + 1); // r, g, b, z
		vec4 rec3 = sel_texel(di + 3); // the turn's cosine and sine, the y size, -1 terrain only / -2 terrain-only additive / -3 additive, wall too / the vertex mask's texel (lane PLACEDECAL-1)
		bool additive = rec3.w == -2.0 || rec3.w == -3.0;
		if (wall && ((rec3.w != 0.0 && rec3.w != -3.0) || !(abs(w.z - rec1.w) <= SEL_MAX_HEIGHT))) {
			continue;
		}
		vec4 rec0 = sel_texel(di); // x, y, size, opacity
		float co = rec3.x;
		float si = rec3.y;
		vec2 uv = vec2((d.x * co + d.y * si) / rec0.z + 0.5, (d.x * si - d.y * co) / rec3.z + 0.5);
		if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
			continue;
		}
		vec4 s = sel_bilinear(sel_texel(di + 2), uv);
		// an additive decal adds texel x colour x opacity, its texel alpha unused (RW 0x73228B: no diffuse alpha; SC_ADDITIVE ONE / ONE)
		float a = additive ? rec0.w / 255.0 : s.a * rec0.w / 255.0;
		if (rec3.w > 0.0) {
			a *= sel_mask_at(int(rec3.w), w.xy) / 255.0;
		}
		vec3 src = s.rgb * rec1.rgb / 255.0;
		if (additive) {
			cd += src * a; // ONE / ONE: the surface keeps its colour
		} else {
			cd = cd * (1.0 - a) + src * a;
			keep *= 1.0 - a;
		}
	}
	return vec4(cd, 1.0 - keep);
}

// the terrain's (the SAGE point x, y)
vec4 sel_merge(vec2 w) {
	return sel_merge_at(vec3(w, 0.0), false);
}
