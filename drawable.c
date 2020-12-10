#define DRAWABLE_INTERAL

#include "linear_algebra.h"
#include "images.h"
#include "misc.h"

#include <stddef.h>
#include <assert.h>
#include <stdlib.h>

// Shader's uniform abstraction. 
typedef struct {
	enum {
		UNIFORM_VEC3, UNIFORM_VEC2,
		UNIFORM_FLOAT, UNIFORM_BOOL
	} type;

	union {
		Vector3 vec3;
		Vector2 vec2;
		float f;
		uint b;
	} data;

	GLint location;
	GLboolean is_set;
} Uniform;

// Basically a collection of uniforms for a particular shader.
typedef struct {
	GLuint program_id;
	GLint view_position_matrix_location;
	GLint model_matrix_location;

	uint uniforms_count;
	Uniform uniforms[];
} Material;

// Array Buffer abstraction
typedef struct {
	void* data;
	uint size;
	GLuint buffer;
} Buffer;

// Biggest abstraction yet. A Drawable is a collection of array
// buffers, some flags, a material, a pointer to a position, and some flags.

#define DRAWBLE_MAX_BUFFER_COUNT 256

typedef struct {
	Buffer elements_buffer;

	Material* material;
	Vector3* position;
	GLuint* textures;

	GLuint vertex_array;
	GLenum draw_mode;

	uint textures_count;
	uint elements_count;
	uint buffer_count;
	uint flags;

	Buffer buffers[];
} Drawable;

#include "drawable.h"

static unsigned short rectangle_elements[] = { 0, 1, 2, 1, 3, 2 };

Buffer array_buffer_create(uint size, int type, void* data, GLuint update_rate) {
	GLuint array_buffer;
	glGenBuffers(1, &array_buffer);
	glBindBuffer(type, array_buffer);
	glBufferData(type, size, data, update_rate);

	Buffer buffer;
	buffer.buffer = array_buffer;
	buffer.data = data;
	buffer.size = size;

	return buffer;
}

void array_buffer_update(Buffer* buffer) {
	glBindBuffer(GL_ARRAY_BUFFER, buffer->buffer);
	glBufferSubData(GL_ARRAY_BUFFER, 0, buffer->size, buffer->data);
}

void array_buffer_destroy(Buffer* buffer) {
	glDeleteBuffers(1, &buffer->buffer);
}

GLuint texture_create(Image* image, bool generate_mipmap) {
	GLuint texture;
	glGenTextures(1, &texture);

	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, image->width, image->height, 0, image->color_encoding, GL_UNSIGNED_BYTE, image->data);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	if (generate_mipmap)
		glGenerateMipmap(GL_TEXTURE_2D);

	return texture;
}

void rectangle_vertices_set(float* rectangle_vertices, float width, float height) {
	rectangle_vertices[0] = 0.f; rectangle_vertices[1] = 0.f;
	rectangle_vertices[2] = width; rectangle_vertices[3] = 0.f;
	rectangle_vertices[4] = 0.f; rectangle_vertices[5] = height;
	rectangle_vertices[6] = width; rectangle_vertices[7] = height;
}

Drawable* drawable_allocate(uint buffer_count) {
	return malloc(sizeof(Drawable) + buffer_count * sizeof(Buffer));
}

void drawable_init(Drawable* drawable, unsigned short* elements, uint elements_number, ArrayBufferDeclaration* declarations, uint declarations_count, Material* material, GLenum mode, Vector3* position, GLuint* textures, uint textures_count, uint flags) {
	drawable->buffer_count = declarations_count;
	drawable->material = material;
	drawable->position = position;
	drawable->textures = textures;
	drawable->textures_count = textures_count;
	drawable->flags = flags;
	drawable->draw_mode = mode;

	glGenVertexArrays(1, &drawable->vertex_array);
	glBindVertexArray(drawable->vertex_array);

	for (uint i = 0; i < declarations_count; i++) {
		drawable->buffers[i] = array_buffer_create(declarations[i].data_size, GL_ARRAY_BUFFER, declarations[i].data, declarations[i].update_rate);

		glBindBuffer(GL_ARRAY_BUFFER, drawable->buffers[i].buffer);

		glEnableVertexAttribArray(declarations[i].data_layout);

		glVertexAttribPointer(
			declarations[i].data_layout,				// Index 
			declarations[i].stride,						// Size
			GL_FLOAT,									// Type
			GL_FALSE,									// Normalized?
			declarations[i].stride * sizeof(float),		// Stride
			NULL										// Pointer
		);
	}

	drawable->elements_count = elements_number;

	if (elements != NULL) {
		drawable->elements_buffer = array_buffer_create(elements_number * sizeof(unsigned short), GL_ELEMENT_ARRAY_BUFFER, elements, GL_STATIC_DRAW);
		drawable->flags |= DRAWABLE_USES_ELEMENTS;
	}
	else {
		drawable->elements_buffer.buffer = 0;
		drawable->flags &= ~DRAWABLE_USES_ELEMENTS;
	}

	glBindVertexArray(0);
}

void drawable_destroy(Drawable* drawable) {
	for (uint i = 0; i < drawable->buffer_count; i++)
		array_buffer_destroy(&drawable->buffers[i]);

	if (drawable->flags & DRAWABLE_USES_ELEMENTS)
		array_buffer_destroy(&drawable->elements_buffer);

	glDeleteVertexArrays(1, &drawable->vertex_array);

	free(drawable->material);
	free(drawable);
}

void drawable_draw(Drawable* drawable) {
	for (uint i = 0; i < drawable->textures_count; i++) {
		glActiveTexture(GL_TEXTURE0 + i);
		glBindTexture(GL_TEXTURE_2D, drawable->textures[i]);
	}

	glBindVertexArray(drawable->vertex_array);

	if ((drawable->flags & DRAWABLE_USES_ELEMENTS) > 0)
		glDrawElements(drawable->draw_mode, drawable->elements_count, GL_UNSIGNED_SHORT, NULL);
	else
		glDrawArrays(drawable->draw_mode, 0, drawable->elements_count);
}

void drawable_update_buffer(Drawable* drawable, uint buffer_id) {
	array_buffer_update(&drawable->buffers[buffer_id]);
}

void drawable_update(Drawable* drawable) {
	for (uint i = 0; i < drawable->buffer_count; i++)
		array_buffer_update(&drawable->buffers[i]);
}

void drawable_rectangle_init(Drawable* drawable, float width, float height, Material* material, GLenum mode, Vector3* position, uint flags) {
	float* rectangle_vertices = malloc(sizeof(float) * 8);
	rectangle_vertices_set(rectangle_vertices, width, height);

	ArrayBufferDeclaration rectangle_buffers[] = {
		{rectangle_vertices, sizeof(float) * 8, 2, 0, GL_DYNAMIC_DRAW},
	};

	drawable_init(drawable, rectangle_elements, ARRAY_SIZE(rectangle_elements), rectangle_buffers, 1, material, mode, position, NULL, 0, flags);
}

void drawable_rectangle_texture_init(Drawable* drawable, float width, float height, Material* material, GLenum mode, Vector3* position, GLuint* textures, uint textures_count, float* texture_uv, uint flags) {
	float* rectangle_vertices = malloc(sizeof(float) * 8);
	rectangle_vertices_set(rectangle_vertices, width, height);

	ArrayBufferDeclaration rectangle_buffers[] = {
		{rectangle_vertices, sizeof(float) * 8, 2, 0, GL_DYNAMIC_DRAW},
		{texture_uv, sizeof(float) * 8, 2, 1, GL_STATIC_DRAW}
	};

	drawable_init(drawable, rectangle_elements, ARRAY_SIZE(rectangle_elements), rectangle_buffers, 2, material, mode, position, textures, textures_count, flags);
}

void drawable_rectangle_set_size(Drawable* rectangle, float width, float height) {
	float* rectangle_vertices = rectangle->buffers[0].data;
	rectangle_vertices_set(rectangle_vertices, width, height);

	drawable_update_buffer(rectangle, 0);
}

Material* drawable_material(Drawable* drawable) {
	return drawable->material;
}

Vector3 drawable_position(Drawable* drawable) {
	return *drawable->position;
}

uint drawable_flags(Drawable* drawable) {
	return drawable->flags;
}

void* drawable_buffer_data(Drawable* drawable, uint buffer_id) {
	return drawable->buffers[buffer_id].data;
}

GLuint shader_create(const char* vertex_shader_path, const char* fragment_shader_path) {
	GLboolean errors_occurred = GL_FALSE;

	// Create the shaders
	GLuint VertexShaderID = glCreateShader(GL_VERTEX_SHADER);
	GLuint FragmentShaderID = glCreateShader(GL_FRAGMENT_SHADER);

	// Read the Vertex Shader code from the file
	char VertexShaderCode[2048];
	read_file(VertexShaderCode, vertex_shader_path);

	// Read the Fragment Shader code from the file
	char FragmentShaderCode[2048];
	read_file(FragmentShaderCode, fragment_shader_path);

	GLint Result = GL_FALSE;
	int InfoLogLength;

	// Compile Vertex Shader
	printf("Compiling shader : %s\n", vertex_shader_path);
	char const* VertexSourcePointer = VertexShaderCode;

	glShaderSource(VertexShaderID, 1, &VertexSourcePointer, NULL);
	glCompileShader(VertexShaderID);

	// Check Vertex Shader
	glGetShaderiv(VertexShaderID, GL_COMPILE_STATUS, &Result);
	glGetShaderiv(VertexShaderID, GL_INFO_LOG_LENGTH, &InfoLogLength);

	if (InfoLogLength > 0) {
		char* VertexShaderErrorMessage = malloc(sizeof(char) * ((size_t)InfoLogLength + 1));
		glGetShaderInfoLog(VertexShaderID, InfoLogLength, NULL, VertexShaderErrorMessage);
		printf("%s\n", VertexShaderErrorMessage);

		free(VertexShaderErrorMessage);

		errors_occurred = GL_TRUE;
	}

	// Compile Fragment Shader
	printf("Compiling shader : %s\n", fragment_shader_path);
	char const* FragmentSourcePointer = FragmentShaderCode;
	glShaderSource(FragmentShaderID, 1, &FragmentSourcePointer, NULL);
	glCompileShader(FragmentShaderID);

	// Check Fragment Shader
	glGetShaderiv(FragmentShaderID, GL_COMPILE_STATUS, &Result);
	glGetShaderiv(FragmentShaderID, GL_INFO_LOG_LENGTH, &InfoLogLength);

	if (InfoLogLength > 0) {
		char* FragmentShaderErrorMessage = malloc(sizeof(char) * ((size_t)InfoLogLength + 1));
		glGetShaderInfoLog(FragmentShaderID, InfoLogLength, NULL, FragmentShaderErrorMessage);
		printf("%s\n", FragmentShaderErrorMessage);

		free(FragmentShaderErrorMessage);

		errors_occurred = GL_TRUE;
	}

	// Link the program
	printf("Linking program\n");
	GLuint ProgramID = glCreateProgram();
	glAttachShader(ProgramID, VertexShaderID);
	glAttachShader(ProgramID, FragmentShaderID);
	glLinkProgram(ProgramID);

	// Check the program
	glGetProgramiv(ProgramID, GL_LINK_STATUS, &Result);
	glGetProgramiv(ProgramID, GL_INFO_LOG_LENGTH, &InfoLogLength);

	if (InfoLogLength > 0) {
		char* ProgramErrorMessage = malloc(sizeof(char) * ((size_t)InfoLogLength + 1));
		glGetProgramInfoLog(ProgramID, InfoLogLength, NULL, &ProgramErrorMessage[0]);
		printf("%s\n", ProgramErrorMessage);

		free(ProgramErrorMessage);

		errors_occurred = GL_TRUE;
	}

	glDetachShader(ProgramID, VertexShaderID);
	glDetachShader(ProgramID, FragmentShaderID);

	glDeleteShader(VertexShaderID);
	glDeleteShader(FragmentShaderID);

	assert(!errors_occurred);

	return ProgramID;
}

Material* material_create(GLuint shader, char** uniforms_position, uint uniforms_count) {
	Material* new_material = malloc(sizeof(Material) + sizeof(Uniform) * uniforms_count);

	assert(new_material != NULL);

	new_material->program_id = shader;

	new_material->uniforms_count = uniforms_count;

	new_material->view_position_matrix_location = glGetUniformLocation(shader, "view_position");
	new_material->model_matrix_location = glGetUniformLocation(shader, "model");

	for (uint i = 0; i < uniforms_count; i++) {
		new_material->uniforms[i].location = glGetUniformLocation(shader, uniforms_position[i]);
		new_material->uniforms[i].is_set = GL_FALSE;

		assert(new_material->uniforms[i].location >= 0);
	}

	return new_material;
}

void material_set_uniform_vec3(Material* material, uint uniform_id, Vector3 vec) {
	assert(uniform_id < material->uniforms_count);

	material->uniforms[uniform_id].type = UNIFORM_VEC3;
	material->uniforms[uniform_id].data.vec3 = vec;
	material->uniforms[uniform_id].is_set = GL_TRUE;
}

void material_set_uniform_vec2(Material* material, uint uniform_id, Vector2 vec) {
	assert(uniform_id < material->uniforms_count);

	material->uniforms[uniform_id].type = UNIFORM_VEC2;
	material->uniforms[uniform_id].data.vec2 = vec;
	material->uniforms[uniform_id].is_set = GL_TRUE;
}

void material_set_uniform_float(Material* material, uint uniform_id, float f) {
	assert(uniform_id < material->uniforms_count);

	material->uniforms[uniform_id].type = UNIFORM_FLOAT;
	material->uniforms[uniform_id].data.f = f;
	material->uniforms[uniform_id].is_set = GL_TRUE;
}

void material_set_uniform_bool(Material* material, uint uniform_id, uint b) {
	assert(uniform_id < material->uniforms_count);

	material->uniforms[uniform_id].type = UNIFORM_BOOL;
	material->uniforms[uniform_id].data.b = b;
	material->uniforms[uniform_id].is_set = GL_TRUE;
}

void material_uniform_vec2(Material* material, uint uniform_id, Vector2 vec) {
	glUniform2f(material->uniforms[uniform_id].location, vec.x, vec.y);
}

void material_uniform_vec3(Material* material, uint uniform_id, Vector3 vec) {
	glUniform3f(material->uniforms[uniform_id].location, vec.x, vec.y, vec.z);
}

void material_use(Material* material, float* model_matrix, float* view_position_matrix) {
	glUseProgram(material->program_id);

	if (view_position_matrix)
		glUniformMatrix4fv(material->view_position_matrix_location, 1, GL_FALSE, view_position_matrix);
	if (model_matrix)
		glUniformMatrix4fv(material->model_matrix_location, 1, GL_FALSE, model_matrix);

	Uniform* uniforms = material->uniforms;

	for (uint i = 0; i < material->uniforms_count; i++) {
		if (uniforms[i].is_set) {
			GLuint uniform_id = uniforms[i].location;

			switch (material->uniforms[i].type) {
			case UNIFORM_VEC3:
				glUniform3f(uniform_id, uniforms[i].data.vec3.x, uniforms[i].data.vec3.y, uniforms[i].data.vec3.z);
				break;
			case UNIFORM_VEC2:
				glUniform2f(uniform_id, uniforms[i].data.vec2.x, uniforms[i].data.vec2.y);
				break;
			case UNIFORM_FLOAT:
				glUniform1f(uniform_id, uniforms[i].data.f);
				break;
			case UNIFORM_BOOL:
				glUniform1i(uniform_id, uniforms[i].data.b);
				break;
			}
		}
	}
}


#undef DRAWABLE_INTERAL
