#version 330 core

out vec4 FragColor;
in vec3 fragPos;

vec3 color = { 40 / 255.0, 40 / 255.0, 40 / 255.0 };

uniform float transparency = 1;

uniform vec3 anchor_position;
uniform vec3 model_position;

uniform mat4 view_position;

uniform float max_width, max_height;
uniform float border_size;
uniform float width, height;

vec3 inverse_color(vec3 color) {
	return vec3(1.0 - color.x, 1.0 - color.y, 1.0 - color.z);
}

vec3 rgb_to_vec3(float r, float g, float b) {
	return vec3(r / 255.0, g / 255.0, b / 255.0);
}

void main() {
	float is_visible = 1;

	vec2 real_pos = fragPos.xy - model_position.xy;

	vec3 final_color = mix(color, inverse_color(color), real_pos.y / height);

	if (real_pos.y < border_size || real_pos.x < border_size ||
		real_pos.y > height - border_size || real_pos.x > width - border_size) 
	{
		final_color = rgb_to_vec3(6, 18, 71);
	}

	real_pos = fragPos.xy - anchor_position.xy;

	if (real_pos.x > max_width || -real_pos.y > max_height)
		is_visible = 0;

	FragColor = vec4(final_color, transparency * is_visible);
}