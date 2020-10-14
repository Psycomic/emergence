#version 330 core
layout(location = 0) in vec3 vertexPos;

uniform mat4 view_position;
uniform mat4 model;

void main() {
	gl_Position = view_position * model * vec4(vertexPos, 1);
}