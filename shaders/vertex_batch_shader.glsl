#version 330 core

layout(location = 0) in vec2 VertexPos;
layout(location = 1) in vec3 VertexColor;

out vec3 color;

void main() {
	color = VertexColor;
	gl_Position = vec4(VertexPos, 0, 1);
}
