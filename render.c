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

#define SCENE_DEFAULT_CAPACITY 10
#define CAMERA_SPEED 0.1f
#define MOUSE_SENSIBILLITY 0.01f

/// A camera, that holds a position and a rotation to render
/// a scene correctly
typedef struct {
	Vector3 position;

	float rx;
	float ry;

	int width, height;
	float fov;

	Mat4 perspective_matrix;	// Not having to recreate one every time
	Mat4 ortho_matrix;
	Mat4 rotation_matrix;		// Needed for the direction
} Camera;

// Abstraction over text. Basically a collection of drawables, each with a 
// glyph texture, to end up with text
typedef struct {
	Vector2 position;
	Vector3 color;

	Drawable* drawable;
	char* string;

	float size;
	float angle;

	GLuint font_texture;
} Text;

#define WIDGET_STATE_HOVERED	(1 << 0)
#define WIDGET_STATE_CLICKED	(1 << 1)

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

	float width;
	float height;
	float min_width;
	float min_height;

	float transparency;
	float pack_last_size;

	uint widgets_count;

	Layout layout;

	Text* title;

	Drawable* drawables[2];
	Widget* widgets[64];
} Window;

#define SCENE_MAX_WINDOWS 4

#define SCENE_GUI_MODE		(1 << 0)

#define SCENE_EVENT_MOUSE_RIGHT	(1 << 0)
#define SCENE_EVENT_MOUSE_LEFT	(1 << 1)

// What you see on the screen. It is basically the container of every
// graphical aspect of the game : the 3D view and the 2D UI.
typedef struct {
	DynamicArray windows;	// Window array
	DynamicArray drawables;	// Drawables array

	Camera camera;

	uint glfw_last_character;
	uint flags;
	uint selected_window;
} Scene;

static GLuint ui_background_shader;
static GLuint ui_texture_shader;
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

static char* ui_texture_uniforms[] = {
	"model_position",
	"color",
	"angle",
	"center_position",
	"transparency",
	"max_width",
	"max_height",
	"anchor_position"
};

static char* ui_background_uniforms[] = {
	"color",
	"transparency",
	"model_position",
};

static char* axis_uniforms[] = {
	"color", "transparency"
};

static Vector3 button_background_color = { 0.5f, 0.5f, 0.5f };
static Vector3 button_background_hover_color = { 0.9f, 0.9f, 1.f };

static Vector3 button_text_color = { 0.f, 0.f, 0.f };
static Vector3 button_text_hover_color = { 0.2f, 0.2f, 0.4f };

static Drawable* axis_drawable;

double last_xpos = -1.0, last_ypos = -1.0;

void render_initialize(void);

float random_float(void);

void window_draw(Window* window, Mat4 view_position_matrix);

void window_set_position(Window* window, float x, float y);
void window_set_size(Window* window, float width, float height);
void window_set_transparency(Window* window, float transparency);
void window_destroy(Scene* scene, uint id);

void material_use(Material* material, float* model_matrix, float* view_position_matrix);

void widget_draw(Window* window, Widget* widget, Mat4 view_position_matrix);
Vector2 widget_get_real_position(Widget* widget, Window* window);
void widget_set_transparency(Widget* widget, float transparency);
float widget_get_margin_height(Widget* widget);
GLboolean widget_is_colliding(Widget* widget, Window* window, float x, float y);
void widget_on_hover(Widget* widget, Event* evt);
void widget_on_click(Widget* widget, Event* evt);
void widget_destroy(Widget* widget);

void scene_set_size(Scene* scene, float width, float height);

void camera_create_rotation_matrix(Mat4 destination, float rx, float ry) {
	Mat4 rotation_matrix_x;
	Mat4 rotation_matrix_y;

	mat4_create_rotation_x(rotation_matrix_x, rx);
	mat4_create_rotation_y(rotation_matrix_y, ry);

	mat4_mat4_mul(destination, rotation_matrix_y, rotation_matrix_x); // First, Y rotation, after X rotation
}

void camera_create_final_matrix(Mat4 destination, Mat4 perspective, Mat4 rotation, Vector3 position) {
	Mat4 translation_matrix;
	Mat4 temporary_matrix;

	vector3_neg(&position);
	mat4_create_translation(translation_matrix, position);

	mat4_mat4_mul(temporary_matrix, translation_matrix, rotation);
	mat4_mat4_mul(destination, temporary_matrix, perspective);
}

void camera_init(Camera* camera, Vector3 position, float far, float near, float fov, int width, int height) {
	camera->position = position;

	camera->rx = 0.f;
	camera->ry = 0.f;

	camera->width = width;
	camera->height = height;
	
	mat4_create_perspective(camera->perspective_matrix, far, near, fov, (float) width / height);
	
	float half_width = (float)width / 2,
		half_height = (float)height / 2;

	mat4_create_orthogonal(camera->ortho_matrix, -half_width, half_width, -half_height, half_height, -2.f, 2.f);
}

void camera_get_final_matrix(Camera* camera, Mat4 final_matrix) {
	camera_create_rotation_matrix(camera->rotation_matrix, camera->rx, camera->ry);
	camera_create_final_matrix(final_matrix, camera->perspective_matrix, camera->rotation_matrix, camera->position);
}

void camera_get_direction(Camera* camera, Vector3* direction, float speed) {
	Vector3 orientation = { 0.f, 0.f, 1.f };

	mat4_vector3_mul(direction, orientation, camera->rotation_matrix);
	vector3_scalar_mul(direction, *direction, speed);
}

void camera_translate(Camera* camera, Vector3 direction) {
	vector3_sub(&camera->position, camera->position, direction);
}

void camera_rotate(Camera* camera, float rx, float ry) {
	camera->rx += rx;
	camera->ry += ry;
}

void character_callback(GLFWwindow* window, unsigned int codepoint) {
	Scene* scene = glfwGetWindowUserPointer(window);

	scene->glfw_last_character = codepoint;
}

void opengl_window_resize_callback(GLFWwindow* window, int width, int height) {
	Scene* scene = glfwGetWindowUserPointer(window);

	scene_set_size(scene, width, height);
}

GLFWwindow* opengl_window_create(uint width, uint height, const char* title) {
	// Initialize GLFW and the opengl context
	glewExperimental = 1;

	if (!glfwInit()) {
		fprintf(stderr, "GLFW not initialized correctly !\n");
		return NULL;
	}

	glfwWindowHint(GLFW_SAMPLES, 4); // 4x antialiasing
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); // We want OpenGL 3.3
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); // We don't want the old OpenGL

	GLFWwindow* window;
	window = glfwCreateWindow(width, height, title, NULL, NULL);

	if (window == NULL) {
		fprintf(stderr, "Failed to open GLFW window.");
		glfwTerminate();

		return NULL;
	}

	glfwSetCharCallback(window, character_callback);
	glfwSetWindowSizeCallback(window, opengl_window_resize_callback);

	glfwMakeContextCurrent(window);

	if (glewInit() != GLEW_OK) {
		fprintf(stderr, "Failed to initialize GLEW\n");
		glfwTerminate();

		return NULL;
	}

	// Disable double buffering
	glfwSwapInterval(1);

	// OpenGL settings
	glEnable(GL_BLEND);

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glViewport(0, 0, width, height);

	// Initialize scene.
	render_initialize();

	return window;
}

Drawable* scene_create_drawable(Scene* scene, unsigned short* elements, uint elements_number, ArrayBufferDeclaration* declarations, uint declarations_count, Material* material, GLenum mode, Vector3* position, GLuint* textures, uint textures_count, uint flags) {
	Drawable** drawable_pos = dynamic_array_push_back(&scene->drawables);

	*drawable_pos = drawable_allocate(declarations_count);

	drawable_init(*drawable_pos, elements, elements_number, declarations, declarations_count, material, mode, position, textures, textures_count, flags);
	return *drawable_pos;
}

void text_set_color(Text* text, Vector3 color) {
	material_set_uniform_vec3(drawable_material(text->drawable), 1, color);
}

void text_set_transparency(Text* text, float transparency) {
	material_set_uniform_float(drawable_material(text->drawable), 4, transparency);
}

void text_set_angle(Text* text, float angle) {
	text->angle = angle;

	material_set_uniform_float(drawable_material(text->drawable), 2, text->angle);
}

void text_set_position(Text* text, Vector2 position) {
	text->position = position;
}

float text_get_width(Text* text) {
	float max_width = 0.f, width = 0.f;
	char* c = text->string;

	do {
		if (*c == '\n' || *c == '\0') {
			if (width > max_width)
				max_width = width;

			width = 0.f;
		}
		else {
			width += text->size;
		}
	} while (*(c++) != '\0');

	return max_width;
}

float text_get_height(Text* text) {
	char* c = text->string;
	uint count = 0;

	do {
		if (*c == '\n')
			count++;
	} while (*(++c) != '\0');

	return text->size * (count + 1);
}

// Every glyph will be a 32 x 32 texture. This means the image is 64 x 416
Text* text_create(char* string, GLuint font_texture, float size, Vector2 position, float angle, Vector3 color) {
	Text* text = malloc(sizeof(Text));
	assert(text != NULL);

	text->string = string;
	text->font_texture = font_texture;
	text->position = position;
	text->size = size;
	text->angle = angle;
	text->color = color;

	uint text_length = strlen(string);

	Material* glyph_material = material_create(ui_texture_shader, ui_texture_uniforms, ARRAY_SIZE(ui_texture_uniforms));
	material_set_uniform_vec3(glyph_material, 1, text->color);
	material_set_uniform_float(glyph_material, 2, text->angle);
	material_set_uniform_float(glyph_material, 4, 1.f);

	float* drawable_uvs = malloc(sizeof(float) * 12 * text_length);
	float* drawable_vertices = malloc(sizeof(float) * 12 * text_length);

	assert(drawable_uvs != NULL && drawable_vertices != NULL);

	const float height = 512,
		half_height = height / 32,
		width = 64,
		half_width = width / 32;

	int y_stride = 0;

	for (uint i = 0, j = 0; j < text_length; j++) {
		if (string[j] == '\n') {
			y_stride--;
			i = 0;
		}
		else {
			assert((string[j] >= 'A' && string[j] <= 'Z') || string[j] == ' ' || string[j] == '\n');

			uint index = string[j] == ' ' ? 31 : string[j] - 'A';

			float x_pos = ((index % 2) * 32) / 64.f,
				y_pos = (1.f - 1 / half_height) - ((index / 2) * 32) / height;

			float uv_down_left[] = { x_pos, (y_pos + (1.f / half_height)) };
			float uv_down_right[] = { x_pos + 1.f / half_width, (y_pos + (1.f / half_height)) };
			float uv_up_left[] = { x_pos, y_pos };
			float uv_up_right[] = { x_pos + 1.f / half_width, y_pos };

			float vertex_up_left[2] = { i * size, -size + y_stride * size };
			float vertex_up_right[2] = { i * size + size, -size + y_stride * size };
			float vertex_down_left[2] = { i * size, y_stride * size };
			float vertex_down_right[2] = { i * size + size, y_stride * size };

#define GET_INDEX(arr, index) arr[j * 12 + index]

			GET_INDEX(drawable_uvs, 0) = uv_up_left[0]; GET_INDEX(drawable_uvs, 1) = uv_up_left[1];
			GET_INDEX(drawable_uvs, 2) = uv_down_left[0]; GET_INDEX(drawable_uvs, 3) = uv_down_left[1];
			GET_INDEX(drawable_uvs, 4) = uv_up_right[0]; GET_INDEX(drawable_uvs, 5) = uv_up_right[1];

			GET_INDEX(drawable_uvs, 6) = uv_down_right[0]; GET_INDEX(drawable_uvs, 7) = uv_down_right[1];
			GET_INDEX(drawable_uvs, 8) = uv_up_right[0]; GET_INDEX(drawable_uvs, 9) = uv_up_right[1];
			GET_INDEX(drawable_uvs, 10) = uv_down_left[0]; GET_INDEX(drawable_uvs, 11) = uv_down_left[1];

			GET_INDEX(drawable_vertices, 0) = vertex_up_left[0]; GET_INDEX(drawable_vertices, 1) = vertex_up_left[1];
			GET_INDEX(drawable_vertices, 2) = vertex_down_left[0]; GET_INDEX(drawable_vertices, 3) = vertex_down_left[1];
			GET_INDEX(drawable_vertices, 4) = vertex_up_right[0]; GET_INDEX(drawable_vertices, 5) = vertex_up_right[1];

			GET_INDEX(drawable_vertices, 6) = vertex_down_right[0]; GET_INDEX(drawable_vertices, 7) = vertex_down_right[1];
			GET_INDEX(drawable_vertices, 8) = vertex_up_right[0]; GET_INDEX(drawable_vertices, 9) = vertex_up_right[1];
			GET_INDEX(drawable_vertices, 10) = vertex_down_left[0]; GET_INDEX(drawable_vertices, 11) = vertex_down_left[1];

#undef GET_INDEX

			i++;
		}
	}

	text->drawable = drawable_allocate(2);

	ArrayBufferDeclaration declarations[] = {
		{drawable_vertices, sizeof(float) * 12 * text_length, 2, 0, GL_STATIC_DRAW},
		{drawable_uvs, sizeof(float) * 12 * text_length, 2, 1, GL_STATIC_DRAW}
	};

	drawable_init(text->drawable, NULL, 6 * text_length, declarations, ARRAY_SIZE(declarations), glyph_material, GL_TRIANGLES, NULL, &text->font_texture, 1, 0x0);

	return text;
}

void text_destroy(Text* text) {
	free(drawable_buffer_data(text->drawable, 0));
	free(drawable_buffer_data(text->drawable, 1));

	drawable_destroy(text->drawable);

	free(text);
}

void text_draw(Text* text, Vector2* shadow_displacement, float max_width, float max_height, Vector2 anchor, Mat4 view_position_matrix) {
	Vector2 text_position = text->position;

	Material* text_material = drawable_material(text->drawable);

	material_set_uniform_float(text_material, 5, max_width);		// Max width
	material_set_uniform_float(text_material, 6, max_height);	// Max height
	
	material_set_uniform_vec2(text_material, 3, text_position);	// Position
	material_set_uniform_vec2(text_material, 7, anchor);			// Anchor position

	material_use(text_material, NULL, view_position_matrix);		// Using the text's material

	if (shadow_displacement) {
		Vector3 shadow_color = { 0.f, 0.f, 0.f };
		Vector2 shadow_drawable_position;

		vector2_add(&shadow_drawable_position, text_position, *shadow_displacement);	// Setting the shadow's position

		material_uniform_vec3(text_material, 2, shadow_color);				// Color
		material_uniform_vec2(text_material, 0, shadow_drawable_position);	// Model position

		drawable_draw(text->drawable);	// Drawing the shadow
	}

	material_uniform_vec3(text_material, 2, text->color);				// Color
	material_uniform_vec2(text_material, 0, text_position);				// Model position

	drawable_draw(text->drawable);	// Drawing the text
}

Scene* scene_create(Vector3 camera_position, GLFWwindow* window) {
	Scene* scene = malloc(sizeof(Scene));
	assert(scene != NULL);

	int width, height;

	glfwGetWindowSize(window, &width, &height);

	camera_init(&scene->camera, camera_position, 1e+4f, 1e-4f, 120.f, width, height);

	scene->flags = 0x0;
	scene->selected_window = 0;
	scene->glfw_last_character = 0;

	DYNAMIC_ARRAY_CREATE(&scene->drawables, Drawable*);
	DYNAMIC_ARRAY_CREATE(&scene->windows, Window);

	glfwSetWindowUserPointer(window, scene);

	return scene;
}

void scene_set_size(Scene* scene, float width, float height) {
	scene->camera.width = width;
	scene->camera.height = height;
	
	mat4_create_perspective(scene->camera.perspective_matrix, 1000.f, 0.1f, 90.f, (float) scene->camera.width / scene->camera.height);

	float half_width = (float)width / 2,
		half_height = (float)height / 2;

	mat4_create_orthogonal(scene->camera.ortho_matrix, -half_width, half_width, -half_height, half_height, -2.f, 2.f);

	glViewport(0, 0, scene->camera.width, scene->camera.height);
}

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
		if (flags & DRAWABLE_NO_DEPTH_TEST) {
			glDisable(GL_DEPTH_TEST);
		}
		else {
			glEnable(GL_DEPTH_TEST);
		}

		material_use(drawable_material(drawable), position_matrix, camera_final_matrix);
		drawable_draw(drawable);

		if (flags & DRAWABLE_SHOW_AXIS) {
			// Drawing the elements axis
			glDisable(GL_DEPTH_TEST);

			material_use(drawable_material(axis_drawable), position_matrix, camera_final_matrix);
			drawable_draw(axis_drawable);
		}
	}

	if (scene->flags & SCENE_GUI_MODE) {
		glDisable(GL_DEPTH_TEST);

		// Drawing the windows
		for (uint i = 0; i < scene->windows.size; i++) {
			Window* win = dynamic_array_at(&scene->windows, (i + scene->selected_window + 1) % scene->windows.size);

			window_draw(win, scene->camera.ortho_matrix);
		}
	}
}

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
				window_set_transparency(dynamic_array_at(&scene->windows, i), 0.9f);
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

	window->title->angle += 0.001f;

	text_set_position(window->title, text_position);
}

void window_set_position(Window* window, float x, float y) {
	window->position.x = x;
	window->position.y = y;

	window_update(window);

	Material* background_material = drawable_material(window->drawables[0]);
	material_set_uniform_vec2(background_material, 2, window->position);

	material_set_uniform_vec2(drawable_material(window->drawables[1]), 1, window->position);
}

void window_set_size(Window* window, float width, float height) {
	window->width = max(width, window->min_width);
	window->height = max(height, window->min_height);
	
	window_update(window);

	drawable_rectangle_set_size(window->drawables[0], window->width, window->height);

	float* text_bar_vertices = drawable_buffer_data(window->drawables[1], 0);
	text_bar_vertices[0] = window->title->size + 6.f; text_bar_vertices[1] = window->height;
	text_bar_vertices[2] = window->title->size + 6.f; text_bar_vertices[3] = 0.f;

	drawable_update_buffer(window->drawables[1], 0);
}

void window_set_transparency(Window* window, float transparency) {
	window->transparency = transparency;
	material_set_uniform_float(drawable_material(window->drawables[0]), 1, window->transparency);

	for (uint i = 0; i < window->widgets_count; i++)
		widget_set_transparency(window->widgets[i], transparency);
}

WindowID window_create(Scene* scene, float width, float height, float* position, char* title) {
	Window* window = dynamic_array_push_back(&scene->windows);

	window->min_width = 200.f;
	window->min_height = 100.f;

	window->pack_last_size = 0.f;

	window->widgets_count = 0;
	window->layout = LAYOUT_PACK;

	Vector3 title_color = { 0.6f, 0.6f, 0.6f };
	window->title = text_create(title, monospaced_font_texture, 15.f, window->position, ((float) M_PI) * 3 / 2, title_color);

	Drawable* background_drawable = drawable_allocate(1);
	assert(background_drawable != NULL);

	drawable_rectangle_init(background_drawable, window->width, window->height, material_create(ui_background_shader, ui_background_uniforms, ARRAY_SIZE(ui_background_uniforms)), GL_TRIANGLES, NULL, 0x0);
	window->drawables[0] = background_drawable;

	// #ECE8D9
	Vector3 backgroud_color = rgb_to_vec(0x1d, 0x23, 0x86);

	material_set_uniform_vec3(drawable_material(background_drawable), 0, backgroud_color);

	Drawable* text_bar_drawable = drawable_allocate(1);

	assert(text_bar_drawable != NULL);

	float* line_vertices = malloc(sizeof(float) * 4);

	ArrayBufferDeclaration text_bar_declarations[] = {
		{line_vertices, sizeof(float) * 4, 2, 0, GL_DYNAMIC_DRAW}
	};

	static Vector3 bar_color = {
		0.7f, 0.7f, 0.7f
	};

	drawable_init(text_bar_drawable, NULL, 2, text_bar_declarations, 1, material_create(color_shader, color_uniforms, ARRAY_SIZE(color_uniforms)), GL_LINES, NULL, NULL, 0, 0x0);
	window->drawables[1] = text_bar_drawable;

	material_set_uniform_vec3(drawable_material(window->drawables[1]), 0, bar_color);

	window_set_size(window, width, height);
	window_set_position(window, position[0], position[1]);
	window_set_transparency(window, 0.9f);

	scene->selected_window = scene->windows.size - 1;

	return scene->windows.size - 1;
}

Vector2 window_get_anchor(Window* window) {
	Vector2 window_anchor = { 
		window->position.x + 30.f, 
		window->position.y + window->height - 30.f
	};

	return window_anchor;
}

void window_draw(Window* window, Mat4 view_position_matrix) {
	assert(window->layout == LAYOUT_PACK);

	Drawable* background_drawable = window->drawables[0];

	material_use(drawable_material(background_drawable), NULL, view_position_matrix);
	drawable_draw(background_drawable);

	Drawable* line_drawable = window->drawables[1];

	material_use(drawable_material(line_drawable), NULL, view_position_matrix);
	drawable_draw(line_drawable);

	Vector3 shadow_displacement = { 0.01f, 0.f };

	text_draw(window->title, &shadow_displacement, window->height * 0.8f - 0.05f, window->title->size, window->title->position, view_position_matrix);

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

void window_destroy(Scene* scene, uint id) {
	Window* window = dynamic_array_at(&scene->windows, id);

	free(drawable_buffer_data(window->drawables[0], 0));
	free(drawable_buffer_data(window->drawables[1], 0));

	// Freeing memory resources
	for (uint i = 0; i < ARRAY_SIZE(window->drawables); i++)
		drawable_destroy(window->drawables[i]);
	
	for (uint i = 0; i < window->widgets_count; i++)
		widget_destroy(window->widgets[i]);

	text_destroy(window->title);

	// Reajusting the windows array
	dynamic_array_remove(&scene->windows, id);

	scene->selected_window = scene->selected_window % scene->windows.size;
}

Vector2 widget_get_real_position(Widget* widget, Window* window) {
	Vector2 widget_position = {
		widget->position.x + widget->margin,
		widget->position.y
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
	if (widget->on_hover != NULL) {
		widget->on_hover(widget, evt);
	}
}

void widget_on_click(Widget* widget, Event* evt) {
	if (widget->on_click != NULL) {
		widget->on_click(widget, evt);
	}
}

void widget_set_on_hover(Widget* widget, EventCallback on_hover) {
	widget->on_hover = on_hover;
}

void widget_set_on_click(Widget* widget, EventCallback on_click) {
	widget->on_click = on_click;
}

Widget* widget_label_create(WindowID window_id, Scene* scene, Widget* parent, char* text, float text_size, float margin, Vector3 color, Layout layout) {
	Window* window = dynamic_array_at(&scene->windows, window_id);
	
	Vector2 text_position = { 0.f, 0.f };

	Label* label = malloc(sizeof(Label));

	assert(label != NULL);

	label->header.type = WIDGET_TYPE_LABEL;
	// Setting the widget's type

	label->color = color;
	label->text = text_create(text, monospaced_font_texture, text_size, text_position, 0.f, label->color);	// Initializing the text

	label->header.height = text_get_height(label->text);	// Setting widget height

	widget_init(label, window, parent, margin, layout);		// Intializing the widget

	return label;
}

Widget* widget_button_create(WindowID window_id, Scene* scene, Widget* parent, char* text, float text_size, float margin, float padding, Layout layout) {
	Window* window = dynamic_array_at(&scene->windows, window_id);

	static const float border_size = 1.f;

	Vector2 text_position = { 0.f, 0.f };

	Button* button = malloc(sizeof(Button));	// Allocating the button widget
	assert(button != NULL);

	button->header.type = WIDGET_TYPE_BUTTON;	// Setting button type, padding and hover function

	button->padding = padding + border_size * 2;
	button->text = text_create(text, monospaced_font_texture, text_size, text_position, 0.f, button_text_color);	// Initializing the button's text

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

	widget_init(button, window, parent, margin, layout);	// Intializing the widget

	return button;
}

void widget_label_draw(Window* window, Widget* widget, Vector2 position, Mat4 view_position_matrix) {
	Label* label_widget = widget;

	static Vector2 label_shadow_displacement = { 0.005f, 0.f };

	text_set_position(label_widget->text, position);	// Setting the text's position

	Vector2 window_max_position = window_get_max_position(window);

	text_draw(label_widget->text, &label_shadow_displacement, window_max_position.x, window_max_position.y, window_get_anchor(window), view_position_matrix);
}

void widget_button_draw(Window* window, Widget* widget, Vector2 position, Mat4 view_position_matrix) {
	Button* button = widget;

	Vector2 background_position = position;		// Calculating the button's background position
	background_position.y -= button->header.height + button->padding * 2;

	Vector2 window_max_position = window_get_max_position(window);	// Max position before fading

	Material* button_material = drawable_material(button->button_background);

	material_set_uniform_vec2(button_material, 0, background_position);			// Position
	material_set_uniform_vec2(button_material, 4, window_get_anchor(window));	// Anchor

	material_set_uniform_float(button_material, 2, window_max_position.x);		// Max Width
	material_set_uniform_float(button_material, 3, window_max_position.y);		// Max Height

	material_set_uniform_float(button_material, 6, widget_get_width(button));	// Width
	material_set_uniform_float(button_material, 7, widget_get_height(button));	// Height

	if (widget->state & WIDGET_STATE_HOVERED) {		// Setting the background color
		button->text->color = button_text_hover_color;
		material_set_uniform_vec3(button_material, 8, button_background_hover_color);
	}
	else {
		button->text->color = button_text_color;
		material_set_uniform_vec3(button_material, 8, button_background_color);
	}

	material_use(button_material, NULL, view_position_matrix);	// Drawing the background using the material
	drawable_draw(button->button_background);

	position.x += button->padding;	// Drawing the text a little bit below
	position.y -= button->padding;

	text_set_position(button->text, position);

	text_draw(button->text, NULL, window_max_position.x, window_max_position.y, window_get_anchor(window), view_position_matrix);
}

void widget_label_set_transparency(Widget* widget, float transparency) {
	text_set_transparency(((Label*)widget)->text, transparency);
}

void widget_button_set_transparency(Widget* widget, float transparency) {
	Button* button = widget;
	
	text_set_transparency(button->text, transparency);
	material_set_uniform_float(drawable_material(button->button_background), 1, transparency);
}

void widget_label_destroy(Widget* widget) {
	Label* label = widget;
	
	text_destroy(label->text);
	free(label);
}

void widget_button_destroy(Widget* widget) {
	Button* button = widget;

	text_destroy(button->text);

	free(drawable_buffer_data(button->button_background, 0));

	drawable_destroy(button->button_background);
	free(button);
}

static void (*widget_draw_vtable[])(Window*, Widget*, Vector2, Mat4) = {
	[WIDGET_TYPE_LABEL] = &widget_label_draw,
	[WIDGET_TYPE_BUTTON] = &widget_button_draw,
};

static void (*widget_set_transparency_vtable[])(Widget*, float) = {
	[WIDGET_TYPE_LABEL] = &widget_label_set_transparency,
	[WIDGET_TYPE_BUTTON] = &widget_button_set_transparency,
};

static void (*widget_destroy_vtable[])(Widget*) = {
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
		0.f, 0.f, 0.f,
		1.f, 0.f, 0.f,
		0.f, 1.f, 0.f,
		0.f, 0.f, 1.f
	};

	static unsigned short axis_elements[] = {
		0, 1, 0, 2, 0, 3
	};

	static ArrayBufferDeclaration axis_buffers[] = {
		{axis, sizeof(axis), 3, 0, GL_STATIC_DRAW}
	};

	static Vector3 axis_position = { 0.f, 0.f, 0.f };

	axis_shader = shader_create("./shaders/vertex_uniform_color.glsl", "./shaders/fragment_uniform_color.glsl");
	ui_background_shader = shader_create("./shaders/vertex_ui_background.glsl", "./shaders/fragment_ui_background.glsl");
	ui_texture_shader = shader_create("./shaders/vertex_ui_texture.glsl", "./shaders/fragment_ui_texture.glsl");
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