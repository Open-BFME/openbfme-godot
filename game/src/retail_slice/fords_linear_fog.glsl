#[compute]
#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(rgba16f, set = 0, binding = 0) uniform image2D color_image;
layout(set = 0, binding = 1) uniform sampler2D depth_texture;

// Three vec4-aligned rows. These values are supplied by FordsLinearFog after
// it validates the exact source parameters and an explicit uniform map scale.
layout(push_constant, std430) uniform Params {
	mat4 inv_projection;
	vec2 raster_size;
	float fog_start;
	float fog_end;
	vec4 fog_color;
} params;

void main() {
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = ivec2(params.raster_size);
	if (pixel.x >= size.x || pixel.y >= size.y) {
		return;
	}

	vec2 screen_uv = (vec2(pixel) + vec2(0.5)) / params.raster_size;
	float raw_depth = texture(depth_texture, screen_uv).r;
	// Godot Forward+ uses reverse-Z: zero is the clear depth, not geometry.
	// Preserve the environment backdrop instead of fogging clear pixels.
	if (raw_depth <= 0.0) {
		return;
	}
	vec2 ndc_xy = screen_uv * 2.0 - 1.0;
	vec4 view_position = params.inv_projection * vec4(ndc_xy, raw_depth, 1.0);

	// Guard malformed finite-depth reconstruction independently of clear depth.
	if (abs(view_position.w) <= 1e-8) {
		return;
	}

	float camera_depth = -(view_position.z / view_position.w);
	float fog_factor = clamp(
		(camera_depth - params.fog_start) / (params.fog_end - params.fog_start),
		0.0,
		1.0
	);
	vec4 color = imageLoad(color_image, pixel);
	color.rgb = mix(color.rgb, params.fog_color.rgb, fog_factor);
	imageStore(color_image, pixel, color);
}
