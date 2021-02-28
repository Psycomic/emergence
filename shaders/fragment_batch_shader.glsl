#version 330 core

in float transparency;
in vec2 texture_coords;
in float aspect_ratio;

out vec4 FragColor;

const float border_size = 0.01f;

void main() {
	if (texture_coords.x <= border_size / aspect_ratio || texture_coords.x >= 1.f - border_size / aspect_ratio ||
		texture_coords.y <= border_size || texture_coords.y >= 1.f - border_size)
		FragColor = vec4(transparency, transparency, transparency, 1);
	else
		FragColor = vec4(0.0, 0.0, transparency, 1.0);
}
