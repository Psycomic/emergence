#version 330 core
out vec4 FragColor;

in vec2 TexCoord;
in vec2 fragPos;

uniform sampler2D tex;
uniform float transparency = 1.0;

void main(void) {
    vec3 texture_fragment = texture(tex, TexCoord).xyz;

    FragColor = vec4(texture_fragment, transparency);
}