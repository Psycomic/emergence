#define RENDER_INTERNAL

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "misc.h"
#include "linear_algebra.h"
#include "stack_allocator.h"
#include "render.h"
#include "drawable.h"
#include "text.h"
#include "batch_renderer.h"
#include "camera.h"

#define SCENE_DEFAULT_CAPACITY 10
#define CAMERA_SPEED 0.1f
#define MOUSE_SENSIBILLITY 0.01f

#define WIDGET_STATE_HOVERED	(1 << 0)
#define WIDGET_STATE_CLICKED	(1 << 1)

const Vector3 white = { { 1, 1, 1 } };
const Vector3 red = { { 1, 0, 0 } };
const Vector3 blue = { { 0, 0, 1 } };
const Vector3 black = { { 0, 0, 0 } };
const Vector3 green = { { 0, 1, 0 } };

typedef void (*EventCallback)(void*, void*);

typedef struct Widget {
	Vector2 position;

	struct Widget* parent;

	float height;
	float margin;

	enum {
		WIDGET_TYPE_LABEL = 0,
		WIDGET_TYPE_BUTTON
	} type;

	EventCallback on_hover;
	EventCallback on_click;
	EventCallback on_click_up;

	Layout layout;

	uint32_t state;
	uint index;
} Widget;

typedef struct {
	Widget header;

	Vector3 color;
	Text* text;
} Label;

typedef struct {
	Widget header;

	Text* text;
	Drawable* button_background;

	float padding;
} Button;

// Window UI : a width, height, mininum width and transparency.
// every drawable has its own container, differing from the original scene.
typedef struct {
	Vector2 position;
	void (*on_close)();

	float width;
	float height;

	float min_width;
	float min_height;

	float depth;

	float transparency;
	float pack_last_size;

	uint widgets_count;

	Layout layout;

	Text* title;

// TODO: Change everything to batch drawable.
	BatchDrawable background_drawable;
	Drawable* text_bar_drawable;
	Widget* widgets[64];
} Window;

#define SCENE_MAX_WINDOWS 4

#define SCENE_GUI_MODE			(1 << 0)
#define SCENE_EVENT_MOUSE_RIGHT	(1 << 1)
#define SCENE_EVENT_MOUSE_LEFT	(1 << 2)
#define SCENE_EVENT_QUIT		(1 << 3)

// What you see on the screen. It is basically the container of every
// graphical aspect of the game : the 3D view and the 2D UI.
typedef struct {
	DynamicArray windows;	// Window array
	DynamicArray drawables;	// Drawables array

	Camera camera;

	Batch windows_batch;
	Batch text_batch;

	GLFWwindow* context;

	uint glfw_last_character;
	uint flags;
	uint selected_window;
} Scene;

static GLuint ui_background_shader;
static GLuint ui_text_shader;
static GLuint ui_button_shader;
static GLuint color_shader;
static GLuint axis_shader;

static GLuint monospaced_font_texture;

static Material* axis_material = NULL;

static char* color_uniforms[] = {
	"color",
	"model_position",
	"transparency",
};

static char* ui_button_uniforms[] = {
	"model_position",
	"transparency",
	"max_width",
	"max_height",
	"anchor_position",
	"border_size",
	"width",
	"height",
	"color"
};

static char* axis_uniforms[] = {
	"color", "transparency"
};

static Vector3 button_background_color = { { 0.5f, 0.5f, 0.5f } };
static Vector3 button_background_hover_color = { { 0.9f, 0.9f, 1.f } };
static Vector3 button_background_click_color = { { 0.2f, 0.2f, 0.4f } };

static Vector3 button_text_color = { { 0.f, 0.f, 0.f } };
static Vector3 button_text_hover_color = { { 0.2f, 0.2f, 0.4f } };
static Vector3 button_text_click_color = { { 0.8f, 0.8f, 0.9f } };

static Drawable* axis_drawable;

double last_xpos = -1.0, last_ypos = -1.0;

void render_initialize(void);

float random_float(void);

void window_draw(Window* window, Mat4 view_position_matrix);

void window_set_position(Window* window, float x, float y);
void window_set_size(Window* window, float width, float height);
void window_set_transparency(Window* window, float transparency);
void window_destroy(Scene* scene, WindowID id);

void widget_draw(Window* window, Widget* widget, Mat4 view_position_matrix);
Vector2 widget_get_real_position(Widget* widget, Window* window);
void widget_set_transparency(Widget* widget, float transparency);
float widget_get_margin_height(Widget* widget);

GLboolean widget_is_colliding(Widget* widget, Window* window, float x, float y);

void widget_on_hover(Widget* widget, Event* evt);
void widget_on_click(Widget* widget, Event* evt);
void widget_on_click_up(Widget* widget, Event* evt);

void widget_destroy(Widget* widget);

Widget* widget_label_create(WindowID window_id, Scene* scene, Widget* parent, char* text, float text_size, float margin, Vector3 color, Layout layout);
Widget* widget_button_create(WindowID window_id, Scene* scene, Widget* parent, char* text, float text_size, float margin, float padding, Layout layout);

void scene_set_size(Scene* scene, float width, float height);

int initialize_everything() {
	glewExperimental = 1;

	if (!glfwInit()) {
		fprintf(stderr, "GLFW not initialized correctly !\n");
		return -1;
	}

	glfwWindowHint(GLFW_SAMPLES, 4); // 4x antialiasing
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); // We want OpenGL 3.3
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

	return 0;
}

void character_callback(GLFWwindow* window, unsigned int codepoint) {
	Scene* scene = glfwGetWindowUserPointer(window);

	scene->glfw_last_character = codepoint;
}

void resize_callback(GLFWwindow* window, int width, int height) {
	Scene* scene = glfwGetWindowUserPointer(window);

	scene_set_size(scene, width, height);
}

GLFWwindow* scene_context(Scene* scene) {
	return scene->context;
}

int scene_should_close(Scene* scene) {
	return glfwWindowShouldClose(scene->context) ||
		scene->flags & SCENE_EVENT_QUIT;
}

void scene_quit(Scene* scene) {
	scene->flags |= SCENE_EVENT_QUIT;
}

/* Create a scene instance */
Scene* scene_create(Vector3 camera_position, int width, int height, const char* title) {
	Scene* scene = malloc(sizeof(Scene)); /* Allocating the scene object */
	assert(scene != NULL);

	/* Creating window and OpenGL context */
	scene->context = glfwCreateWindow(width, height, title, NULL, NULL);

	if (scene->context == NULL) {
		fprintf(stderr, "Failed to open a window\n");
		return NULL;
	}

	glfwSetCharCallback(scene->context, character_callback);
	glfwSetWindowSizeCallback(scene->context, resize_callback);

	glfwSetWindowUserPointer(scene->context, scene);
	glfwGetWindowSize(scene->context, &width, &height);

	glfwMakeContextCurrent(scene->context); /* Make window the current context */

	if (glewInit() != GLEW_OK) {
		fprintf(stderr, "Failed to initialize GLEW\n");
		return NULL;
	}

	glfwSwapInterval(1);			/* Disable double buffering */

	// OpenGL settings
	glEnable(GL_BLEND);			/* Enable blend */
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); /* Set blend func */

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); /* Texture parameters */
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glViewport(0, 0, width, height); /* Viewport size */

	render_initialize();		/* Initialize scene */

	camera_init(&scene->camera, camera_position, 1e+4f, 1e-4f, 120.f, width, height); /* Initalize player camera */

	scene->flags = 0x0;
	scene->selected_window = 0;
	scene->glfw_last_character = 0;

	DYNAMIC_ARRAY_CREATE(&scene->drawables, Drawable*);
	DYNAMIC_ARRAY_CREATE(&scene->windows, Window);

	Material* window_batch_material = material_create(ui_background_shader, NULL, 0);
	uint64_t windows_attributes_sizes[] = {
		3, 1 // Position, transparency
	};

	batch_init(&scene->windows_batch, window_batch_material, sizeof(float) * 512, sizeof(uint32_t) * 512,
			   windows_attributes_sizes, ARRAY_SIZE(windows_attributes_sizes));

	Material* text_batch_material = material_create(ui_text_shader, NULL, 0);
	uint64_t text_attributes_sizes[] = {
		2, 2, 1 // Position, texture postion, transparency
	};

	batch_init(&scene->text_batch, text_batch_material, sizeof(float) * 2048, sizeof(uint32_t) * 2048,
			   text_attributes_sizes, ARRAY_SIZE(text_attributes_sizes));

	return scene;
}

/* Change size of scene viewport */
void scene_set_size(Scene* scene, float width, float height) {
	scene->camera.width = width;
	scene->camera.height = height;

	mat4_create_perspective(scene->camera.perspective_matrix, 1000.f, 0.1f, 90.f, (float) scene->camera.width / scene->camera.height);

	float half_width = (float)width / 2,
		half_height = (float)height / 2;

	mat4_create_orthogonal(scene->camera.ortho_matrix, -half_width, half_width, -half_height, half_height, -2.f, 2.f);

	glViewport(0, 0, scene->camera.width, scene->camera.height);
}

/* The main function of the whole renderer logic */
void scene_draw(Scene* scene, Vector3 clear_color) {
	glClearColor(clear_color.x, clear_color.y, clear_color.z, 0.01f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	Mat4 camera_final_matrix;
	camera_get_final_matrix(&scene->camera, camera_final_matrix);

	for (uint i = 0; i < scene->drawables.size; i++) {
		Drawable* drawable = *(Drawable**)dynamic_array_at(&scene->drawables, i);
		uint flags = drawable_flags(drawable);

		Mat4 position_matrix;
		mat4_create_translation(position_matrix, drawable_position(drawable));

		// Drawing the elements added to the scene
		if (flags & DRAWABLE_NO_DEPTH_TEST)
			glDisable(GL_DEPTH_TEST);
		else
			glEnable(GL_DEPTH_TEST);

		material_use(drawable_material(drawable), position_matrix, camera_final_matrix);
		drawable_draw(drawable);

		if (flags & DRAWABLE_SHOW_AXIS) {
			material_use(drawable_material(axis_drawable), position_matrix, camera_final_matrix);
			drawable_draw(axis_drawable);
		}
	}

	if (scene->flags & SCENE_GUI_MODE) {
		glEnable(GL_DEPTH_TEST);

		batch_draw(&scene->windows_batch, scene->camera.ortho_matrix);

		for (uint i = 0; i < scene->windows.size; i++) {
			Window* win = dynamic_array_at(&scene->windows, i);
			window_draw(win, scene->camera.ortho_matrix);
		}

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, monospaced_font_texture);
		batch_draw(&scene->text_batch, scene->camera.ortho_matrix);
	}
}

/* Set windows' depth correspondingly to their priority rank */
void scene_update_window_depths(Scene* scene) {
	for (uint i = 0; i < scene->windows.size; i++) {
		size_t index = ((size_t)scene->selected_window + i) % scene->windows.size;
		Window* window = dynamic_array_at(&scene->windows, index);
		window->depth = -((float)i / scene->windows.size + 1.f);

		printf("Window %lu has depth %.2f\n", index, window->depth);

		for (uint j = 0; j < window->background_drawable.vertices_count; j++) {
			float* vertex = (float*)window->background_drawable.vertices + scene->windows_batch.vertex_size * j;
			vertex[2] = window->depth;
		}
	}
}

/* Add drawable to scene */
Drawable* scene_create_drawable(Scene* scene, unsigned short* elements, uint elements_number, ArrayBufferDeclaration* declarations, uint declarations_count, Material* material, GLenum mode, Vector3* position, GLuint* textures, uint textures_count, uint flags) {
	Drawable** drawable_pos = dynamic_array_push_back(&scene->drawables);

	*drawable_pos = drawable_allocate(declarations_count);

	drawable_init(*drawable_pos, elements, elements_number, declarations, declarations_count, material, mode, position, textures, textures_count, flags);
	return *drawable_pos;
}

/* Handle every event happening in the scene. TODO: Cleanup */
void scene_handle_events(Scene* scene, GLFWwindow* window) {
	if (scene->glfw_last_character == 'e')
		scene->flags ^= SCENE_GUI_MODE;

	double xpos, ypos;
	glfwGetCursorPos(window, &xpos, &ypos);

	int width, height;
	glfwGetWindowSize(window, &width, &height);

	if ((scene->flags & SCENE_GUI_MODE) && scene->windows.size > 0) {
		for (uint i = 0; i < scene->windows.size; i++) {
			if (i == scene->selected_window)
				window_set_transparency(dynamic_array_at(&scene->windows, i), 1.f);
			else
				window_set_transparency(dynamic_array_at(&scene->windows, i), 0.3f);
		}

		float screen_x = (float)xpos - (width / 2.f),
			screen_y = -(float)ypos + (height / 2.f);

		for (uint i = 0; i < DYNAMIC_ARRAY_AT(&scene->windows, scene->selected_window, Window)->widgets_count; i++) {
			Widget* widget = DYNAMIC_ARRAY_AT(&scene->windows, scene->selected_window, Window)->widgets[i];

			if (widget_is_colliding(widget, dynamic_array_at(&scene->windows, scene->selected_window), screen_x, screen_y)) {
				Event evt;
				evt.mouse_info.screen_x = screen_x;
				evt.mouse_info.screen_y = screen_y;

				widget_on_hover(widget, &evt);
				widget->state |= WIDGET_STATE_HOVERED;

				if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
					widget_on_click(widget, &evt);
					widget->state |= WIDGET_STATE_CLICKED;
				}
				else {
					if (widget->state & WIDGET_STATE_CLICKED)
						widget_on_click_up(widget, &evt);

					widget->state &= ~WIDGET_STATE_CLICKED;
				}
			}
			else {
				widget->state &= ~(WIDGET_STATE_HOVERED | WIDGET_STATE_CLICKED);
			}
		}

		switch (scene->glfw_last_character) {
		case ' ':
			scene->selected_window = ((size_t)scene->selected_window + 1) % scene->windows.size;
			scene_update_window_depths(scene);
			break;
		case 'c':
			window_destroy(scene, scene->selected_window);
			break;
		}

		if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL)) {
			Window* selected_window = dynamic_array_at(&scene->windows, scene->selected_window);

			window_set_position(selected_window, screen_x - selected_window->width / 2, screen_y - selected_window->height / 2);
		}
		if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT)) {
			Window* selected_window = dynamic_array_at(&scene->windows, scene->selected_window);

			float new_width = screen_x - selected_window->position.x;
			float new_height = screen_y - selected_window->position.y;

			window_set_size(selected_window, new_width, new_height);
		}
	}
	else {
		Vector3 camera_direction;
		camera_get_direction(&scene->camera, &camera_direction, CAMERA_SPEED);

		// Keyboard input handling
		if (glfwGetKey(window, GLFW_KEY_W))
			camera_translate(&scene->camera, camera_direction);

		if (glfwGetKey(window, GLFW_KEY_S)) {
			vector3_neg(&camera_direction);
			camera_translate(&scene->camera, camera_direction);
		}

		if (last_xpos == -1.0 || last_ypos == -1.0) {
			last_xpos = xpos;
			last_ypos = ypos;
		}

		// Rotate the camera proporitionaly to the mouse position
		camera_rotate(&scene->camera, ((float)ypos - last_ypos) * MOUSE_SENSIBILLITY, -((float)xpos - last_xpos) * MOUSE_SENSIBILLITY);
	}

	last_xpos = xpos;
	last_ypos = ypos;

	scene->glfw_last_character = 0;
}

void window_update(Window* window) {
	Vector2 text_position = window->position;
	text_position.y += 0.1f * window->height;
	text_position.x += 6.f;

	text_set_position(window->title, text_position);
}

void window_set_position(Window* window, float x, float y) {
	Vector2 translation;
	Vector2 new_position = { { x, y } };

	vector2_sub(&translation, new_position, window->position);

	// Set position of the window background
	for (uint i = 0; i < window->background_drawable.vertices_count; i++) {
		BatchDrawable* drawable = &window->background_drawable;

		Vector2* vertex = (Vector2*)((float*)drawable->vertices + drawable->batch->vertex_size * i);
		vector2_add(vertex, *((Vector2*)vertex), translation);
	}

	window->position.x = x;
	window->position.y = y;

	window_update(window);
	batch_drawable_update(&window->background_drawable);

	material_set_uniform_vec2(drawable_material(window->text_bar_drawable), 1, window->position);
	drawable_update(window->text_bar_drawable);
}

void window_set_size(Window* window, float width, float height) {
	window->width = max(width, window->min_width);
	window->height = max(height, window->min_height);

#ifdef _DEBUG
	printf("Set window size to %.2f %.2f\n", window->width, window->height);
#endif

	float* text_bar_vertices = drawable_buffer_data(window->text_bar_drawable, 0);
	text_bar_vertices[0] = text_get_size(window->title) + 6.f; text_bar_vertices[1] = window->height;
	text_bar_vertices[2] = text_get_size(window->title) + 6.f; text_bar_vertices[3] = 0.f;

	drawable_update(window->text_bar_drawable);
	window_update(window);

	rectangle_vertices_set((float*)window->background_drawable.vertices,
						   window->width, window->height, 4,
						   window->position.x, window->position.y);

	batch_drawable_update(&window->background_drawable);
}

void window_set_transparency(Window* window, float transparency) {
	window->transparency = transparency;

	for (uint i = 0; i < window->background_drawable.vertices_count; i++) {
		float* vertex = (float*)window->background_drawable.vertices + window->background_drawable.batch->vertex_size * i;
		vertex[3] = transparency;
	}

	batch_drawable_update(&window->background_drawable);

	for (uint i = 0; i < window->widgets_count; i++)
		widget_set_transparency(window->widgets[i], transparency);
}

static uint32_t rectangle_elements[] = { 0, 1, 2, 1, 3, 2 };

WindowID window_create(Scene* scene, float width, float height, float* position, char* title) {
	Window* window = dynamic_array_push_back(&scene->windows);

	window->min_width = 200.f;
	window->min_height = 100.f;
	window->depth = -1.f;
	window->pack_last_size = 0.f;
	window->widgets_count = 0;
	window->layout = LAYOUT_PACK;
	window->on_close = NULL;

	Vector3 title_color = { { 0.6f, 0.6f, 0.6f } };
	window->title = text_create(&scene->text_batch, title, 15.f, window->position, title_color);

	float* background_drawable_vertices = malloc(sizeof(float) * 4 * 5);
	batch_drawable_init(&scene->windows_batch, &window->background_drawable, background_drawable_vertices, 4,
						rectangle_elements, ARRAY_SIZE(rectangle_elements));

	Drawable* text_bar_drawable = drawable_allocate(1);
	assert(text_bar_drawable != NULL);

	float* text_bar_vertices = malloc(sizeof(float) * 4);
	ArrayBufferDeclaration text_bar_declarations[] = {
		{text_bar_vertices, sizeof(float) * 4, 2, 0, GL_DYNAMIC_DRAW}
	};
	static Vector3 bar_color = {
		{ 0.7f, 0.7f, 0.7f }
	};

	drawable_init(text_bar_drawable, NULL, 2, text_bar_declarations, 1,
				  material_create(color_shader, color_uniforms, ARRAY_SIZE(color_uniforms)),
				  GL_LINES, NULL, NULL, 0, 0x0);

	window->text_bar_drawable = text_bar_drawable;

	material_set_uniform_vec3(drawable_material(window->text_bar_drawable), 0, bar_color);

	window_set_position(window, position[0], position[1]);
	window_set_size(window, width, height);
	window_set_transparency(window, 0.9f);

	scene->selected_window = scene->windows.size - 1;
	scene_update_window_depths(scene);

	return scene->windows.size - 1;
}

void window_set_on_close(Scene* scene, WindowID id, void (*on_close)()) {
	((Window*)dynamic_array_at(&scene->windows, id))->on_close = on_close;
}

Vector2 window_get_anchor(Window* window) {
	Vector2 window_anchor = {
		{
			window->position.x + 30.f,
			window->position.y + window->height - 30.f
		}
	};

	return window_anchor;
}

void window_draw(Window* window, Mat4 view_position_matrix) {
	assert(window->layout == LAYOUT_PACK);

	Drawable* text_bar_drawable = window->text_bar_drawable;

	material_use(drawable_material(text_bar_drawable), NULL, view_position_matrix);
	drawable_draw(text_bar_drawable);

	if (window->layout == LAYOUT_PACK) {
		for (uint i = 0; i < window->widgets_count; i++)
			widget_draw(window, window->widgets[i], view_position_matrix);
	}
}

Vector2 window_get_max_position(Window* window) {
	Vector2 window_anchor = window_get_anchor(window);

	Vector2 max_positon;
	max_positon.x = window->width - (window_anchor.x - window->position.x);
	max_positon.y = window_anchor.y - window->position.y;

	return max_positon;
}

void window_destroy(Scene* scene, WindowID id) {
	if (scene->windows.size <= 1) {
		float position[] = {
			0.f, 0.f
		};

		WindowID error_window = window_create(scene, 400.f, 100.f, position, "ERROR");
		widget_label_create(error_window, scene, NULL, "ATTEMPTED TO DELETE\nSOLE WINDOW", 14.f, 5.f, red, LAYOUT_PACK);
	}
	else {
		Window* window = dynamic_array_at(&scene->windows, id);

		if (window->on_close != NULL)
			window->on_close();

		free(drawable_buffer_data(window->text_bar_drawable, 0));

		// Freeing memory resources
		drawable_destroy(window->text_bar_drawable);

		for (uint i = 0; i < window->widgets_count; i++)
			widget_destroy(window->widgets[i]);

		text_destroy(window->title);

		// Reajusting the windows array
		dynamic_array_remove(&scene->windows, id);

		scene->selected_window = scene->selected_window % scene->windows.size;
	}
}

void window_switch_to(Scene* scene, WindowID id) {
	scene->selected_window = id;
}

Vector2 widget_get_real_position(Widget* widget, Window* window) {
	Vector2 widget_position = {
		{
			widget->position.x + widget->margin,
			widget->position.y
		}
	};

	if (widget->parent == NULL) {
		widget_position.y -= widget->margin;
		vector2_add(&widget_position, widget_position, window_get_anchor(window));

		return widget_position;
	}
	else {
		Vector2 real_position, parent_position = widget_get_real_position(widget->parent, window);

		vector2_add(&real_position, widget_position, parent_position);

		return real_position;
	}
}

float widget_get_height(Widget* widget) {
	switch (widget->type) {
	case WIDGET_TYPE_LABEL:
		return text_get_height(((Label*)widget)->text);
		break;
	case WIDGET_TYPE_BUTTON:
		return text_get_height(((Button*)widget)->text) + ((Button*)widget)->padding * 2;
		break;
	default:
		return 0.f;
	}
}

float widget_get_width(Widget* widget) {
	switch (widget->type) {
	case WIDGET_TYPE_LABEL:
		return text_get_width(((Label*)widget)->text);
		break;
	case WIDGET_TYPE_BUTTON:
		return text_get_width(((Button*)widget)->text) + ((Button*)widget)->padding * 2;
		break;
	default:
		return 0.f;
	}
}

float widget_get_margin_height(Widget* widget) {
	float padding = widget->type == WIDGET_TYPE_BUTTON ? ((Button*)widget)->padding * 2 : 0.f;

	return widget->height + padding + widget->margin * 2;
}

float widget_get_margin_width(Widget* widget) {
	return widget_get_width(widget) + widget->margin * 2;
}

void widget_get_hitbox(Widget* widget, Vector3 real_position, float* min_x, float* min_y, float* max_x, float* max_y) {
	*min_x = real_position.x;
	*max_x = *min_x + widget_get_width(widget);
	*min_y = real_position.y - widget_get_height(widget);
	*max_y = real_position.y;
}

void widget_init(Widget* widget, Window* window, Widget* parent, float margin, Layout layout) {
	widget->parent = parent;
	widget->layout = layout;
	widget->margin = margin;

	widget->on_hover = NULL;
	widget->on_click = NULL;
	widget->on_click_up = NULL;

	widget->state = 0x0;

	widget->position.x = 0.f;
	widget->position.y = (parent ? -widget_get_margin_height(parent) : -window->pack_last_size);

	widget->index = window->widgets_count;

	for (Widget* ptr = widget; ptr != NULL; ptr = ptr->parent) {
		if (ptr->parent)
			ptr->parent->height += widget_get_margin_height(widget);
		else
			window->pack_last_size += widget_get_margin_height(widget);

		for (uint i = 0; i < window->widgets_count; i++) {
			if (window->widgets[i]->parent == ptr->parent && window->widgets[i]->index > ptr->index) {
				window->widgets[i]->position.y -= widget_get_margin_height(widget);
			}
		}
	}

	window->widgets[window->widgets_count++] = widget;
}

GLboolean widget_is_colliding(Widget* widget, Window* window, float x, float y) {
	Vector2 real_position = widget_get_real_position(widget, window);

	if (x <= window->position.x || x >= window->position.x + window->width ||
		y <= window->position.y || y >= window->position.y + window->height) {
		return GL_FALSE;
	}

	float min_x = real_position.x,
		min_y = real_position.y - widget_get_height(widget),
		max_x = min_x + widget_get_width(widget),
		max_y = real_position.y;

	return (min_x <= x && max_x >= x && min_y <= y && max_y >= y);
}

void widget_on_hover(Widget* widget, Event* evt) {
	if (widget->on_hover != NULL)
		widget->on_hover(widget, evt);
}

void widget_on_click(Widget* widget, Event* evt) {
	if (widget->on_click != NULL)
		widget->on_click(widget, evt);
}

void widget_on_click_up(Widget* widget, Event* evt) {
	if (widget->on_click_up != NULL)
		widget->on_click_up(widget, evt);
}

void widget_set_on_hover(Widget* widget, EventCallback on_hover) {
	widget->on_hover = on_hover;
}

void widget_set_on_click(Widget* widget, EventCallback on_click) {
	widget->on_click = on_click;
}

void widget_set_on_click_up(Widget* widget, EventCallback on_click_up) {
	widget->on_click_up = on_click_up;
}

Widget* widget_label_create(WindowID window_id, Scene* scene, Widget* parent, char* text,
							float text_size, float margin, Vector3 color, Layout layout) {
	Window* window = dynamic_array_at(&scene->windows, window_id);
	Vector2 text_position = { { 0.f, 0.f } };
	Label* label = malloc(sizeof(Label));

	assert(label != NULL);

	label->header.type = WIDGET_TYPE_LABEL;
	label->color = color;
	label->text = text_create(&scene->text_batch, text, text_size, text_position, color);

	label->header.height = text_get_height(label->text);	// Setting widget height

	widget_init(SUPER(label), window, parent, margin, layout);		// Intializing the widget

	return SUPER(label);
}

Widget* widget_button_create(WindowID window_id, Scene* scene, Widget* parent, char* text,
							 float text_size, float margin, float padding, Layout layout) {
	Window* window = dynamic_array_at(&scene->windows, window_id);

	static const float border_size = 1.f;

	Vector2 text_position = { { 0.f, 0.f } };

	Button* button = malloc(sizeof(Button));	// Allocating the button widget
	button->header.type = WIDGET_TYPE_BUTTON;	// Setting button type, padding and hover function

	button->padding = padding + border_size * 2;
	button->text = text_create(&scene->text_batch, text, text_size, text_position, button_text_color);	// Initializing the button's text

	float text_width = text_get_width(button->text),
		text_height = text_get_height(button->text);

	button->header.height = text_height;	// Setting widget height

	button->button_background = drawable_allocate(1);	// Background of the button

	Material* button_material = material_create(ui_button_shader, ui_button_uniforms, ARRAY_SIZE(ui_button_uniforms));	// Background's material

	drawable_rectangle_init(button->button_background,	// Initializing the background drawable
		text_width + button->padding * 2.f,
		text_height + button->padding * 2.f,
		button_material, GL_TRIANGLES, NULL, 0x0);

	material_set_uniform_float(button_material, 5, border_size);			// Border size
	material_set_uniform_vec3(button_material, 8, button_background_color);	// Color

	widget_init(SUPER(button), window, parent, margin, layout);	// Intializing the widget

	return SUPER(button);
}

void widget_label_draw(Window* window, void* widget, Vector2 position, Mat4 view_position_matrix) {
	Label* label_widget = widget;
	text_set_position(label_widget->text, position);	// Setting the text's position
}

void widget_button_draw(Window* window, void* widget, Vector2 position, Mat4 view_position_matrix) {
	Button* button = widget;

	Vector2 background_position = position;		// Calculating the button's background position
	background_position.y -= button->header.height + button->padding * 2;

	Vector2 window_max_position = window_get_max_position(window);	// Max position before fading

	Material* button_material = drawable_material(button->button_background);

	material_set_uniform_vec2(button_material, 0, background_position);			// Position
	material_set_uniform_vec2(button_material, 4, window_get_anchor(window));	// Anchor

	material_set_uniform_float(button_material, 2, window_max_position.x);		// Max Width
	material_set_uniform_float(button_material, 3, window_max_position.y);		// Max Height

	material_set_uniform_float(button_material, 6, widget_get_width(SUPER(button)));	// Width
	material_set_uniform_float(button_material, 7, widget_get_height(SUPER(button)));	// Height

	if (button->header.state & WIDGET_STATE_CLICKED) {
		text_set_color(&button->text, button_text_click_color);
		material_set_uniform_vec3(button_material, 8, button_background_click_color);
	}
	else if (button->header.state & WIDGET_STATE_HOVERED) {		// Setting the background color
		text_set_color(&button->text, button_text_hover_color);
		material_set_uniform_vec3(button_material, 8, button_background_hover_color);
	}
	else {
		text_set_color(&button->text, button_text_color);
		material_set_uniform_vec3(button_material, 8, button_background_color);
	}

	material_use(button_material, NULL, view_position_matrix);	// Drawing the background using the material
	drawable_draw(button->button_background);

	position.x += button->padding;	// Drawing the text a little bit below
	position.y -= button->padding;

	text_set_position(button->text, position);
}

void widget_label_set_transparency(void* widget, float transparency) {
	text_set_transparency(((Label*)widget)->text, transparency);
}

void widget_button_set_transparency(void* widget, float transparency) {
	Button* button = (Button*)widget;

	text_set_transparency(button->text, transparency);
	material_set_uniform_float(drawable_material(button->button_background), 1, transparency);
}

void widget_label_set_text(void* widget, const char* text) { // TODO: Add support for dynamic text changing
	Label* label = widget;

//	text_set_text(...)
}

void widget_label_destroy(void* widget) {
	Label* label = (Label*)widget;

	text_destroy(label->text);
	free(label);
}

void widget_button_destroy(void* widget) {
	Button* button = (Button*)widget;

	text_destroy(button->text);

	free(drawable_buffer_data(button->button_background, 0));

	drawable_destroy(button->button_background);
	free(button);
}

static void (*widget_draw_vtable[])(Window*, void*, Vector2, Mat4) = {
	[WIDGET_TYPE_LABEL] = &widget_label_draw,
	[WIDGET_TYPE_BUTTON] = &widget_button_draw,
};

static void (*widget_set_transparency_vtable[])(void*, float) = {
	[WIDGET_TYPE_LABEL] = &widget_label_set_transparency,
	[WIDGET_TYPE_BUTTON] = &widget_button_set_transparency,
};

static void (*widget_destroy_vtable[])(void*) = {
	[WIDGET_TYPE_LABEL] = &widget_label_destroy,
	[WIDGET_TYPE_BUTTON] = &widget_button_destroy
};

void widget_draw(Window* window, Widget* widget, Mat4 view_position_matrix) {
	Vector2 real_position = widget_get_real_position(widget, window);

	widget_draw_vtable[widget->type](window, widget, real_position, view_position_matrix);
}

void widget_set_transparency(Widget* widget, float transparency) {
	widget_set_transparency_vtable[widget->type](widget, transparency);
}

void widget_destroy(Widget* widget) {
	widget_destroy_vtable[widget->type](widget);
}

void render_initialize(void) {
	// Initializing the drawable axis

	static Vector3 axis[] = {
		{ { 0.f, 0.f, 0.f } },
		{ { 1.f, 0.f, 0.f } },
		{ { 0.f, 1.f, 0.f } },
		{ { 0.f, 0.f, 1.f } }
	};

	static unsigned short axis_elements[] = {
		0, 1, 0, 2, 0, 3
	};

	static ArrayBufferDeclaration axis_buffers[] = {
		{axis, sizeof(axis), 3, 0, GL_STATIC_DRAW}
	};

	static Vector3 axis_position = { { 0.f, 0.f, 0.f } };

	axis_shader = shader_create("./shaders/vertex_uniform_color.glsl", "./shaders/fragment_uniform_color.glsl");
	ui_background_shader = shader_create("./shaders/vertex_batch_shader.glsl", "./shaders/fragment_batch_shader.glsl");
	ui_text_shader = shader_create("./shaders/vertex_ui_text.glsl", "./shaders/fragment_ui_text.glsl");
	ui_button_shader = shader_create("./shaders/vertex_ui_background.glsl", "./shaders/fragment_ui_button.glsl");
	color_shader = shader_create("./shaders/vertex_ui_background.glsl", "./shaders/fragment_uniform_color.glsl");

	axis_material = material_create(axis_shader, axis_uniforms, ARRAY_SIZE(axis_uniforms));
	material_set_uniform_vec3(axis_material, 0, blue);

	axis_drawable = drawable_allocate(1);
	drawable_init(axis_drawable, axis_elements, 6, axis_buffers, 1, axis_material, GL_LINES, &axis_position, NULL, 0, 0x0);

	Image font_image;
	if (image_load_bmp(&font_image, "./fonts/monospace.bmp") >= 0)
		printf("Success loading lain !\n");
	else
		printf("Error when loading image !\n");

	monospaced_font_texture = texture_create(&font_image, 1);
	image_destroy(&font_image);
}


#undef RENDER_INTERNAL
