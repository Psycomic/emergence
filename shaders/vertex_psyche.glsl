#version 330 core
layout (location = 0) in vec2 vertex_position;
layout (location = 1) in vec2 vertex_uvs;
layout (location = 2) in vec4 vertex_color;

uniform mat4 matrix_transform;

out vec2 fragment_uvs;
out vec4 fragment_color;

void main() {
	gl_Position = matrix_transform * vec4(vertex_position, 0, 1);
	fragment_uvs = vertex_uvs;
	fragment_color = vertex_color;
}
