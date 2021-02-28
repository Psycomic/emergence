#version 330 core

layout(location = 0) in vec3 VertexPos;
layout(location = 1) in float VertexTransparency;
layout(location = 2) in vec2 TexCoords;
layout(location = 3) in float AspectRatio;

out float transparency;
out vec2 texture_coords;
out float aspect_ratio;

uniform mat4 view_position;

void main() {
	transparency = VertexTransparency;
	gl_Position = view_position * vec4(VertexPos, 1);

	texture_coords = TexCoords;
	aspect_ratio = AspectRatio;
}
