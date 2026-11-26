// OpenBFME. GPL-3.0. Lane BLOBSHADOW-1: retail's SHADOW_DECAL blob shadows (W3DProjectedShadowManager's shadow list, drawn at ShadowLOD Low and
// Medium only: RW 0x50BF7A). Retail projects each blob straight down onto the terrain and multiplies the frame buffer with the blob texture
// (BFME2 flushDecals 0x109E11, _PresetMultiplicativeShader; RTS3DScene::Flush draws the decal shadows after the terrain and the water, RW 0x471007).
// The port draws every visible blob into one top-down accumulation texture (multiplicative quads, cleared to white: overlapping blobs darken as
// retail's repeated multiplies do) and the receivers multiply their gamma colour with it once.

global uniform sampler2D blob_shadow_tex; // RGBA8, white = no blob
global uniform vec4 blob_shadow_rect;     // the texture's ground rectangle in SAGE x / y: min x, min y, width, height; width <= 0: no blobs

// the blob multiplier at a SAGE ground point (x, y), 1 outside the covered rectangle
vec3 blob_shadow_at(vec2 sage_xy) {
	if (blob_shadow_rect.z <= 0.0) {
		return vec3(1.0);
	}
	vec2 uv = (sage_xy - blob_shadow_rect.xy) / blob_shadow_rect.zw;
	if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) {
		return vec3(1.0);
	}
	return textureLod(blob_shadow_tex, uv, 0.0).rgb;
}
