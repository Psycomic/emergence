#version 330 core

out vec4 FragColor;
in vec3 fragPos;

vec3 color = { 40 / 255.0, 40 / 255.0, 40 / 255.0 };

uniform float transparency = 1;

uniform vec3 anchor_position;
uniform vec3 model_position;

uniform float max_width, max_height;
uniform float border_size;
uniform float width, height;

void main() {
	float is_visible = 1;
	vec3 final_color = color;

	vec2 real_pos = fragPos.xy - model_position.xy;

	if (real_pos.y <= border_size || real_pos.x <= border_size ||
		real_pos.y >= height - border_size || real_pos.x >= width - border_size)
			final_color = vec3(0xda / 255.0, 0xdb / 255.0, 0xdc / 255.0); // ##DADBDC

	real_pos = fragPos.xy - anchor_position.xy;

	if (real_pos.x > max_width || -real_pos.y > max_height)
		is_visible = 0;

	FragColor = vec4(final_color, transparency * is_visible);
}