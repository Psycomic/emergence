#version 330 core

out vec4 FragColor;
in vec3 fragPos;

uniform vec3 color = vec3(0, 1, 0);
uniform float transparency = 1;

uniform vec3 position;

uniform float width;
uniform float height;

void main(void) {
	vec3 relative_position = fragPos - position;

	vec3 distance_vector = fragPos - (position + vec3(width / 2, height / 2, 0));

	float distance_to_middle = length(vec3(distance_vector.x / width, distance_vector.y / height, distance_vector.z));
	float rounding_effect = 1 - clamp(distance_to_middle, 0, 1) * 1.5;

	rounding_effect = clamp(rounding_effect * 20, 0, 0.6) / 0.6;

	FragColor = vec4(color, transparency * rounding_effect * clamp((sin(relative_position.y * 700) + 1.5) / 2, 0, 1));
}