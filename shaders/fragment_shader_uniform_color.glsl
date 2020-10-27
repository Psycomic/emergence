#version 330 core

out vec4 FragColor;
in vec3 fragPos;

uniform vec3 color = vec3(0, 1, 0);
uniform float transparency = 1;

uniform vec3 anchor_position;
uniform vec3 model_position;

uniform float max_width = -1, max_height = -1;
uniform float border_size = 0.f;
uniform float width, height;

void main() {
	float is_visible = 1;
	vec3 final_color = color;

	// Adding a border: UI element
	if (border_size > 0.f) {
		vec2 real_pos = fragPos.xy - model_position.xy;

		if (real_pos.y <= border_size || real_pos.x <= border_size ||
			real_pos.y >= height - border_size || real_pos.x >= width - border_size)
		{
			final_color = vec3(0, 0, 0);
			is_visible = 0.5;
		}
		else {
			is_visible = 0.7;
		}
	}

	// Limit to the size: UI element
	if (max_width > 0 && max_height > 0) {
		vec2 real_pos = fragPos.xy - anchor_position.xy;

		if (real_pos.x > max_width || -real_pos.y > max_height)
			is_visible = 0;
	}

	FragColor = vec4(final_color, transparency * is_visible);
}