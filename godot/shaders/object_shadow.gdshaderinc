// OpenBFME. GPL-3.0. Godot shadow-map coverage, with a SAGE-style gamma-space multiplier.
// DONOR: ZH W3DVolumetricShadowManager::renderStencilShadows uses DESTCOLOR / ZERO with the map's shadow RGB.
// This composite for RotWK shadow maps is an inference (S-3304), not a port of its stencil volumes.
global uniform vec4 w3d_shadow; // RGB multiplier; w: an active shadow light is present
varying vec3 v_shadow_color; // fragment -> light, in retail gamma space

vec3 shadow_to_linear(vec3 c) {
	return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

vec3 shadow_to_gamma(vec3 c) {
	c = max(c, vec3(0.0));
	return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

vec3 object_shadow_light(float coverage) {
	return shadow_to_linear(v_shadow_color * mix(w3d_shadow.rgb, vec3(1.0), coverage));
}
