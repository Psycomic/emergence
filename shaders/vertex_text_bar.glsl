#version 330 core

layout(location = 0) in vec3 vertexPos; // 0 1 2
layout(location = 1) in vec3 color; // 3 4 5

out vec3 vertexColor;

uniform mat4 view_position;

void main() {
	gl_Position = view_position * vec4(vertexPos, 1);
	vertexColor = color;
}
