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
#include "render.h"
#include "drawable.h"
#include "batch_renderer.h"
#include "psyche.h"
#include "window.h"

#define SCENE_DEFAULT_CAPACITY 10
#define CAMERA_SPEED 0.1f
#define MOUSE_SENSIBILLITY 0.005f

#define WINDOW_ELEMENT_DEPTH_OFFSET 0.001f

#define WINDOW_BACKGROUND_VERTEX_SIZE 8

static GLuint axis_shader;
static GLuint screen_shader;

static Material* axis_material = NULL;

static char* axis_uniforms[] = {
	"color", "transparency"
};

enum {
	AXIS_MODEL_COLOR_UNIFORM = 0,
	AXIS_MODEL_TRANSPARENCY_UNIFORM,
};

static Drawable* axis_drawable;

double last_xpos = -1.0, last_ypos = -1.0;

extern float global_time;

void render_initialize(void);
float random_float(void);

void scene_resize_callback(void* scene, int width, int height) {
	scene_set_size(scene, width, height);
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

Scene* scene_create(Vector3 camera_position) {
	Scene* scene = malloc(sizeof(Scene)); // Allocating the scene object
	assert(scene != NULL);

	// Initializing framebuffer
	glGenFramebuffers(1, &scene->fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, scene->fbo);

	glGenTextures(1, &scene->fbo_color_buffer);
	glGenRenderbuffers(1, &scene->rbo);

	scene_update_framebuffer(scene, g_window.size.x, g_window.size.y);

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, scene->fbo_color_buffer, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, scene->rbo);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		printf("ERROR::FRAMEBUFFER:: Framebuffer is not complete!\n");
		return NULL;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	render_initialize();		// Initialize scene
	camera_init(&scene->camera, camera_position, 1e+4f, 1e-4f, 120.f, g_window.size.x, g_window.size.y); // Initalize player camera

	scene->flags = 0x0;

	DYNAMIC_ARRAY_CREATE(&scene->drawables, Drawable*);

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

	return scene;
}

// Change size of scene viewport
void scene_set_size(Scene* scene, float width, float height) {
	scene->camera.width = width;
	scene->camera.height = height;

	mat4_create_perspective(scene->camera.perspective_matrix, 1000.f, 0.1f, 90.f, (float)scene->camera.width / scene->camera.height);

	float half_width = (float)width / 2,
		half_height = (float)height / 2;

	mat4_create_orthogonal(scene->camera.ortho_matrix, -half_width, half_width, -half_height, half_height, -2.f, 2.f);

	glViewport(0, 0, scene->camera.width, scene->camera.height);
	scene_update_framebuffer(scene, width, height);
}

void scene_toggle_wireframe(Scene* scene) {
	scene->flags ^= SCENE_WIREFRAME;
}

void scene_draw(Scene* scene, Vector3 clear_color) {
	glBindFramebuffer(GL_FRAMEBUFFER, scene->fbo);

	glClearColor(clear_color.x, clear_color.y, clear_color.z, 0.01f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	if (scene->flags & SCENE_WIREFRAME)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	Mat4 camera_final_matrix;
	camera_get_final_matrix(&scene->camera, camera_final_matrix);

	for (uint i = 0; i < scene->drawables.size; i++) {
		Drawable* drawable = *(Drawable**)dynamic_array_at(&scene->drawables, i);
		uint flags = drawable->flags;

		Mat4 position_matrix;
		mat4_create_translation(position_matrix, *drawable->position);

		// Drawing the elements added to the scene
		if (flags & DRAWABLE_NO_DEPTH_TEST)
			glDisable(GL_DEPTH_TEST);
		else
			glEnable(GL_DEPTH_TEST);

		material_use(drawable->material, position_matrix, camera_final_matrix);
		drawable_draw(drawable);

		if (flags & DRAWABLE_SHOW_AXIS) {
			material_use(axis_drawable->material, position_matrix, camera_final_matrix);
			drawable_draw(axis_drawable);
		}
	}

	if (scene->flags & SCENE_WIREFRAME)
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	glClearColor(1.f, 0.0f, 1.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	glDisable(GL_DEPTH_TEST);

	glUseProgram(screen_shader);
	glBindVertexArray(scene->quad_vao);
	glBindTexture(GL_TEXTURE_2D, scene->fbo_color_buffer);
	glUniform1f(glGetUniformLocation(screen_shader, "time"), global_time);
	glDrawArrays(GL_TRIANGLES, 0, 6);
}

// Add drawable to scene
Drawable* scene_create_drawable(Scene* scene, uint* elements, uint elements_number, ArrayBufferDeclaration* declarations, uint declarations_count, Material* material, GLenum mode, Vector3* position, GLuint* textures, uint textures_count, uint flags) {
	Drawable** drawable_pos = dynamic_array_push_back(&scene->drawables, 1);

	*drawable_pos = malloc(sizeof(Drawable) + sizeof(Buffer) * declarations_count);

	drawable_init(*drawable_pos, elements, elements_number, declarations, declarations_count, material, mode, position, textures, textures_count, flags);
	return *drawable_pos;
}

// Handle every event happening in the scene.
void scene_handle_events(Scene* scene) {
	if (!(scene->flags & SCENE_GUI_MODE) &&	g_window.keys[GLFW_KEY_E])
			scene->flags |= SCENE_GUI_MODE;

	if (scene->flags & SCENE_GUI_MODE && g_window.keys[GLFW_KEY_ESCAPE])
			scene->flags &= ~SCENE_GUI_MODE;

	float xpos = g_window.cursor_position.x,
		ypos = g_window.cursor_position.y;

	if (!(scene->flags & SCENE_GUI_MODE)) {
		Vector3 camera_direction;
		camera_get_direction(&scene->camera, &camera_direction, CAMERA_SPEED);

		if (g_window.keys[GLFW_KEY_W]) {
			camera_translate(&scene->camera, camera_direction);
		}
		if (g_window.keys[GLFW_KEY_S]) {
			vector3_neg(&camera_direction);
			camera_translate(&scene->camera, camera_direction);
		}

		if (last_xpos == -1.0 || last_ypos == -1.0) {
			last_xpos = xpos;
			last_ypos = ypos;
		}

		camera_rotate(&scene->camera, ((float)ypos - last_ypos) * -MOUSE_SENSIBILLITY, -((float)xpos - last_xpos) * MOUSE_SENSIBILLITY);
	}

	last_xpos = xpos;
	last_ypos = ypos;
}

void render_initialize(void) {
	// Initializing the drawable axis

	static Vector3 axis[] = {
		{ { 0.f, 0.f, 0.f } },
		{ { 1.f, 0.f, 0.f } },
		{ { 0.f, 1.f, 0.f } },
		{ { 0.f, 0.f, 1.f } }
	};

	static uint axis_elements[] = {
		0, 1, 0, 2, 0, 3
	};

	static ArrayBufferDeclaration axis_buffers[] = {
		{axis, sizeof(axis), 3, 0, GL_STATIC_DRAW}
	};

	static Vector3 axis_position = { { 0.f, 0.f, 0.f } };

	axis_shader = shader_create("./shaders/vertex_uniform_color.glsl", "./shaders/fragment_uniform_color.glsl");
	screen_shader = shader_create("./shaders/vertex_screen.glsl", "./shaders/fragment_screen.glsl");

	axis_material = material_create(axis_shader, axis_uniforms, ARRAY_SIZE(axis_uniforms));
	material_set_uniform_vec3(axis_material, AXIS_MODEL_COLOR_UNIFORM, (Vector3) { { 0, 0, 1 } });

	axis_drawable = malloc(sizeof(Drawable) + sizeof(Buffer));
	drawable_init(axis_drawable, axis_elements, 6, axis_buffers, 1, axis_material, GL_LINES, &axis_position, NULL, 0, 0x0);
}

#undef RENDER_INTERNAL
