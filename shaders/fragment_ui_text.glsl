#version 330 core

out vec4 FragColor;

in vec2 TexCoord;
in float FragTransparency;
in vec3 vertexColor;

uniform sampler2D tex;

void main(void) {
	vec3 texture_fragment = texture(tex, TexCoord).xyz;
	FragColor = vec4(vertexColor * texture_fragment.x, texture_fragment.x * FragTransparency);
}
