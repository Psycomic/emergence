#version 330 core
in vec2 fragment_uvs;
in vec4 fragment_color;
out vec4 out_color;

uniform sampler2D tex;

void main() {
	out_color = fragment_color * texture(tex, fragment_uvs);
}
