#version 330 core

out vec4 FragColor;
in vec3 fragPos;

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

	return (seed.x - 0.5) * 2;
}

void main(void) {
	vec3 relative_position = fragPos - position;

	vec3 distance_vector = fragPos - (position + vec3(width / 2, height / 2, 0));

	float distance_to_middle = length(vec3(distance_vector.x / width, distance_vector.y / height, distance_vector.z));
	float rounding_effect = 1 - clamp(distance_to_middle, 0, 1) * 1.5;

	rounding_effect = clamp(rounding_effect * 10, 0, 0.8);

	FragColor = vec4(color,// + vec3(rand(), rand(), rand()) * 0.1, 
		transparency * clamp((sin(relative_position.y * 700) + 1.8) / 3, 0, 1) * rounding_effect * 1.3f);
}