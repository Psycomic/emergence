#version 330 core

out vec4 FragColor;

in vec2 TexCoord;

uniform sampler2D tex;
uniform float transparency = 1.0;

vec3 color = vec3(1, 0, 0);

void main(void) {
	vec3 texture_fragment = texture(tex, TexCoord).xyz;
	FragColor = vec4(color * texture_fragment.x, texture_fragment.x);
}
