#version 330 core

layout(location = 0) in vec2 vertexPos;
layout(location = 1) in vec2 texPos;
layout(location = 2) in float transparency;

out vec2 TexCoord;
uniform mat4 view_position;

void main() {
	gl_Position = view_position * vec4(vertexPos, 1.999, 1);
	TexCoord = texPos;
}
