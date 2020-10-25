#version 330 core

out vec4 FragColor;
in vec3 fragPos;

uniform vec3 color = vec3(0, 1, 0);
uniform float transparency = 1;

uniform vec3 anchor_position;

uniform float max_width = -1, max_height = -1;

void main() {
	float is_visible = 1;

	if (max_width > 0 && max_height > 0) {
		vec2 real_pos = fragPos.xy - anchor_position.xy;

		if (real_pos.x > max_width || -real_pos.y > max_height)
			is_visible = 0;
	}

	FragColor = vec4(color, transparency * is_visible);
}