#version 330 core

layout(location = 0) in vec3 VertexPos;
layout(location = 1) in float VertexTransparency;

out float transparency;

uniform mat4 view_position;

void main() {
	transparency = VertexTransparency;
	gl_Position = view_position * vec4(VertexPos, 1);
}
