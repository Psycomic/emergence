#version 330 core

out vec4 FragColor;
in vec3 fragmentPos;

uniform vec3 lower_color;
uniform vec3 upper_color;
uniform float strenght;

void main() {
	FragColor = vec4(
		mix(lower_color.x, upper_color.x, fragmentPos.y / strenght),
		mix(lower_color.y, upper_color.y, fragmentPos.y / strenght),
		mix(lower_color.z, upper_color.z, fragmentPos.y / strenght), 1);
}