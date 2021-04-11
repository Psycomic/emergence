#include "linear_algebra.h"
#include "images.h"
#include "misc.h"

#include <stddef.h>
#include <assert.h>
#include <stdio.h>

// Biggest abstraction yet. A Drawable is a collection of array
// buffers, some flags, a material, a pointer to a position, and some flags.

#include "drawable.h"

#define DRAWBLE_MAX_BUFFER_COUNT 256

static unsigned short rectangle_elements[] = { 0, 1, 2, 1, 3, 2 };

Buffer array_buffer_create(uint size, int type, void* data, GLuint update_rate) {
	GLuint array_buffer;
	glGenBuffers(1, &array_buffer);
	glBindBuffer(type, array_buffer);
	glBufferData(type, size, data, update_rate);

	Buffer buffer = {
		.size = size,
		.data = data,
		.buffer = array_buffer,
		.type = type
	};

	return buffer;
}

void array_buffer_update(Buffer* buffer) {
	glBindBuffer(GL_ARRAY_BUFFER, buffer->buffer);
	glBufferSubData(GL_ARRAY_BUFFER, 0, buffer->size, buffer->data);
}

void array_buffer_destroy(Buffer* buffer) {
	glDeleteBuffers(1, &buffer->buffer);
}

GLuint texture_create(Image* image) {
	GLuint texture;
	glGenTextures(1, &texture);

	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, image->width, image->height, 0,
				 image->color_encoding, GL_UNSIGNED_BYTE, image->data);

	glGenerateMipmap(GL_TEXTURE_2D);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);	// Texture parameters
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);

	return texture;
}

void rectangle_vertices_set(float* rectangle_vertices, float width, float height, uint32_t stride, float x, float y) {
	rectangle_vertices[0] = x; rectangle_vertices[1] = y;
	rectangle_vertices[stride] = width + x; rectangle_vertices[stride + 1] = y;
	rectangle_vertices[stride * 2] = x; rectangle_vertices[stride * 2 + 1] = height + y;
	rectangle_vertices[stride * 3] = width + x; rectangle_vertices[stride * 3 + 1] = height + y;
}

/* Create an abstraction over basic openGL calls */
void drawable_init(Drawable* drawable, unsigned short* elements, uint elements_number, ArrayBufferDeclaration* declarations, uint declarations_count,
				   Material* material, GLenum mode, Vector3* position, GLuint* textures, uint textures_count, uint flags) {
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

void drawable_draw(Drawable* drawable, StateContext* gl) {
	for (uint i = 0; i < drawable->textures_count; i++) {
		StateGlActiveTexure(gl, GL_TEXTURE0 + i);
		StateGlBindTexture(gl, GL_TEXTURE_2D, drawable->textures[i]);
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

void drawable_rectangle_init(Drawable* drawable, float width, float height, Material* material, GLenum mode,
							 Vector3* position, uint flags)
{
	float* rectangle_vertices = malloc(sizeof(float) * 8);
	rectangle_vertices_set(rectangle_vertices, width, height, 2, 0.f, 0.f);

	ArrayBufferDeclaration rectangle_buffers[] = {
		{rectangle_vertices, sizeof(float) * 8, 2, 0, GL_DYNAMIC_DRAW},
	};

	drawable_init(drawable, rectangle_elements, ARRAY_SIZE(rectangle_elements), rectangle_buffers, 1, material, mode,
				  position, NULL, 0, flags);
}

void drawable_rectangle_texture_init(Drawable* drawable, float width, float height, Material* material, GLenum mode,
									 Vector3* position, GLuint* textures, uint textures_count, float* texture_uv, uint flags)
{
	float* rectangle_vertices = malloc(sizeof(float) * 8);
	rectangle_vertices_set(rectangle_vertices, width, height, 2, 0.f, 0.f);

	ArrayBufferDeclaration rectangle_buffers[] = {
		{rectangle_vertices, sizeof(float) * 8, 2, 0, GL_DYNAMIC_DRAW},
		{texture_uv, sizeof(float) * 8, 2, 1, GL_STATIC_DRAW}
	};

	drawable_init(drawable, rectangle_elements, ARRAY_SIZE(rectangle_elements), rectangle_buffers, 2, material, mode,
				  position, textures, textures_count, flags);
}

void drawable_rectangle_set_size(Drawable* rectangle, float width, float height) {
	float* rectangle_vertices = rectangle->buffers[0].data;
	rectangle_vertices_set(rectangle_vertices, width, height, 2, 0.f, 0.f);

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

void StateGlEnable(StateContext* gl, GLuint thing) {
	uint64_t state = 0;

	switch (thing) {
	case GL_BLEND:
		state = STATE_GL_BLEND;
		break;
	case GL_DEPTH_TEST:
		state = STATE_GL_DEPTH_TEST;
		break;
	case GL_CULL_FACE:
		state = STATE_GL_CULL_FACE;
		break;
	case GL_STENCIL_TEST:
		state = STATE_GL_STENCIL_TEST;
		break;
	default:
		assert(1);
	}

	if (!(gl->state & state))
		glEnable(thing);

	gl->state |= state;
}

void StateGlDisable(StateContext* gl, GLuint thing) {
	uint64_t state = 0;

	switch (thing) {
	case GL_BLEND:
		state = STATE_GL_BLEND;
		break;
	case GL_DEPTH_TEST:
		state = STATE_GL_DEPTH_TEST;
		break;
	case GL_CULL_FACE:
		state = STATE_GL_CULL_FACE;
		break;
	case GL_STENCIL_TEST:
		state = STATE_GL_STENCIL_TEST;
		break;
	default:
		assert(1);
	}

	if (gl->state & state)
		glDisable(thing);

	gl->state &= ~state;
}

void StateGlActiveTexure(StateContext* gl, GLuint texture_id) {
	if (gl->active_texture != texture_id)
		glActiveTexture(texture_id);

	gl->active_texture = texture_id;
}

void StateGlBindTexture(StateContext* gl, GLuint type, GLuint texture) {
	if (gl->bound_texture != texture)
		glBindTexture(type, texture);

	gl->bound_texture = texture;
}

void StateGlUseProgram(StateContext* gl, GLuint program_id) {
	if (gl->bound_program != program_id)
		glUseProgram(program_id);

	gl->bound_program = program_id;
}

void get_opengl_errors_f(const char* file, int line) {
	GLenum err;
	while((err = glGetError()) != GL_NO_ERROR) {
		printf("====OPENGL ERROR DETECTED====\n%s:%d, ", file, line);

		switch (err) {
		case GL_INVALID_ENUM:
			printf("INVALID ENUM!\n");
			break;
		case GL_INVALID_OPERATION:
			printf("INVALID OPERATION!\n");
			break;
		case GL_INVALID_VALUE:
			printf("INVALID VALUE!\n");
			break;
		case GL_STACK_OVERFLOW:
			printf("STACK OVERFLOW!\n");
			break;
		case GL_STACK_UNDERFLOW:
			printf("STACK UNDERFLOW!\n");
			break;
		case GL_OUT_OF_MEMORY:
			printf("OUT OF MEMORY!\n");
			break;
		case GL_INVALID_FRAMEBUFFER_OPERATION:
			printf("INVALID FRAMEBUFFER OPERATION!\n");
			break;
		default:
			printf("Unknown error");
			break;
		}

		exit(-1);
	}
}

GLuint shader_create(const char* vertex_shader_path, const char* fragment_shader_path) {
	GLboolean errors_occurred = GL_FALSE;

	// Create the shaders
	GLuint VertexShaderID = glCreateShader(GL_VERTEX_SHADER);
	GLuint FragmentShaderID = glCreateShader(GL_FRAGMENT_SHADER);

	// Read the Vertex Shader code from the file
	char* VertexShaderCode = read_file(vertex_shader_path);
	if (VertexShaderCode == NULL)
		goto error;

	// Read the Fragment Shader code from the file
	char* FragmentShaderCode = read_file(fragment_shader_path);
	if (FragmentShaderCode == NULL)
		goto error;

	GLint Result = GL_FALSE;
	int InfoLogLength;

	// Compile Vertex Shader
	printf("Compiling shader : %s\n", vertex_shader_path);
	char const* VertexSourcePointer = VertexShaderCode;

	glShaderSource(VertexShaderID, 1, &VertexSourcePointer, NULL);
	glCompileShader(VertexShaderID);

	free(VertexShaderCode);

	// Check Vertex Shader
	glGetShaderiv(VertexShaderID, GL_COMPILE_STATUS, &Result);
	glGetShaderiv(VertexShaderID, GL_INFO_LOG_LENGTH, &InfoLogLength);

	if (Result != GL_TRUE) {
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

	free(FragmentShaderCode);

	// Check Fragment Shader
	glGetShaderiv(FragmentShaderID, GL_COMPILE_STATUS, &Result);
	glGetShaderiv(FragmentShaderID, GL_INFO_LOG_LENGTH, &InfoLogLength);

	if (Result != GL_TRUE) {
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

	get_opengl_errors();

	return ProgramID;

error:
	fprintf(stderr, "Could not read shaders %s and %s", vertex_shader_path, fragment_shader_path);
	return 0;
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

		if (new_material->uniforms[i].location < 0)
			printf("Uniform %s doesn't exist!\n", uniforms_position[i]);

		assert(new_material->uniforms[i].location >= 0);
	}

	get_opengl_errors();

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

void material_use(Material* material, StateContext* gl, float* model_matrix, float* view_position_matrix) {
	StateGlUseProgram(gl, material->program_id);

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

	get_opengl_errors();
}


#undef DRAWABLE_INTERAL
