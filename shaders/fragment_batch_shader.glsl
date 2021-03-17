#version 330 core

in float transparency;
in vec2 texture_coords;
in vec2 size;

out vec4 FragColor;

const float border_size = 2.f;
const vec3 border_color = vec3(0.9, 0.9, 0.9);

void main() {
	vec2 real_coords = texture_coords * size;

	if (real_coords.x <= border_size || real_coords.x >= size.x - border_size ||
		real_coords.y <= border_size || real_coords.y >= size.y - border_size)
		FragColor = vec4(border_color * transparency, 1);
	else
		FragColor = vec4(0.0, 0.0, transparency, 1.0);
}
