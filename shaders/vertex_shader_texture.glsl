#version 330 core
layout(location = 0) in vec3 vertexPos;
layout(location = 1) in vec2 texPos;

out vec2 TexCoord;
out vec2 fragPos;

uniform mat4 view_position;
uniform mat4 model;

void main() {
	gl_Position = view_position * model * vec4(vertexPos, 1);

	TexCoord = texPos;
	fragPos = gl_Position.xy;
}