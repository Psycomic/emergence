#version 330 core
out vec4 FragColor;

in vec2 TexCoord;
in vec2 fragPos;

uniform sampler2D tex;

uniform float transparency = 1.0;

uniform float max_width = 0.1f;
uniform float max_height = 0.1f;

uniform vec2 anchor_position;
uniform vec3 color = vec3(1, 0, 0);

void main(void) {
	vec3 texture_fragment = texture(tex, TexCoord).xyz;
	vec2 real_pos = fragPos.xy - anchor_position.xy;

	float width_transparency = real_pos.x > max_width ? 0.f : 1.f;
	float height_transparency = -real_pos.y > max_height ? 0.f : 1.f;

	FragColor = vec4(color * texture_fragment.x, transparency * texture_fragment.x * width_transparency * height_transparency);
}