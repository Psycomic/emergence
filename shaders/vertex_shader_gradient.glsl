#version 330 core
layout(location = 0) in vec3 vertexPos;

out vec3 fragmentPos;

uniform mat4 view_position;
uniform mat4 model;

void main(){
	vec4 model_vertex = model * vec4(vertexPos, 1);

	gl_Position =  view_position * model_vertex;
	fragmentPos = model_vertex.xyz;
}