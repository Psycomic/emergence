#version 330 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D screenTexture;
uniform float time;

void main() {
	vec4 color = texture(screenTexture, TexCoords);
	// float avg = (color.x + color.y + color.z) / 3.f;

    // FragColor = vec4(avg, avg, avg, 1.0);
	FragColor = color;
}
