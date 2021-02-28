#version 330 core

out vec4 FragColor;

in vec2 TexCoord;
in float FragTransparency;
in vec3 vertexColor;

uniform sampler2D tex;

void main(void) {
	vec3 texture_fragment = texture(tex, TexCoord).xyz;
	vec4 base_color = vec4(vertexColor, texture_fragment.x);

	float dist_alpha_mask = texture_fragment.x;

	if (dist_alpha_mask <= 0.01)
		discard;

	base_color.w *= smoothstep(0.0, 0.3, dist_alpha_mask);

	FragColor = vec4(base_color.xyz, base_color.w * FragTransparency);
}
