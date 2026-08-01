shader_type spatial;
render_mode unshaded, blend_add, depth_draw_never, cull_disabled, shadows_disabled;

// RETAIL'S `MouseoverEffectFlareup`, WITH A FALLOFF INSTEAD OF A FLAT WASH.
//
// THE DEFECT THIS REPLACES. The mouseover flare was a StandardMaterial3D with a
// constant albedo over retail's whole `LMR_Fill` footprint: uniform additive
// light right up to the polygon boundary, which ends on a hard edge and draws the
// province's raw silhouette as a sheet of colour. It is the primary interaction of
// this screen - the pointer is in a province almost all the time - and a flat
// sheet is exactly what it should not look like.
//
// WHY A SHADER AND NOT MORE MATERIAL SETTINGS. Retail's fill mesh carries
// POSITIONS AND NOTHING ELSE: no UVs, no vertex colours, so nothing in a
// StandardMaterial3D has a coordinate to fade against.
// `wotr_region_geometry.gd:fill_falloff_mesh` bakes one - `COLOR.r` is 0 on the
// province's own outline (measured against retail's own border ribbon for that
// region) and 1 at its deepest interior point - and this reads it.
//
// THE SHAPE OF THE FALL. Light POOLS in the province: it reaches full strength
// `edge` of the way in from the rim and is exactly zero ON the rim, so the flare
// never has an edge of its own. The ownership band and its bloomed shoulder
// already carry the outline, and they burn harder on the hovered province
// (`HOVER_BAND_GAIN`), so the two together read as a lit province rather than as
// a coloured polygon: a hot rim from the band, a soft pool inside it from this.
//
// `core` lifts the deep interior a little further so a large province does not go
// flat in the middle the way a pure smoothstep does.
uniform vec4 flare_color : source_color = vec4(1.0, 1.0, 1.0, 0.0);
uniform float edge : hint_range(0.05, 1.0) = 0.45;
uniform float core : hint_range(0.0, 1.0) = 0.30;

varying float rim_depth;

void vertex() {
	// A region whose falloff could NOT be baked keeps retail's plain fill mesh,
	// and Godot hands an absent COLOR array through as white - so `rim_depth` is
	// 1 everywhere and this degrades exactly to the flat wash it replaces. That is
	// the honest fallback: no falloff is invented for a region that has no rim
	// geometry to measure one against, and the view names those regions.
	rim_depth = COLOR.r;
}

void fragment() {
	float pool = smoothstep(0.0, edge, rim_depth);
	// The deep interior, added on top of the pool rather than multiplied into it,
	// so the rim still reaches exactly zero.
	pool = clamp(pool + core * smoothstep(edge, 1.0, rim_depth), 0.0, 1.0);
	ALBEDO = flare_color.rgb;
	ALPHA = flare_color.a * pool;
}
