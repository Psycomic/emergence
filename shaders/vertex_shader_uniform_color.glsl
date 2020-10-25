#version 330 core
layout(location = 0) in vec3 vertexPos;
out vec3 fragPos;

uniform mat4 view_position;
uniform mat4 model;

void main() {
	gl_Position = view_position * model * vec4(vertexPos, 1);
	fragPos = gl_Position.xyz;
}