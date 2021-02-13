#version 330 core

layout(location = 0) in vec2 vertexPos;
layout(location = 1) in vec2 texPos;
layout(location = 2) in float transparency;
layout(location = 3) in vec3 color;

out vec2 TexCoord;
out float FragTransparency;
out vec3 vertexColor;

uniform mat4 view_position;

void main() {
	gl_Position = view_position * vec4(vertexPos, 1.999, 1);
	TexCoord = texPos;
	FragTransparency = transparency;
	vertexColor = color;
}
