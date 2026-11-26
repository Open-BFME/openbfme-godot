// OpenBFME. GPL-3.0. Lane BLOBSHADOW-1: one SHADOW_DECAL blob in the top-down accumulation viewport (blob_shadows.gdshaderinc). Retail's shadow
// list draws with _PresetMultiplicativeShader: frame buffer x texture x the vertex colour 0xFFFFFFFF (BFME2 flushDecals 0x109E11). A canvas item
// in a 2D viewport keeps the texel values as they are (no colour-space conversion), so the multiply happens on retail's gamma values.
shader_type canvas_item;
render_mode blend_mul, unshaded;

void fragment() {
	// clamped as retail's texture (address mode clamp, RW 0x532326): a texel outside the blob's quad is never sampled
	COLOR = vec4(textureLod(TEXTURE, clamp(UV, vec2(0.0), vec2(1.0)), 0.0).rgb, 1.0);
}
