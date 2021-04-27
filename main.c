#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include "misc.h"
#include "render.h"
#include "physics.h"
#include "images.h"
#include "noise.h"
#include "psyche.h"
#include "random.h"
#include "window.h"

#define WORLD_STEP 0.1f

#define TERRAIN_SIZE 500

#define POINTS_COUNT 100

#define ITERATIONS_NUMBER 32000
#define SUBSET_NUMBER 10
#define RINGS_NUMBER 3

float points[POINTS_COUNT * 2];
Octaves octaves;
float global_time = 0.f;

void execute_tests(void);

float terrain_noise(float x, float y) {
	return clampf(distortion_noise(&octaves, x, y, 0.01f, 0.15f) + 0.5f, 0.f, 1.f);
}

float terrain_ridged_noise(float x, float y) {
	return clampf(ridged_noise(&octaves, x, y) + 0.5f, 0.f, 1.f);
}

static Scene* scene;
static Vector3* hopalong_points;
static Vector3* hopalong_color;
static Drawable* hopalong_drawable;

void update_fractal(void) {
	static const float scale = 4.f;

	float a = gaussian_random() * scale,
		b = gaussian_random() * scale,
		c = gaussian_random() * scale;

	printf("A %.2f; B %.2f; C %.2f;\n", a, b, c);

	hopalong_fractal(hopalong_points, ITERATIONS_NUMBER, a, b, c, 0.1f);

	Vector3 color;

	uint rate = (ITERATIONS_NUMBER / SUBSET_NUMBER);

	for (uint i = 0; i < ITERATIONS_NUMBER; i++) {
		if (i % rate == 0)
			random_arrayf((float*)&color, 3);

		hopalong_color[i] = color;
	}

	drawable_update(hopalong_drawable);
}

static char* main_file_contents = NULL;
static World* physic_world;
static Drawable* triangle1_drawable;
static Drawable* triangle2_drawable;
static Vector3 background_color = { { 0, 0, 0.2f } };
static PsButton *randomize_btn, *quit_btn;

void update(clock_t fps) {
	scene_draw(scene, background_color);

	char buf[256];
	snprintf(buf, sizeof(buf), "%lu FPS", fps);

	ps_text(buf, (Vector2) { { -400.f, 300.f } }, 30.f, (Vector4){ { 1.f, 1.f, 1.f, 1.f } });

	if (ps_button_state(randomize_btn) & PS_BUTTON_CLICKED)
		update_fractal();

	if (ps_button_state(quit_btn) & PS_BUTTON_CLICKED)
		g_window.should_close = GL_TRUE;

	if (scene->flags & SCENE_GUI_MODE)
		ps_render();

	scene_handle_events(scene);

	if (g_window.keys[GLFW_KEY_Q]) {
		world_update(physic_world, 0.01f);

		drawable_update(triangle1_drawable);
		drawable_update(triangle2_drawable);
	}
}

void setup() {
	scene = scene_create((Vector3) { { 0.f, 0.f, 0.f } });
	ps_init();
}

int main() {
	random_seed(time(NULL));

	main_file_contents = read_file("lisp/core.ul");
	execute_tests();			// Unit tests

	Image lain_image;
	Image copland_os_image;

	if (image_load_bmp(&copland_os_image, "./images/copland_os_enterprise.bmp") < 0)
		goto error;
	if (image_load_bmp(&lain_image, "./images/lain.bmp") < 0)
		goto error;

	Vector3 triangle1_vertices[] = {
		{ { 2.f, 1.f, 0.f, } },
		{ { 0.f, 0.f, 2.f, } },
		{ { 0.f, 0.f, -2.f } },
	};

	Vector3 triangle2_vertices[] = {
		{ { 1.f, 0.f, 0.f, } },
		{ { 0.f, 1.f, 1.f, } },
		{ { -1.f, 0.f, 0.f } },
	};

	float texture_coords[] = {
		0.0f, 0.0f,
		0.5f, 1.0f,
		1.0f, 0.0f,
	};

	Vector3 gravity = { { 0.f, -0.05f, 0.f } };
	physic_world = world_create(gravity, 10);

	Shape triangle1_shape = { { { 0.f, 0.f, 0.f } }, triangle1_vertices, NULL, 3, 3, };
	Shape triangle2_shape = { { { 0.f, 1.f, 0.5f } }, triangle2_vertices, NULL, 3, 3 };

	world_body_add(physic_world, &triangle1_shape, 0.f);
	world_body_add(physic_world, &triangle2_shape, 1.f);

	Vector3* terrain_vertices = malloc(sizeof(Vector3) * TERRAIN_SIZE * TERRAIN_SIZE);
	uint* terrain_indexes = malloc(sizeof(uint) * (TERRAIN_SIZE - 1) * (TERRAIN_SIZE - 1) * 6);
	Vector3* terrain_color = malloc(sizeof(Vector3) * (TERRAIN_SIZE - 1) * (TERRAIN_SIZE - 1) * 6);

	random_arrayf(points, 2 * POINTS_COUNT);
	octaves_init(&octaves, 8, 5, 2.f, 0.5f);

	Image noise_image;
	noise_image_create(&noise_image, 500, terrain_noise);
	image_write_to_file(&noise_image, "./images/noise.ppm");

	float terrain_width = 80.f, terrain_height = 7.f;
	terrain_create(terrain_vertices, TERRAIN_SIZE, terrain_height, terrain_width, &terrain_noise);
	terrain_elements(terrain_indexes, TERRAIN_SIZE);

	for (uint i = 0; i < (TERRAIN_SIZE - 1) * (TERRAIN_SIZE - 1) * 6; i++) {
		float height = terrain_vertices[terrain_indexes[i]].y / terrain_height;

		terrain_color[terrain_indexes[i]].x = height;
		terrain_color[terrain_indexes[i]].y = 0.f;
		terrain_color[terrain_indexes[i]].z = 1 - height;
	}

	hopalong_points = malloc(sizeof(Vector3) * ITERATIONS_NUMBER);
	hopalong_color = malloc(sizeof(Vector3) * ITERATIONS_NUMBER);

	window_create(1200, 800, "Emergence", setup, update);

	window_add_resize_hook(scene_resize_callback, scene);
	window_add_resize_hook(ps_resized_callback, NULL);

	GLuint texture_shader = shader_create("./shaders/vertex_texture.glsl", "./shaders/fragment_texture.glsl");
	GLuint color_shader = shader_create("./shaders/vertex_color.glsl", "./shaders/fragment_color.glsl");

	GLuint lain_texture, copland_os_texture;

	Material* texture_material1 = material_create(texture_shader, NULL, 0);
	Material* texture_material2 = material_create(texture_shader, NULL, 0);

	lain_texture = texture_create(&lain_image);
	copland_os_texture = texture_create(&copland_os_image);

	image_destroy(&lain_image);
	image_destroy(&copland_os_image);

	ArrayBufferDeclaration triangle1_buffers[] = {
		{ triangle1_vertices, sizeof(triangle1_vertices), 3, 0, GL_STATIC_DRAW },
		{ texture_coords, sizeof(texture_coords), 2, 1, GL_STATIC_DRAW }
	};

	ArrayBufferDeclaration triangle2_buffers[] = {
		{ triangle2_vertices, sizeof(triangle2_vertices), 3, 0, GL_STATIC_DRAW },
		{ texture_coords, sizeof(texture_coords), 2, 1, GL_STATIC_DRAW }
	};

	triangle1_drawable = scene_create_drawable(scene, NULL, 3, triangle1_buffers, 2, texture_material1, GL_TRIANGLES, &triangle1_shape.position, &lain_texture, 1, DRAWABLE_SHOW_AXIS);
	triangle2_drawable = scene_create_drawable(scene, NULL, 3, triangle2_buffers, 2, texture_material2, GL_TRIANGLES, &triangle2_shape.position, &copland_os_texture, 1, DRAWABLE_SHOW_AXIS);

	Material* terrain_material = material_create(color_shader, NULL, 0);

	ArrayBufferDeclaration terrain_buffers[] = {
		{terrain_vertices, sizeof(Vector3) * TERRAIN_SIZE * TERRAIN_SIZE, 3, 0, GL_STATIC_DRAW},
		{terrain_color, sizeof(Vector3) * (TERRAIN_SIZE - 1) * (TERRAIN_SIZE - 1) * 6, 3, 1, GL_STATIC_DRAW}
	};

	Vector3 terrain_position = { { 0.f, -5.f, 0.f } };
	scene_create_drawable(scene, terrain_indexes, (TERRAIN_SIZE - 1) * (TERRAIN_SIZE - 1) * 6, terrain_buffers, ARRAY_SIZE(terrain_buffers), terrain_material, GL_TRIANGLES, &terrain_position, NULL, 0, 0x0);

	Material* hopalong_material = material_create(color_shader, NULL, 0);
	ArrayBufferDeclaration hopalong_buffers[] = {
		{hopalong_points, sizeof(Vector3) * ITERATIONS_NUMBER, 3, 0, GL_STATIC_DRAW},
		{hopalong_color, sizeof(Vector3) * ITERATIONS_NUMBER, 3, 1, GL_STATIC_DRAW}
	};

	Vector3	hopalong_position = { { 10.f, 0.f, 5.f } };
	hopalong_drawable = scene_create_drawable(scene, NULL, ITERATIONS_NUMBER, hopalong_buffers, ARRAY_SIZE(hopalong_buffers), hopalong_material, GL_POINTS, &hopalong_position, NULL, 0, 0x0);

	update_fractal();

	PsWindow* test_window = ps_window_create("Serial Experiments Lain: Layer 04, Girls");
	PsLabel* test_label = ps_label_create(test_window, "Accela\nA drug created to kill most people", 15);
	PsLabel* label = ps_label_create(test_window, "TESTTEEE", 15);
	PsLabel* child_label = ps_label_create(test_window, "LETS ALL LOVE LAIN", 15);

	randomize_btn = ps_button_create(test_window, "Randomize fractal", 16);
	quit_btn = ps_button_create(test_window, "Quit game", 16);

	window_mainloop();

	return 0;

error:
	printf("Something failed...\n");
	return -1;
}
