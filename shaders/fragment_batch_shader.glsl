#version 330 core

in float transparency;
out vec4 FragColor;

void main() {
	FragColor = vec4(0.0, 0.0, 0.7, transparency);
}
