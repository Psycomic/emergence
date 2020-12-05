#ifndef RENDER_HEADER
#define RENDER_HEADER

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "linear_algebra.h"
#include "images.h"

typedef struct {
	void* data;
	uint data_size;
	uint stride;
	GLuint data_layout;
} ArrayBufferDeclaration;

typedef enum {
	LAYOUT_PACK,
	LAYOUT_GRID,
} Layout;

typedef struct {
	void* (*allocate)(size_t);
	void (*free)(void);
} BlockAllocator;

typedef union {
	struct {
		float screen_x;
		float screen_y;
	} mouse_info;

	struct
	{
		uint keypress;
	} keyboard_info;
} Event;

typedef uint WindowID;

static Vector3 white = { 1, 1, 1 };
static Vector3 red = { 1, 0, 0 };
static Vector3 blue = { 0, 0, 1 };
static Vector3 black = { 0, 0, 0 };
static Vector3 green = { 0, 1, 0 };

typedef void (*EventCallback)(struct Widget*, Event*);

#define DRAWABLE_USES_ELEMENTS (1 << 0)
#define DRAWABLE_SHOW_AXIS (1 << 1)
#define DRAWABLE_NO_DEPTH_TEST (1 << 2)

#ifndef RENDER_INTERNAL

typedef void Material;
typedef void Uniform;
typedef void Drawable;
typedef void Scene;
typedef void Text;
typedef void Widget;

GLuint shader_create(const char* vertex_shader_path, const char* fragment_shader_path);

Material* material_create(GLuint shader, char** uniforms_position, uint uniforms_count);
void material_set_uniform_vec3(Material* material, uint program_id, Vector3 vec);
void material_set_uniform_vec2(Material* material, uint program_id, Vector2 vec);
void material_set_uniform_float(Material* material, uint program_id, float f);
void material_use(Material* material, float* model_matrix, float* position_view_matrix);

GLFWwindow* opengl_window_create(uint width, uint height, const char* title);

GLuint texture_create(Image* image, bool generate_mipmap);

void drawable_update(Drawable* drawable);
void drawable_update_buffer(Drawable* drawable, uint buffer_id);

Scene* scene_create(Vector3 camera_position, GLFWwindow* window);
Drawable* drawable_create(Scene* scene, unsigned short* elements, uint elements_number, ArrayBufferDeclaration* declarations, uint declarations_number, Material* material, GLenum mode, Vector3* position, GLuint* textures, uint textures_count, uint flags);
uint window_create(Scene* scene, float width, float height, float* position, char* title);
void scene_draw(Scene* scene, Vector3 clear_color);
void scene_handle_events(Scene* scene, GLFWwindow* window);

Text* text_create(char* string, GLuint font_texture, float size, Vector2 position, float angle, Vector3 color);
void text_set_color(Text* text, Vector3 color);
void text_set_angle(Text* text, float angle);

Widget* widget_label_create(WindowID window_id, Scene* scene, Widget* parent, char* text, float text_size, float margin, Vector3 color, Layout layout);
Widget* widget_button_create(WindowID window_id, Scene* scene, Widget* parent, char* text, float text_size, float margin, float padding, Layout layout);

void widget_set_on_hover(Widget* widget, EventCallback on_hover);
void widget_set_on_click(Widget* widget, EventCallback on_click);

#endif // RENDER_INTERNAL
#endif // RENDER_HEADER
