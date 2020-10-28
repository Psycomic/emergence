#version 330 core
layout(location = 0) in vec2 vertexPos;

out vec3 fragPos;

uniform vec3 model_position;

void main() {
	vec4 model_vertex = vec4(vertexPos + model_position.xy, 0, 1);

	gl_Position = model_vertex;
	fragPos = model_vertex.xyz;
}