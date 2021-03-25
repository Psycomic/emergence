#ifndef RENDER_HEADER
#define RENDER_HEADER

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "linear_algebra.h"
#include "images.h"
#include "drawable.h"
#include "text.h"
#include "camera.h"

#define WIDGET_STATE_HOVERED	(1 << 0)
#define WIDGET_STATE_CLICKED	(1 << 1)

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

struct Widget;
typedef void (*EventCallback)(struct Widget*, Event*);

// Parent "class" of every UI widget
typedef struct Widget {
	Vector2 position;

	struct Widget* parent; 		// NULL if directly on a window

	float height;				// Used to align the widgets correcly
	float margin;				// Distance between the widget and the previous
								// widget
	enum {
		WIDGET_TYPE_LABEL = 0,
		WIDGET_TYPE_BUTTON
	} type;

	EventCallback on_click_up; // Callbacks for different events
							   // ingored if NULL
	Layout layout;

	uint32_t state;
	uint index;
} Widget;

typedef struct {
	Widget header; // Support for "object-oriented" inheritance

	Vector3 color;
	Text* text;
} Label;

// Simple clickable button
typedef struct {
	Widget header;

	Text* text;
	Drawable* button_background;
	float depth;

	float padding;  // Distance between the text and
					// the button's edges
} Button;

struct Scene;

// Window UI : a width, height, mininum width and transparency.
// every drawable has its own container, differing from the original scene.
typedef struct Window {
	struct Scene* parent;

	struct Window* next;
	struct Window* previous;

	Vector2 position;   // Position used to calculate vertex translation
						// for every change of position
	void (*on_close)();	// Called when the window is closed

	float width, height; // Window dimensions
	float min_width, min_height; // How much you can resize the window

	float depth;
	float transparency;

	float pack_last_size;		// Position of last widget added to the window
	uint widgets_count;
	Layout layout;

	BatchDrawable* background_drawable; // Drawable element for the window's frame
	BatchDrawable* text_bar_drawable;
	Widget* widgets[64];

	Text* title;
} Window;

// What you see on the screen. It is basically the container of every
// graphical aspect of the game : the 3D view and the 2D UI.
typedef struct Scene {
	StateContext gl;

	DynamicArray drawables;	// Drawables array
	Window* last_window;
	uint64_t windows_count;

	Camera camera;

	Batch windows_batch;		// Respectively the batch to draw
								// window backgrounds, the batch to
	Batch text_batch;			// draw text and the batch to draw the
	Batch window_text_bar_batch; // title bar of the windows

	GLFWwindow* context;

	uint glfw_last_character;
	uint flags;
	Window* selected_window;
} Scene;

extern const Vector3 white;
extern const Vector3 red;
extern const Vector3 blue;
extern const Vector3 black;
extern const Vector3 green;

int initialize_everything();

GLFWwindow* scene_context(Scene* scene);
void scene_quit(Scene *scene);
int scene_should_close(Scene* scene);
// Create a scene instance
Scene* scene_create(Vector3 camera_position, int width, int height, const char* title);
Drawable* scene_create_drawable(Scene* scene, unsigned short* elements, uint elements_number,
								ArrayBufferDeclaration* declarations, uint declarations_number,
								Material* material, GLenum mode, Vector3* position, GLuint* textures,
								uint textures_count, uint flags);

// The main function of the whole renderer logic
void scene_draw(Scene* scene, Vector3 clear_color);
void scene_handle_events(Scene* scene, GLFWwindow* window);
void scene_set_size(Scene* scene, float width, float height);
void scene_update_window_depths(Scene* scene);

Window* window_create(Scene* scene, float width, float height, float* position, char* title);
void window_set_on_close(Window* window, void (*on_close)());
void window_set_position(Window* window, float x, float y);
void window_set_size(Window* window, float width, float height);
void window_set_transparency(Window* window, float transparency);
void window_destroy(Scene* scene, Window* window);

void widget_draw(Window* window, Widget* widget, Mat4 view_position_matrix);

void widget_set_transparency(Widget* widget, float transparency);
void widget_set_depth(Widget* widget, float depth);

float widget_get_margin_height(Widget* widget);
Vector2 widget_get_real_position(Widget* widget, Window* window);

GLboolean widget_is_colliding(Widget* widget, Window* window, float x, float y);

BOOL widget_state(Widget* widget, uint state);

void widget_on_click_up(Widget* widget, Event* evt);
void widget_set_on_click_up(Widget* widget, EventCallback on_click_up);
void widget_update_position(Widget* widget, Window* window);

void widget_destroy(Widget* widget);

Widget* widget_label_create(Window* window_id, Scene* scene, Widget* parent, char* text,
							float text_size, float margin, Vector3 color, Layout layout);

Widget* widget_button_create(Window* window_id, Scene* scene, Widget* parent, char* text,
							 float text_size, float margin, float padding, Layout layout);

#endif // RENDER_HEADER
