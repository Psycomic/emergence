#version 330 core

layout(location = 0) in vec3 VertexPos;

void main() {
	gl_Position = vec4(VertexPos, 1);
}
