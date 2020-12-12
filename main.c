#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <string.h>
#include <assert.h>

#ifdef _WIN32 
#include <windows.h>

void usleep(clock_t time) {
	Sleep(time);
}
#endif // _WIN32 
#ifdef __linux__

#include <unistd.h>

#endif // __linux__

#include "misc.h"
#include "render.h"
#include "physics.h"
#include "images.h"
#include "noise.h"

#define WORLD_STEP 0.1f

#define TERRAIN_SIZE 255

#define POINTS_COUNT 100

float points[POINTS_COUNT * 2];

static Scene* scene;

void execute_tests(void);

float terrain_noise(float x, float y) {
	float positon[2] = {
		x, y
	};

	return voronoi_noise(2, points, POINTS_COUNT, positon, &cave_noise);
}

static WindowID confirm_window = -1;

void confirm_on_close() {
	confirm_window = -1;
}

void click_callback(Widget* widget, Event* evt) {
	if (confirm_window < 0) {
		float window_position[] = {
			random_float() * 800.f - 400.f,
			random_float() * 600.f - 300.f
		};

		confirm_window = window_create(scene, 367.91f, 140.31f, window_position, "CONFIRM");
		window_set_on_close(scene, confirm_window, &confirm_on_close);

		widget_label_create(confirm_window, scene, NULL, "DO YOU WANT TO CONFIRM", 14.f, 5.f, black, LAYOUT_PACK);
		widget_button_create(confirm_window, scene, NULL, "YES", 10.f, 5.f, 5.f, LAYOUT_PACK);
		widget_button_create(confirm_window, scene, NULL, "NO", 10.f, 5.f, 5.f, LAYOUT_PACK);
	}
	else {
		window_switch_to(scene, confirm_window);
	}
}

int main(void) {
	srand((uint)time(NULL));	// Seed for random number generation

#ifdef _DEBUG
	execute_tests();			// Unit tests
#endif

	Image copland_os_image;
	if (image_load_bmp(&copland_os_image, "./images/copland_os_enterprise.bmp") >= 0)
		printf("Success loading copland !\n");
	else
		printf("Error when loading image !\n");

	Image lain_image;
	if (image_load_bmp(&lain_image, "./images/copland_os_enterprise.bmp") >= 0)
		printf("Success loading lain !\n");
	else
		printf("Error when loading image !\n");

	Vector3 camera_position = { 0.f, 0.f, 0.f };

	Vector3 triangle1_vertices[] = {
		2.f, 1.f, 0.f,
		0.f, 0.f, 2.f,
		0.f, 0.f, -2.f
	};

	Vector3 triangle2_vertices[] = {
		1.f, 0.f, 0.f,
		0.f, 1.f, 1.f,
		-1.f, 0.f, 0.f
	};

	float texture_coords[] = {
		0.0f, 0.0f,
		0.5f, 1.0f,
		1.0f, 0.0f,
	};

	Vector3 gravity = { 0.f, -0.05f, 0.f };
	World* physic_world = world_create(gravity, 10);

	Shape triangle1_shape = { {0.f, 0.f, 0.f}, triangle1_vertices, NULL, 3, 3, };
	PhysicBody* triangle1_body = world_body_add(physic_world, &triangle1_shape, 0.f);

	Shape triangle2_shape = { {0.f, 1.f, 0.5f}, triangle2_vertices, NULL, 3, 3 };
	PhysicBody* triangle2_body = world_body_add(physic_world, &triangle2_shape, 1.f);

	Vector3* terrain_vertices = malloc(sizeof(Vector3) * TERRAIN_SIZE * TERRAIN_SIZE);
	unsigned short* terrain_indexes = malloc(sizeof(unsigned short) * (TERRAIN_SIZE - 1) * (TERRAIN_SIZE - 1) * 6);
	Vector3* terrain_color = malloc(sizeof(Vector3) * (TERRAIN_SIZE - 1) * (TERRAIN_SIZE - 1) * 6);

	random_arrayf(points, 2 * POINTS_COUNT);

	terrain_create(terrain_vertices, TERRAIN_SIZE, 10.f, 20.f, &terrain_noise);
	terrain_elements(terrain_indexes, TERRAIN_SIZE);

	for (uint i = 0; i < (TERRAIN_SIZE - 1) * (TERRAIN_SIZE - 1) * 6; i++) {
		float height = terrain_vertices[terrain_indexes[i]].y / 5.f;

		terrain_color[terrain_indexes[i]].x = height;
		terrain_color[terrain_indexes[i]].y = 0.f;
		terrain_color[terrain_indexes[i]].z = 1.f;
	}

#define ITERATIONS_NUMBER 10000

	Vector3* hopalong_points = malloc(sizeof(Vector3) * ITERATIONS_NUMBER);
	Vector3* hopalong_color = malloc(sizeof(Vector3) * ITERATIONS_NUMBER);

	for (uint i = 0; i < ITERATIONS_NUMBER; i++) {
		float c = (i % ITERATIONS_NUMBER) / (float)ITERATIONS_NUMBER;
		float c2 = 1.f - c;

		hopalong_color[i].x = c;
		hopalong_color[i].y = c2;
		hopalong_color[i].z = 0.f;
	}

	hopalong_fractal(hopalong_points, ITERATIONS_NUMBER, -1.5f, 2.2f, -1.4f, 0.1f);

	// Creating a window and initialize an opengl context
	GLFWwindow* window = opengl_window_create(1200, 900, "Hello world");
	scene = scene_create(camera_position, window);

	Vector3 background_color = { 0, 0, 0.2f };

	GLuint texture_shader = shader_create("./shaders/vertex_texture.glsl", "./shaders/fragment_texture.glsl");
	GLuint color_shader = shader_create("./shaders/vertex_color.glsl", "./shaders/fragment_color.glsl");

	GLuint lain_texture = texture_create(&lain_image, 1);
	image_destroy(&lain_image);
	
	Material* texture_material1 = material_create(texture_shader, NULL, 0);

	ArrayBufferDeclaration triangle1_buffers[] = {
		{triangle1_vertices, sizeof(triangle1_vertices), 3, 0, GL_STATIC_DRAW},
		{texture_coords, sizeof(texture_coords), 2, 1, GL_STATIC_DRAW}
	};

	Drawable* triangle1_drawable = scene_create_drawable(scene, NULL, 3, triangle1_buffers, 2, texture_material1, GL_TRIANGLES, &triangle1_shape.position, &lain_texture, 1, DRAWABLE_SHOW_AXIS);

	GLuint copland_os_texture = texture_create(&copland_os_image, 1);
	image_destroy(&copland_os_image);

	Material* texture_material2 = material_create(texture_shader, NULL, 0);

	ArrayBufferDeclaration triangle2_buffers[] = {
		{triangle2_vertices, sizeof(triangle2_vertices), 3, 0, GL_STATIC_DRAW},
		{texture_coords, sizeof(texture_coords), 2, 1, GL_STATIC_DRAW}
	};

	Drawable* triangle2_drawable = scene_create_drawable(scene, NULL, 3, triangle2_buffers, 2, texture_material2, GL_TRIANGLES, &triangle2_shape.position, &copland_os_texture, 1, DRAWABLE_SHOW_AXIS);

	Material* terrain_material = material_create(color_shader, NULL, 0);

	ArrayBufferDeclaration terrain_buffers[] = {
		{terrain_vertices, sizeof(Vector3) * TERRAIN_SIZE * TERRAIN_SIZE, 3, 0, GL_STATIC_DRAW},
		{terrain_color, sizeof(Vector3) * (TERRAIN_SIZE - 1) * (TERRAIN_SIZE - 1) * 6, 3, 1, GL_STATIC_DRAW}
	};

	Vector3 terrain_position = { 0.f, -5.f, 0.f };

	Drawable* terrain_drawable = scene_create_drawable(scene, terrain_indexes, (TERRAIN_SIZE - 1) * (TERRAIN_SIZE - 1) * 6, terrain_buffers, ARRAY_SIZE(terrain_buffers), terrain_material, GL_TRIANGLES, &terrain_position, NULL, 0, 0x0);
	Material* hopalong_material = material_create(color_shader, NULL, 0);

	ArrayBufferDeclaration hopalong_buffers[] = {
		{hopalong_points, sizeof(Vector3) * ITERATIONS_NUMBER, 3, 0, GL_STATIC_DRAW},
		{hopalong_color, sizeof(Vector3) * ITERATIONS_NUMBER, 3, 1, GL_STATIC_DRAW}
	};

	Vector3	hopalong_position = { 10.f, 0.f, 5.f };

	Drawable* hopalong_drawable = scene_create_drawable(scene, NULL, ITERATIONS_NUMBER, hopalong_buffers, ARRAY_SIZE(hopalong_buffers), hopalong_material, GL_POINTS, &hopalong_position, NULL, 0, 0x0);

	float window1_position[] = {
		0.5f, 0.2f
	};

	float window2_position[] = {
		0.1f, 0.2f
	};

	WindowID terrain_window = window_create(scene, 478.f, 237.f, window1_position, "EDIT TERRAIN");

	// Window 1's widgets
	Widget* terrain_presentation = widget_label_create(terrain_window, scene, NULL, "EDIT TERRAIN\n", 20.f, 0.f, black, LAYOUT_PACK);
	
	Widget* terrain_presentation_width = widget_label_create(terrain_window, scene, terrain_presentation, "WIDTH\n", 10.f, 0.f, green, LAYOUT_PACK);
	Widget* terrain_presentation_height = widget_label_create(terrain_window, scene, terrain_presentation, "HEIGHT\n", 10.f, 0.f, green, LAYOUT_PACK);

	Widget* button = widget_button_create(terrain_window, scene, terrain_presentation, "CONFIRM", 12.f, 0.f, 5.f, LAYOUT_PACK);

	widget_set_on_click(button, &click_callback);

	clock_t spf = (1.0 / 60.0) * (double)CLOCKS_PER_SEC;
	printf("Seconds per frame: %ld\n", spf);

	glfwPollEvents();

	clock_t start = clock();
	while (!glfwWindowShouldClose(window)) {

		scene_draw(scene, background_color);
		scene_handle_events(scene, window);

		glfwSwapBuffers(window);
		glfwPollEvents();

		if (glfwGetKey(window, GLFW_KEY_Q)) {
			world_update(physic_world, 0.01f);

			drawable_update(triangle1_drawable);
			drawable_update(triangle2_drawable);
		}

		clock_t end = clock();
		usleep(max(spf - (end - start), 0));

		start = clock();
	}

	glfwTerminate();

	printf("Goodbye!\n");
	return 0;
}
