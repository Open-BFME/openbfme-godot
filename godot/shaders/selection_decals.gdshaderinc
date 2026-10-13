// OpenBFME. GPL-3.0. Lane SELPERF-1: the selected units' merge decals (SHADOW_MERGE_DECAL, e.g. decal_G_level4 with its cut-out decal_good_CO), drawn
// by the terrain per pixel instead of a CPU-composed texture (that cost seconds a frame for a marching army of 150 hordes).
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
global uniform sampler2D sel_atlas; // RGBA8: every merge decal texture of the selection
global uniform vec4 sel_grid;       // min x, min y, cell size, cells x
global uniform vec4 sel_grid2;      // cells y, cell offset, index offset, mode (0 nothing, 1 the two stencil passes, 2 the simple pass)
global uniform float sel_alpha_ref; // the alpha test's reference (0..255)

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

// the merge decals at the SAGE world point `w`: their colour (retail space, 0..1) and how much of the terrain it covers (0 nothing, 1 opaque)
vec4 sel_merge(vec2 w) {
	int mode = int(sel_grid2.w);
	if (mode == 0) {
		return vec4(0.0);
	}
	vec2 g = (w - sel_grid.xy) / sel_grid.z;
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
		int di = int(e.w) * 4;
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
