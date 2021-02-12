#version 330 core

out vec4 FragColor;

in vec2 TexCoord;

uniform sampler2D tex;

vec3 color = vec3(1, 1, 1);

void main(void) {
	vec3 texture_fragment = texture(tex, TexCoord).xyz;
	FragColor = vec4(color * texture_fragment.x, texture_fragment.x);
}
