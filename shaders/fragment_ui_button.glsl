#version 330 core

out vec4 FragColor;
in vec3 fragPos;

uniform vec3 color;

uniform float transparency = 1;

uniform vec3 model_position;
uniform mat4 view_position;

uniform float border_size = 2.f;
uniform float width, height;

void main() {
	vec2 real_pos = fragPos.xy - model_position.xy;

	vec3 final_color = mix(color, vec3(0.1, 0, 0.5), 1.0 - real_pos.y / height);

	if (real_pos.y < border_size || real_pos.x < border_size ||
		real_pos.y > height - border_size || real_pos.x > width - border_size)
	{
		final_color = vec3(0, 0, 0);
	}

	FragColor = vec4(final_color, transparency);
}
