#ifndef RENDER_HEADER
#define RENDER_HEADER

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "linear_algebra.h"
#include "images.h"
#include "drawable.h"

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

	struct {
		uint keypress;
	} keyboard_info;
} Event;

typedef int WindowID;

#ifndef RENDER_INTERNAL

extern const Vector3 white;
extern const Vector3 red;
extern const Vector3 blue;
extern const Vector3 black;
extern const Vector3 green;

typedef void Scene;
typedef void Text;
typedef void Widget;

typedef void (*EventCallback)(Widget*, Event*);

int initialize_everything();

GLFWwindow* scene_context(Scene* scene);
void scene_quit(Scene *scene);
int scene_should_close(Scene* scene);
Scene* scene_create(Vector3 camera_position, int width, int height, const char* title);
Drawable* scene_create_drawable(Scene* scene, unsigned short* elements, uint elements_number, ArrayBufferDeclaration* declarations,
								uint declarations_number, Material* material, GLenum mode, Vector3* position, GLuint* textures, uint textures_count, uint flags);
void scene_draw(Scene* scene, Vector3 clear_color);
void scene_handle_events(Scene* scene, GLFWwindow* window);

WindowID window_create(Scene* scene, float width, float height, float* position, char* title);
void window_switch_to(Scene* scene, WindowID id);
void window_set_on_close(Scene* scene, WindowID id, void (*on_close)());

Text* text_create(char* string, float size, Vector2 position, Vector3 color);

void text_set_color(Text* text, Vector3 color);
void text_set_angle(Text* text, float angle);

Widget* widget_label_create(WindowID window_id, Scene* scene, Widget* parent, char* text, float text_size, float margin, Vector3 color, Layout layout);
Widget* widget_button_create(WindowID window_id, Scene* scene, Widget* parent, char* text, float text_size, float margin, float padding, Layout layout);

void widget_set_on_hover(Widget* widget, EventCallback on_hover);
void widget_set_on_click(Widget* widget, EventCallback on_click);
void widget_set_on_click_up(Widget* widget, EventCallback on_click_up);

#endif // RENDER_INTERNAL
#endif // RENDER_HEADER
