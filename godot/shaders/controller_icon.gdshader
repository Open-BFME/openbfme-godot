shader_type canvas_item;
	uniform vec2 rect_size = vec2(1.0);
	varying vec2 local_uv;
	varying vec4 tint;
	void vertex() { local_uv = VERTEX / rect_size; tint = COLOR; }
	void fragment() {
		float edge = length(local_uv - vec2(0.5));
		float alpha = 1.0 - smoothstep(0.5 - fwidth(edge), 0.5, edge);
		COLOR = vec4(texture(TEXTURE, UV).rgb * tint.rgb, alpha * tint.a);
	}
