#version 330 core

layout(location = 0) in vec3 VertexPos;
layout(location = 1) in float VertexTransparency;
layout(location = 2) in vec2 TexCoords;
layout(location = 3) in vec2 QuadSize;

out float transparency;
out vec2 texture_coords;
out vec2 size;

uniform mat4 view_position;

void main() {
	transparency = VertexTransparency;
	gl_Position = view_position * vec4(VertexPos.xyz, 1);

	texture_coords = TexCoords;
	size = QuadSize;
}
