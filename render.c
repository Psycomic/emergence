#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <math.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "misc.h"
#include "linear_algebra.h"
#include "stack_allocator.h"
#include "render.h"
#include "drawable.h"
#include "batch_renderer.h"

#define SCENE_DEFAULT_CAPACITY 10
#define CAMERA_SPEED 0.1f
#define MOUSE_SENSIBILLITY 0.01f

const Vector3 white = { { 1, 1, 1 } };
const Vector3 red = { { 1, 0, 0 } };
const Vector3 blue = { { 0, 0, 1 } };
const Vector3 black = { { 0, 0, 0 } };
const Vector3 green = { { 0, 1, 0 } };

#define SCENE_MAX_WINDOWS 4

#define SCENE_GUI_MODE			(1 << 0)
#define SCENE_EVENT_MOUSE_RIGHT	(1 << 1)
#define SCENE_EVENT_MOUSE_LEFT	(1 << 2)
#define SCENE_EVENT_QUIT		(1 << 3)

#define WINDOW_ELEMENT_DEPTH_OFFSET 0.001f

#define WINDOW_BACKGROUND_VERTEX_SIZE 8

static GLuint ui_background_shader;
static GLuint ui_text_shader;
static GLuint ui_button_shader;
static GLuint color_shader;
static GLuint axis_shader;
static GLuint text_bar_shader;
static GLuint single_color_shader;
static GLuint screen_shader;

static Font monospaced_font;

static Material* axis_material = NULL;

static char* ui_button_uniforms[] = {
	"model_position",			// 0
	"transparency",				// 1
	"width",					// 3
	"height",					// 4
	"color"						// 5
};

enum {
	UI_BUTTON_MODEL_POSITION_UNIFORM = 0,
	UI_BUTTON_TRANSPARENCY_UNIFORM,
	UI_BUTTON_WIDTH_UNIFORM,
	UI_BUTTON_HEIGHT_UNIFORM,
	UI_BUTTON_COLOR_UNIFORM,
};

static char* axis_uniforms[] = {
	"color", "transparency"
};

enum {
	AXIS_MODEL_COLOR_UNIFORM = 0,
	AXIS_MODEL_TRANSPARENCY_UNIFORM,
};

static Vector3 button_background_color = { { 0.5f, 0.5f, 0.5f } };
static Vector3 button_background_hover_color = { { 0.9f, 0.9f, 1.f } };
static Vector3 button_background_click_color = { { 0.2f, 0.2f, 0.4f } };

static Vector3 button_text_color = { { 0.f, 0.f, 0.f } };
static Vector3 button_text_hover_color = { { 0.2f, 0.2f, 0.4f } };
static Vector3 button_text_click_color = { { 0.8f, 0.8f, 0.9f } };

static Drawable* axis_drawable;

double last_xpos = -1.0, last_ypos = -1.0;

extern float global_time;

void render_initialize(void);
float random_float(void);

void window_draw(Window* window, Mat4 view_position_matrix);
void window_update(Window* window);
void window_scroll(Window* window, float amount);

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

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
	Scene* scene = glfwGetWindowUserPointer(window);
	window_scroll(scene->selected_window, -yoffset * 20.f);
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

void scene_update_framebuffer(Scene* scene, int width, int height) {
	glBindTexture(GL_TEXTURE_2D, scene->fbo_color_buffer);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glBindTexture(GL_TEXTURE_2D, 0);

	glBindRenderbuffer(GL_RENDERBUFFER, scene->rbo);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
}

Scene* scene_create(Vector3 camera_position, int width, int height, const char* title) {
	Scene* scene = malloc(sizeof(Scene)); // Allocating the scene object
	assert(scene != NULL);

	// Creating window and OpenGL context
	scene->context = glfwCreateWindow(width, height, title, NULL, NULL);
	m_bzero(&scene->gl, sizeof(StateContext));

	if (scene->context == NULL) {
		fprintf(stderr, "Failed to open a window\n");
		return NULL;
	}

	glfwSetCharCallback(scene->context, character_callback);
	glfwSetWindowSizeCallback(scene->context, resize_callback);

	glfwSetScrollCallback(scene->context, scroll_callback);

	glfwSetWindowUserPointer(scene->context, scene);
	glfwGetWindowSize(scene->context, &width, &height);

	glfwMakeContextCurrent(scene->context); // Make window the current context

	if (glewInit() != GLEW_OK) {
		fprintf(stderr, "Failed to initialize GLEW\n");
		return NULL;
	}

	glfwSwapInterval(1);			// Disable double buffering

	// Initializing framebuffer
	glGenFramebuffers(1, &scene->fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, scene->fbo);

	glGenTextures(1, &scene->fbo_color_buffer);
	glGenRenderbuffers(1, &scene->rbo);

	scene_update_framebuffer(scene, width, height);

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, scene->fbo_color_buffer, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, scene->rbo);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		printf("ERROR::FRAMEBUFFER:: Framebuffer is not complete!\n");
		return NULL;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	// OpenGL settings
	StateGlEnable(&scene->gl, GL_BLEND);			// Enable blend
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // Set blend func

	StateGlEnable(&scene->gl, GL_CULL_FACE);

	glViewport(0, 0, width, height); // Viewport size

	render_initialize();		// Initialize scene
	camera_init(&scene->camera, camera_position, 1e+4f, 1e-4f, 120.f, width, height); // Initalize player camera

	scene->flags = 0x0;
	scene->selected_window = 0;
	scene->glfw_last_character = 0;

	scene->windows_count = 0;

	DYNAMIC_ARRAY_CREATE(&scene->drawables, Drawable*);

	scene->last_window = NULL;

	float quadVertices[] = {
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
		1.0f, -1.0f,  1.0f, 0.0f,

        -1.0f,  1.0f,  0.0f, 1.0f,
		1.0f, -1.0f,  1.0f, 0.0f,
		1.0f,  1.0f,  1.0f, 1.0f
    };

    glGenVertexArrays(1, &scene->quad_vao);
    glGenBuffers(1, &scene->quad_vbo);
    glBindVertexArray(scene->quad_vao);
    glBindBuffer(GL_ARRAY_BUFFER, scene->quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

	Material* window_batch_material = material_create(ui_background_shader, NULL, 0);
	uint64_t windows_attributes_sizes[] = {
		3, 1, 2, 2 // Position, transparency, texture coords, quad size
	};

	batch_init(&scene->windows_batch, GL_TRIANGLES, window_batch_material, sizeof(float) * 2048,
			   sizeof(uint32_t) * 2048, windows_attributes_sizes, ARRAY_SIZE(windows_attributes_sizes));

	Material* text_batch_material = material_create(ui_text_shader, NULL, 0);
	uint64_t text_attributes_sizes[] = {
		3, 2, 1, 3 // Position, texture postion, transparency, color
	};

	batch_init(&scene->text_batch, GL_TRIANGLES, text_batch_material, sizeof(float) * 262144,
			   sizeof(uint32_t) * 262144, text_attributes_sizes, ARRAY_SIZE(text_attributes_sizes));

	Material* text_bar_material = material_create(text_bar_shader, NULL, 0);
	uint64_t text_bar_attributes_sizes[] = {
		3, 3 // Position, color
	};

	batch_init(&scene->window_text_bar_batch, GL_LINES, text_bar_material, sizeof(float) * 1024,
			   sizeof(uint32_t) * 1024, text_bar_attributes_sizes, ARRAY_SIZE(text_bar_attributes_sizes));

	return scene;
}

// Change size of scene viewport
void scene_set_size(Scene* scene, float width, float height) {
	scene->camera.width = width;
	scene->camera.height = height;

	mat4_create_perspective(scene->camera.perspective_matrix, 1000.f, 0.1f, 90.f,
							(float) scene->camera.width / scene->camera.height);

	float half_width = (float)width / 2,
		half_height = (float)height / 2;

	mat4_create_orthogonal(scene->camera.ortho_matrix, -half_width, half_width,
						   -half_height, half_height, -2.f, 2.f);

	glViewport(0, 0, scene->camera.width, scene->camera.height);
	scene_update_framebuffer(scene, width, height);
}

void scene_next_window(Scene* scene) {
	window_set_transparency(scene->selected_window, 0.3f);

	if (scene->selected_window->previous == NULL)
		for (; scene->selected_window->next != NULL; scene->selected_window = scene->selected_window->next);
	else
		scene->selected_window = scene->selected_window->previous;

	window_set_transparency(scene->selected_window, 1.f);
	scene_update_window_depths(scene);
}

void scene_draw(Scene* scene, Vector3 clear_color) {
	glBindFramebuffer(GL_FRAMEBUFFER, scene->fbo);

	StateGlEnable(&scene->gl, GL_STENCIL_TEST);
	glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

	glClearColor(clear_color.x, clear_color.y, clear_color.z, 0.01f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	glStencilMask(0x00);

	Mat4 camera_final_matrix;
	camera_get_final_matrix(&scene->camera, camera_final_matrix);

	StateGlEnable(&scene->gl, GL_CULL_FACE);
	for (uint i = 0; i < scene->drawables.size; i++) {
		Drawable* drawable = *(Drawable**)dynamic_array_at(&scene->drawables, i);
		uint flags = drawable->flags;

		Mat4 position_matrix;
		mat4_create_translation(position_matrix, *drawable->position);

		// Drawing the elements added to the scene
		if (flags & DRAWABLE_NO_DEPTH_TEST)
			StateGlDisable(&scene->gl, GL_DEPTH_TEST);
		else
			StateGlEnable(&scene->gl, GL_DEPTH_TEST);

		material_use(drawable->material, &scene->gl, position_matrix, camera_final_matrix);
		drawable_draw(drawable, &scene->gl);

		if (flags & DRAWABLE_SHOW_AXIS) {
			material_use(axis_drawable->material, &scene->gl, position_matrix, camera_final_matrix);
			drawable_draw(axis_drawable, &scene->gl);
		}
	}

	StateGlDisable(&scene->gl, GL_CULL_FACE);
	if (scene->flags & SCENE_GUI_MODE) {
		glClear(GL_DEPTH_BUFFER_BIT);
		batch_draw(&scene->window_text_bar_batch, &scene->gl, scene->camera.ortho_matrix);

		glStencilFunc(GL_ALWAYS, 1, 0xFF); // all fragments should pass the stencil test
		glStencilMask(0xFF); // enable writing to the stencil buffer
		batch_draw(&scene->windows_batch, &scene->gl, scene->camera.ortho_matrix);

		glStencilFunc(GL_EQUAL, 1, 0xFF);
		glStencilMask(0x00);

		for (Window* win = scene->last_window; win != NULL; win = win->previous)
			window_draw(win, scene->camera.ortho_matrix);

		StateGlActiveTexure(&scene->gl, GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, monospaced_font.texture);
		batch_draw(&scene->text_batch, &scene->gl, scene->camera.ortho_matrix);

		glStencilMask(0xFF);
		glStencilFunc(GL_ALWAYS, 1, 0xFF);
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	glClearColor(1.f, 0.0f, 1.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	StateGlDisable(&scene->gl, GL_DEPTH_TEST);

	glUseProgram(screen_shader);
	glBindVertexArray(scene->quad_vao);
	glBindTexture(GL_TEXTURE_2D, scene->fbo_color_buffer);
	glUniform1f(glGetUniformLocation(screen_shader, "time"), global_time);
	glDrawArrays(GL_TRIANGLES, 0, 6);
}

// Set windows' depth correspondingly to their priority rank
void scene_update_window_depths(Scene* scene) {
	uint i = 0;
	Window* window = scene->selected_window;

	do {
		window = window->previous;

		if (window == NULL)
			window = scene->last_window;

		window->depth = -((float)(scene->windows_count - 1 - i++) / scene->windows_count + 1.f);

		for (uint j = 0; j < window->background_drawable->vertices_count; j++) {
			float* vertex = (float*)window->background_drawable->vertices + WINDOW_BACKGROUND_VERTEX_SIZE * j;
			vertex[2] = window->depth;
		}

		for (uint j = 0; j < window->text_bar_drawable->vertices_count; j++) {
			float* vertex = (float*)window->text_bar_drawable->vertices +
				scene->window_text_bar_batch.vertex_size * j;

			vertex[2] = window->depth + WINDOW_ELEMENT_DEPTH_OFFSET;
		}

		text_set_depth(window->title, window->depth + WINDOW_ELEMENT_DEPTH_OFFSET);

		for (uint j = 0; j < window->widgets_count; j++)
			widget_set_depth(window->widgets[j], window->depth + WINDOW_ELEMENT_DEPTH_OFFSET);

		window_update(window);
	} while (window != scene->selected_window);
}

// Add drawable to scene
Drawable* scene_create_drawable(Scene* scene, unsigned short* elements, uint elements_number,
								ArrayBufferDeclaration* declarations, uint declarations_count,
								Material* material, GLenum mode, Vector3* position, GLuint* textures,
								uint textures_count, uint flags)
{
	Drawable** drawable_pos = dynamic_array_push_back(&scene->drawables);

	*drawable_pos = malloc(sizeof(Drawable) + sizeof(Buffer) * declarations_count);

	drawable_init(*drawable_pos, elements, elements_number, declarations, declarations_count, material, mode,
				  position, textures, textures_count, flags);
	return *drawable_pos;
}

// Handle every event happening in the scene. TODO: Cleanup
void scene_handle_events(Scene* scene, GLFWwindow* window) {
	if (scene->glfw_last_character == 'e')
		scene->flags ^= SCENE_GUI_MODE;

	double xpos, ypos;
	glfwGetCursorPos(window, &xpos, &ypos);

	int width, height;
	glfwGetWindowSize(window, &width, &height);

	if ((scene->flags & SCENE_GUI_MODE) && scene->last_window != NULL) {
		float screen_x = (float)xpos - (width / 2.f),
			screen_y = -(float)ypos + (height / 2.f);

		for (uint i = 0; i < scene->selected_window->widgets_count; i++) {
			Widget* widget = scene->selected_window->widgets[i];

			if (widget_is_colliding(widget, scene->selected_window, screen_x, screen_y)) {
				Event evt;
				evt.mouse_info.screen_x = screen_x;
				evt.mouse_info.screen_y = screen_y;

				widget->state |= WIDGET_STATE_HOVERED;

				if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
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
			scene_next_window(scene);
			break;
		case 'c':
			window_destroy(scene, scene->selected_window);
			break;
		}

		if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL))
			window_set_position(scene->selected_window,
								screen_x - scene->selected_window->width / 2,
								screen_y - scene->selected_window->height / 2);

		if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT)) {
			float new_width = screen_x - scene->selected_window->position.x;
			float new_height = screen_y - scene->selected_window->position.y;

			window_set_size(scene->selected_window, new_width, new_height);
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
		camera_rotate(&scene->camera, ((float)ypos - last_ypos) * MOUSE_SENSIBILLITY,
					  -((float)xpos - last_xpos) * MOUSE_SENSIBILLITY);
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
	text_set_angle(window->title, M_PI / 2.f);

	batch_drawable_update(window->background_drawable);

	static Vector3 text_bar_color = {
		{ 0.7f, 0.7f, 0.7f }
	};

	float* text_bar_vertices = window->text_bar_drawable->vertices;

	text_bar_vertices[0] = text_get_size(window->title) + 6.f + window->position.x;
	text_bar_vertices[1] = window->height + window->position.y;
	text_bar_vertices[2] = window->depth + WINDOW_ELEMENT_DEPTH_OFFSET;

	text_bar_vertices[3] = text_bar_color.x;
	text_bar_vertices[4] = text_bar_color.y;
	text_bar_vertices[5] = text_bar_color.z;

	text_bar_vertices[6] = text_get_size(window->title) + 6.f + window->position.x;
	text_bar_vertices[7] = window->position.y;
	text_bar_vertices[8] = window->depth + WINDOW_ELEMENT_DEPTH_OFFSET;

	text_bar_vertices[9] = text_bar_color.x;
	text_bar_vertices[10] = text_bar_color.y;
	text_bar_vertices[11] = text_bar_color.z;

	batch_drawable_update(window->text_bar_drawable);
}

void window_set_position(Window* window, float x, float y) {
	window->position.x = x;
	window->position.y = y;

	rectangle_vertices_set((float*)window->background_drawable->vertices,
						   window->width, window->height, WINDOW_BACKGROUND_VERTEX_SIZE,
						   window->position.x, window->position.y);

	for (uint i = 0; i < window->widgets_count; i++)
		widget_update_position(window->widgets[i], window);

	window_update(window);
}

void window_set_size(Window* window, float width, float height) {
	window->width = max(width, window->min_width);
	window->height = max(height, window->min_height);

	rectangle_vertices_set((float*)window->background_drawable->vertices,
						   window->width, window->height, WINDOW_BACKGROUND_VERTEX_SIZE,
						   window->position.x, window->position.y);

	for (uint i = 0; i < window->background_drawable->vertices_count; i++) {
		float* vertex = ((float*)window->background_drawable->vertices) + i * WINDOW_BACKGROUND_VERTEX_SIZE;
		vertex[6] = window->width;
		vertex[7] = window->height;
	}

	for (uint i = 0; i < window->widgets_count; i++)
		widget_update_position(window->widgets[i], window);

	window_update(window);
}

void window_set_transparency(Window* window, float transparency) {
	window->transparency = transparency;

	for (uint i = 0; i < window->background_drawable->vertices_count; i++) {
		float* vertex = (float*)window->background_drawable->vertices + WINDOW_BACKGROUND_VERTEX_SIZE * i;
		vertex[3] = transparency;
	}

	batch_drawable_update(window->background_drawable);

	for (uint i = 0; i < window->widgets_count; i++)
		widget_set_transparency(window->widgets[i], transparency);
}

static uint32_t rectangle_elements[] = { 0, 1, 2, 1, 3, 2 };

Window* window_create(Scene* scene, float width, float height, float* position, char* title) {
	Window* window = malloc(sizeof(Window));
	m_bzero(window, sizeof(Window));

	window->next = NULL;
	window->previous = scene->last_window;

	if (scene->last_window != NULL)
		scene->last_window->next = window;

	scene->last_window = window;
	scene->windows_count++;

	window->parent = scene;
	window->min_width = 200.f;
	window->min_height = 100.f;
	window->depth = -1.f;
	window->pack_last_size = 0.f;
	window->widgets_count = 0;
	window->layout = LAYOUT_PACK;
	window->on_close = NULL;
	window->scroll_offset = 0.f;

	window->position.x = position[0];
	window->position.y = position[1];

	Vector3 title_color = {
		{ 0.6f, 0.6f, 0.6f }
	};

	window->title = text_create(&scene->text_batch, &monospaced_font,
								title, 15.f, window->position, title_color);

	float* background_drawable_vertices = malloc(sizeof(float) * 4 * WINDOW_BACKGROUND_VERTEX_SIZE);

	background_drawable_vertices[4] = 0.f;
	background_drawable_vertices[5] = 0.f;
	background_drawable_vertices[4 + WINDOW_BACKGROUND_VERTEX_SIZE] = 1.f;
	background_drawable_vertices[5 + WINDOW_BACKGROUND_VERTEX_SIZE] = 0.f;
	background_drawable_vertices[4 + WINDOW_BACKGROUND_VERTEX_SIZE * 2] = 0.f;
	background_drawable_vertices[5 + WINDOW_BACKGROUND_VERTEX_SIZE * 2] = 1.f;
	background_drawable_vertices[4 + WINDOW_BACKGROUND_VERTEX_SIZE * 3] = 1.f;
	background_drawable_vertices[5 + WINDOW_BACKGROUND_VERTEX_SIZE * 3] = 1.f;

	window->background_drawable = batch_drawable_create(&scene->windows_batch, background_drawable_vertices,
														4, rectangle_elements, 6);

	float* text_bar_vertices = malloc(sizeof(float) * 6 * 2);
	static uint32_t text_bar_elements[] = { 0, 1 };

	window->text_bar_drawable = batch_drawable_create(&scene->window_text_bar_batch, text_bar_vertices, 2,
													  text_bar_elements, 2);

	window_set_size(window, width, height);
	window_set_position(window, window->position.x, window->position.y);
	window_set_transparency(window, 1.f);

	if (scene->selected_window == NULL)
		scene->selected_window = window;
	else
		scene_next_window(scene);

	return window;
}

void window_set_on_close(Window* window, void (*on_close)()) {
	window->on_close = on_close;
}

Vector2 window_get_anchor(Window* window) {
	Vector2 window_anchor = {
		{
			window->position.x + 30.f,
			window->position.y + window->height - 30.f + window->scroll_offset
		}
	};

	return window_anchor;
}

void window_draw(Window* window, Mat4 view_position_matrix) {
	assert(window->layout == LAYOUT_PACK);

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

void window_scroll(Window* window, float amount) {
	window->scroll_offset = clampf(window->scroll_offset + amount, 0.f, window->pack_last_size);

	for (uint i = 0; i < window->widgets_count; i++)
		widget_update_position(window->widgets[i], window);

}

void window_destroy(Scene* scene, Window* window) {
	if (window->previous == NULL && window->next == NULL) {
		float position[] = {
			0.f, 0.f
		};

		Window* error_window = window_create(scene, 400.f, 100.f, position, "ERROR");
		widget_label_create(error_window, scene, NULL, "ATTEMPTED TO DELETE\nSOLE WINDOW",
							14.f, 5.f, red, LAYOUT_PACK);
	}
	else {
		if (window->on_close != NULL)
			window->on_close();

		scene_next_window(scene);

		free(window->text_bar_drawable->vertices);
		batch_drawable_destroy(window->text_bar_drawable);

		for (uint i = 0; i < window->widgets_count; i++)
			widget_destroy(window->widgets[i]);

		text_destroy(window->title);
		batch_drawable_destroy(window->background_drawable);

		// Reajusting the windows List
		if (window->previous != NULL)
			window->previous->next = window->next;

		if (window->next != NULL)
			window->next->previous = window->previous;
		else
			scene->last_window = window->previous;

		scene->windows_count--;
	}
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

	widget_update_position(widget, window);
	window->widgets[window->widgets_count++] = widget;

	if (window == window->parent->selected_window)
		widget_set_transparency(widget, 1.f);
	else
		widget_set_transparency(widget, 0.3f);

	widget_set_depth(widget, window->depth + WINDOW_ELEMENT_DEPTH_OFFSET);
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

void widget_on_click_up(Widget* widget, Event* evt) {
	if (widget->on_click_up != NULL)
		widget->on_click_up(widget, evt);
}

BOOL widget_state(Widget* widget, uint state) {
	return widget->state & state;
}

void widget_set_on_click_up(Widget* widget, EventCallback on_click_up) {
	widget->on_click_up = on_click_up;
}

Widget* widget_label_create(Window* window, Scene* scene, Widget* parent, char* text,
							float text_size, float margin, Vector3 color, Layout layout) {
	Vector2 text_position = { { 0.f, 0.f } };
	Label* label = malloc(sizeof(Label));

	assert(label != NULL);

	label->header.type = WIDGET_TYPE_LABEL;
	label->color = color;
	label->text = text_create(&scene->text_batch, &monospaced_font, text, text_size, text_position, color);

	label->header.height = text_get_height(label->text);	// Setting widget height

	widget_init(SUPER(label), window, parent, margin, layout);		// Intializing the widget

	return SUPER(label);
}

Widget* widget_button_create(Window* window, Scene* scene, Widget* parent, char* text,
							 float text_size, float margin, float padding, Layout layout) {
	static const float border_size = 1.f;

	Vector2 text_position = { { 0.f, 0.f } };

	Button* button = malloc(sizeof(Button));	// Allocating the button widget
	button->header.type = WIDGET_TYPE_BUTTON;	// Setting button type, padding and hover function

	button->padding = padding + border_size * 2;
	// Initializing the button's text
	button->text = text_create(&scene->text_batch, &monospaced_font, text, text_size, text_position, button_text_color);

	float text_width = text_get_width(button->text),
		text_height = text_get_height(button->text);

	button->header.height = text_height;	// Setting widget height

	button->button_background = malloc(sizeof(Drawable) + sizeof(Buffer) * 1);	// Background of the button
	// Background's material
	Material* button_material = material_create(ui_button_shader, ui_button_uniforms, ARRAY_SIZE(ui_button_uniforms));

	drawable_rectangle_init(button->button_background,	// Initializing the background drawable
		text_width + button->padding * 2.f,
		text_height + button->padding * 2.f,
		button_material, GL_TRIANGLES, NULL, 0x0);

	material_set_uniform_vec3(button_material, UI_BUTTON_COLOR_UNIFORM, button_background_color);	// Color

	widget_init(SUPER(button), window, parent, margin, layout);	// Intializing the widget

	return SUPER(button);
}

void widget_label_draw(Window* window, void* widget, Vector2 position, Mat4 view_position_matrix) {
	/* pass */
}

void widget_button_draw(Window* window, void* widget, Vector2 position, Mat4 view_position_matrix) {
	Button* button = widget;

	Vector3 background_position;		// Calculating the button's background position
	background_position.x = position.x;
	background_position.y = position.y - (button->header.height + button->padding * 2);
	background_position.z = button->depth;

	Material* button_material = button->button_background->material;

	material_set_uniform_vec3(button_material, UI_BUTTON_MODEL_POSITION_UNIFORM, background_position);

	material_set_uniform_float(button_material, UI_BUTTON_WIDTH_UNIFORM, widget_get_width(SUPER(button)));
	material_set_uniform_float(button_material, UI_BUTTON_HEIGHT_UNIFORM, widget_get_height(SUPER(button)));

	if (button->header.state & WIDGET_STATE_CLICKED) {
		text_set_color(button->text, button_text_click_color);
		material_set_uniform_vec3(button_material, UI_BUTTON_COLOR_UNIFORM, button_background_click_color);
	}
	else if (button->header.state & WIDGET_STATE_HOVERED) {		// Setting the background color
		text_set_color(button->text, button_text_hover_color);
		material_set_uniform_vec3(button_material, UI_BUTTON_COLOR_UNIFORM, button_background_hover_color);
	}
	else {
		text_set_color(button->text, button_text_color);
		material_set_uniform_vec3(button_material, UI_BUTTON_COLOR_UNIFORM, button_background_color);
	}

	material_use(button_material, &window->parent->gl, NULL, view_position_matrix);	// Drawing the background using the material
	drawable_draw(button->button_background, &window->parent->gl);
}

void widget_label_set_transparency(void* widget, float transparency) {
	text_set_transparency(((Label*)widget)->text, transparency);
}

void widget_button_set_transparency(void* widget, float transparency) {
	Button* button = (Button*)widget;

	text_set_transparency(button->text, transparency);
	material_set_uniform_float(button->button_background->material, UI_BUTTON_TRANSPARENCY_UNIFORM, transparency);
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

	free(button->button_background->buffers[0].data);

	drawable_destroy(button->button_background);
	free(button);
}

void widget_label_set_depth(void* widget, float depth) {
	text_set_depth(((Label*)widget)->text, depth);
}

void widget_button_set_depth(void* widget, float depth) {
	Button* button = widget;

	button->depth = depth;
	text_set_depth(button->text, depth + WINDOW_ELEMENT_DEPTH_OFFSET);
}

void widget_label_update_position(void* widget, Window* window) {
	Label* label = widget;
	text_set_position(label->text, widget_get_real_position(widget, window));
}

void widget_button_update_position(void* widget, Window* window) {
	Button* button = widget;

	Vector2 position = widget_get_real_position(widget, window);

	position.x += button->padding;	// Drawing the text a little bit below
	position.y -= button->padding;

	text_set_position(button->text, position);
}

static void (*widget_draw_vtable[])(Window*, void*, Vector2, Mat4) = {
	[WIDGET_TYPE_LABEL] = widget_label_draw,
	[WIDGET_TYPE_BUTTON] = widget_button_draw,
};

static void (*widget_set_transparency_vtable[])(void*, float) = {
	[WIDGET_TYPE_LABEL] = widget_label_set_transparency,
	[WIDGET_TYPE_BUTTON] = widget_button_set_transparency,
};

static void (*widget_destroy_vtable[])(void*) = {
	[WIDGET_TYPE_LABEL] = widget_label_destroy,
	[WIDGET_TYPE_BUTTON] = widget_button_destroy
};

static void (*widget_set_depth_vtable[])(void*, float) = {
	[WIDGET_TYPE_LABEL] = widget_label_set_depth,
	[WIDGET_TYPE_BUTTON] = widget_button_set_depth
};

static void (*widget_update_position_vtable[])(void*, Window* window) = {
	[WIDGET_TYPE_LABEL] = widget_label_update_position,
	[WIDGET_TYPE_BUTTON] = widget_button_update_position
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

void widget_set_depth(Widget* widget, float depth) {
	widget_set_depth_vtable[widget->type](widget, depth);
}

void widget_update_position(Widget* widget, Window* window) {
	widget_update_position_vtable[widget->type](widget, window);
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
	text_bar_shader = shader_create("./shaders/vertex_text_bar.glsl", "./shaders/fragment_text_bar.glsl");
	single_color_shader = shader_create("./shaders/vertex_ui_background.glsl", "./shaders/fragment_single_color.glsl");
	screen_shader = shader_create("./shaders/vertex_screen.glsl", "./shaders/fragment_screen.glsl");

	axis_material = material_create(axis_shader, axis_uniforms, ARRAY_SIZE(axis_uniforms));
	material_set_uniform_vec3(axis_material, AXIS_MODEL_COLOR_UNIFORM, blue);

	axis_drawable = malloc(sizeof(Drawable) + sizeof(Buffer));
	drawable_init(axis_drawable, axis_elements, 6, axis_buffers, 1, axis_material, GL_LINES, &axis_position, NULL, 0, 0x0);

	Image font_image;
	if (image_load_bmp(&font_image, "./fonts/Monospace.bmp") >= 0) {
		printf("Success loading font !\n");
	}
	else {
		printf("Error when loading font !\n");
		exit(0);
	}

	font_init(&monospaced_font, &font_image, 19, 32, 304, 512);
	image_destroy(&font_image);
}

#undef RENDER_INTERNAL
