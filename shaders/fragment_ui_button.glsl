#version 330 core

out vec4 FragColor;
in vec3 fragPos;

uniform vec3 color;

uniform float transparency = 1;

uniform vec2 model_position;
uniform mat4 view_position;

uniform float border_size;
uniform float width, height;

vec3 rgb_to_vec3(float r, float g, float b) {
	return vec3(r / 255.0, g / 255.0, b / 255.0);
}

void main() {
	vec2 real_pos = fragPos.xy - model_position.xy;

	vec3 final_color = mix(color, vec3(0.1, 0.1, 0.1), 1.0 - real_pos.y / height);

	if (real_pos.y < border_size || real_pos.x < border_size ||
		real_pos.y > height - border_size || real_pos.x > width - border_size)
	{
		final_color = rgb_to_vec3(6, 18, 71);
	}

	FragColor = vec4(final_color, transparency);
}
