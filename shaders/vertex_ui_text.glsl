#version 330 core

layout(location = 0) in vec3 vertexPos; // 0 1 2
layout(location = 1) in vec2 texPos; // 3 4
layout(location = 2) in float transparency; // 5
layout(location = 3) in vec3 color; // 6 7 8

out vec2 TexCoord;
out float FragTransparency;
out vec3 vertexColor;

uniform mat4 view_position;

void main() {
	gl_Position = view_position * vec4(vertexPos, 1);
	TexCoord = texPos;
	FragTransparency = transparency;
	vertexColor = color;
}
