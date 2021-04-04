#version 330 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D screenTexture;
uniform float time;

float remapped_sin(float x) {
	return clamp((sin(x) + 1.9) / 2, 0, 1);
}

void main() {
	vec4 color = texture(screenTexture, TexCoords);

    FragColor = vec4(color.xy, color.z * remapped_sin((TexCoords.y + time) * 1000), 1.0);
}
