#version 330 core
layout(location = 0) in vec2 vertexPos;

out vec3 fragPos;

uniform vec2 model_position;
uniform mat4 view_position;

void main() {
	vec4 model_vertex = vec4((vertexPos + model_position), 0, 1);

	gl_Position = view_position * model_vertex;
	fragPos = model_vertex.xyz;
}