#version 330 core

out vec4 FragColor;
in vec3 fragPos;

uniform vec3 color = vec3(0, 1, 0);
uniform float transparency = 1;

void main() {
	FragColor = vec4(color, transparency);
}