#ifndef DRAWABLE_HEADER
#define DRAWABLE_HEADER

#include "linear_algebra.h"

#include <GL/glew.h>

typedef struct {
	void* data;
	uint data_size;
	uint stride;
	GLuint data_layout;
	GLuint update_rate;
} ArrayBufferDeclaration;

#ifndef DRAWABLE_INTERAL

typedef void Material;
typedef void Uniform;
typedef void Drawable;

#endif // !DRAWABLE_INTERAL

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

GLuint texture_create(Image* image, bool generate_mipmap);

void drawable_update(Drawable* drawable);
void drawable_update_buffer(Drawable* drawable, uint buffer_id);

Material* drawable_material(Drawable* drawable);
uint drawable_flags(Drawable* drawable);
Vector3 drawable_position(Drawable* drawable);
void* drawable_buffer_data(Drawable* drawable, uint buffer_id);

void drawable_draw(Drawable* drawable);

Drawable* drawable_allocate(uint buffer_count);
void drawable_destroy(Drawable* drawable);

void drawable_init(Drawable* drawable, unsigned short* elements,
	uint elements_number, ArrayBufferDeclaration* declarations, uint declarations_count, 
	Material* material, GLenum mode, Vector3* position, GLuint* textures, uint textures_count, uint flags);
void drawable_rectangle_texture_init(Drawable* drawable, float width, float height,
	Material* material, GLenum mode, Vector3* position, GLuint* textures, uint textures_count,
	float* texture_uv, uint flags);
void drawable_rectangle_init(Drawable* drawable, float width, float height,
	Material* material, GLenum mode, Vector3* position, uint flags);

void drawable_rectangle_set_size(Drawable* rectangle, float width, float height);

#endif // !DRAWABLE_HEADER