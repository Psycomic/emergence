#ifndef RENDER_HEADER
#define RENDER_HEADER

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "linear_algebra.h"
#include "images.h"
#include "drawable.h"
#include "camera.h"
#include "misc.h"

// What you see on the screen. It is basically the container of every
// graphical aspect of the game : the 3D view and the 2D UI.
typedef struct Scene {
	DynamicArray drawables;	// Drawables array
	Camera camera;

	GLFWwindow* context;

	uint quad_vao, quad_vbo;

	uint glfw_last_character;
	uint flags;
	uint fbo;
	uint fbo_color_buffer;
	uint rbo;
} Scene;

int initialize_everything();

GLFWwindow* scene_context(Scene* scene);
void scene_quit(Scene *scene);
int scene_should_close(Scene* scene);
// Create a scene instance
Scene* scene_create(Vector3 camera_position, int width, int height, const char* title);
Drawable* scene_create_drawable(Scene* scene, uint* elements, uint elements_number, ArrayBufferDeclaration* declarations, uint declarations_number, Material* material, GLenum mode, Vector3* position, GLuint* textures,	uint textures_count, uint flags);

// The main function of the whole renderer logic
void scene_draw(Scene* scene, Vector3 clear_color);
void scene_handle_events(Scene* scene, GLFWwindow* window);
void scene_set_size(Scene* scene, float width, float height);

void scene_character_callback(Scene* scene, unsigned int codepoint);
void scene_resize_callback(Scene* scene, int width, int height);
void scene_scroll_callback(Scene* scene, float xoffset, float yoffset);

#endif // RENDER_HEADER
