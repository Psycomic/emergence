#version 330 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D screenTexture;
uniform float time;

void main() {
	FragColor = texture(screenTexture, TexCoords);
}
