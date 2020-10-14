#version 330 core

out vec4 FragColor;
in vec3 FragPos;

uniform vec3 color = vec3(0, 1, 0);
uniform float transparency = 1;

uniform vec3 position;

uniform float width;
uniform float height;

uniform float time;

vec2 seed = vec2(time + gl_FragCoord.x, time + gl_FragCoord.y);

float remapped_sin(float x) {
	return (sin(x) + 1) / 2;
}

float rand() {
	seed.x = fract(sin(dot(seed.xy, vec2(12.9898, 78.233))) * 43758.5453);

	return seed.x;
}

float superior_to(float x, float min) {
	return x < min ? 0.f : 1.f;
}

void main(void) {
	vec3 relative_position = FragPos - position;

	vec3 distance_vector = FragPos - (position + vec3(width / 2, height / 2, 0));

	float distance_to_middle = length(vec3(distance_vector.x / width, distance_vector.y / height, distance_vector.z));
	float rounding_effect = 1 - clamp(distance_to_middle, 0, 1) * 1.7;

	FragColor = vec4(color + vec3(rand(), rand(), rand()) * 0.1, 
		transparency * clamp((sin(relative_position.y * 700 + time * 70) + 1.5) / 3 * rounding_effect * 1.3f, 0, 1));
}