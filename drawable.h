#ifndef DRAWABLE_HEADER
#define DRAWABLE_HEADER

#include "linear_algebra.h"
#include "images.h"

#include <GL/glew.h>

typedef struct {
	void* data;
	uint data_size;
	uint stride;
	GLuint data_layout;
	GLuint update_rate;
} ArrayBufferDeclaration;


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
	GLenum type;
	GLuint buffer;
} Buffer;

// OpenGL context information
#define STATE_GL_DEPTH_TEST		(1 << 0)
#define STATE_GL_BLEND			(1 << 1)
#define STATE_GL_CULL_FACE		(1 << 2)
#define STATE_GL_STENCIL_TEST	(1 << 3)

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

#define DRAWABLE_USES_ELEMENTS (1 << 0)
#define DRAWABLE_SHOW_AXIS (1 << 1)
#define DRAWABLE_NO_DEPTH_TEST (1 << 2)

GLuint shader_create(const char* vertex_shader_path, const char* fragment_shader_path);

Material* material_create(GLuint shader, char** uniforms_position, uint uniforms_count);
void material_set_uniform_vec3(Material* material, uint program_id, Vector3 vec);
void material_set_uniform_vec2(Material* material, uint program_id, Vector2 vec);
void material_set_uniform_float(Material* material, uint program_id, float f);
void material_uniform_vec2(Material* material, uint uniform_id, Vector2 vec);
void material_uniform_vec3(Material* material, uint uniform_id, Vector3 vec);
void material_use(Material* material, float* model_matrix, float* position_view_matrix);

GLuint texture_create(Image* image);

void get_opengl_errors_f();

#ifdef _DEBUG
#define get_opengl_errors() get_opengl_errors_f(__FILE__, __LINE__)
#else
#define get_opengl_errors() (void)0
#endif

void drawable_update(Drawable* drawable);
void drawable_update_buffer(Drawable* drawable, uint buffer_id);

void drawable_draw(Drawable* drawable);
void drawable_destroy(Drawable* drawable);

void drawable_init(Drawable* drawable, unsigned int* elements, uint elements_number, ArrayBufferDeclaration* declarations, uint declarations_count, Material* material, GLenum mode, Vector3* position, GLuint* textures, uint textures_count, uint flags);
void drawable_rectangle_texture_init(Drawable* drawable, float width, float height, Material* material, GLenum mode, Vector3* position, GLuint* textures, uint textures_count, float* texture_uv, uint flags);
void drawable_rectangle_init(Drawable* drawable, float width, float height, Material* material, GLenum mode, Vector3* position, uint flags);

void drawable_rectangle_set_size(Drawable* rectangle, float width, float height);
void rectangle_vertices_set(float* rectangle_vertices, float width, float height, uint32_t stride, float x, float y);

#endif // !DRAWABLE_HEADER
