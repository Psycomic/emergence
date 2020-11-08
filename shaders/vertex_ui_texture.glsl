#version 330 core
layout(location = 0) in vec2 vertexPos;
layout(location = 1) in vec2 texPos;

out vec2 TexCoord;
out vec2 fragPos;

uniform mat4 view_position;
uniform vec3 model_position;
uniform vec3 center_position;
uniform float angle = 0.f;

mat2 rotate2d(float _angle) {
	return mat2(cos(_angle), -sin(_angle),
		sin(_angle), cos(_angle));
}

void main() {
	vec2 relative_position = model_position.xy - center_position.xy;
	gl_Position = view_position * vec4(rotate2d(angle) * (vertexPos + relative_position) + center_position.xy, 0.1, 1);

	fragPos = vertexPos.xy + model_position.xy;
	TexCoord = texPos;
}