#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <string.h>

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

#define TERRAIN_SIZE 100

#define POINTS_COUNT 50

float points[POINTS_COUNT * 2];

float terrain_noise(float x, float y) {
	float positon[2] = {
		x, y
	};

	return voronoi_noise(2, points, POINTS_COUNT, positon, &cellular_noise);
}

int main(void) {
	srand((uint)time(NULL));  // Seed for random number generation

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

	for (uint i = 0; i < (TERRAIN_SIZE - 1) * (TERRAIN_SIZE - 1) * 6; i++) {
		terrain_color[i].x = random_float();
		terrain_color[i].y = 0.f;
		terrain_color[i].z = random_float();
	}

	terrain_create(terrain_vertices, TERRAIN_SIZE, 5.f, 10.f, &terrain_noise);
	terrain_elements(terrain_indexes, TERRAIN_SIZE);

	// Creating a window and initialize an opengl context
	GLFWwindow* window = opengl_window_create(1200, 900, "Hello world");
	Scene* scene = scene_create(camera_position, window);

	Vector3 background_color = { 0, 0, 0.2f };

	GLuint texture_shader = shader_create("./shaders/vertex_texture.glsl", "./shaders/fragment_texture.glsl");
	GLuint color_shader = shader_create("./shaders/vertex_color.glsl", "./shaders/fragment_color.glsl");

	GLuint lain_texture = texture_create(&lain_image, 1);
	image_destroy(&lain_image);
	
	Material* texture_material1 = material_create(texture_shader, 0, NULL);

	ArrayBufferDeclaration triangle1_buffers[] = {
		{triangle1_vertices, sizeof(triangle1_vertices), 3, 0},
		{texture_coords, sizeof(texture_coords), 2, 1}
	};

	Drawable* triangle1_drawable = drawable_create(scene, NULL, 3, triangle1_buffers, 2, texture_material1, GL_TRIANGLES, &triangle1_shape.position, &lain_texture, 1, DRAWABLE_SHOW_AXIS);

	GLuint copland_os_texture = texture_create(&copland_os_image, 1);
	image_destroy(&copland_os_image);

	Material* texture_material2 = material_create(texture_shader, 0, NULL);

	ArrayBufferDeclaration triangle2_buffers[] = {
		{triangle2_vertices, sizeof(triangle2_vertices), 3, 0},
		{texture_coords, sizeof(texture_coords), 2, 1}
	};

	Drawable* triangle2_drawable = drawable_create(scene, NULL, 3, triangle2_buffers, 2, texture_material2, GL_TRIANGLES, &triangle2_shape.position, &copland_os_texture, 1, DRAWABLE_SHOW_AXIS);

	Material* terrain_material = material_create(color_shader, NULL, 0);

	ArrayBufferDeclaration terrain_buffers[] = {
		{terrain_vertices, sizeof(Vector3) * TERRAIN_SIZE * TERRAIN_SIZE, 3, 0},
		{terrain_color, sizeof(Vector3) * (TERRAIN_SIZE - 1) * (TERRAIN_SIZE - 1) * 6, 3, 1}
	};

	Vector3 terrain_position = { 0.f, -5.f, 0.f };

	Drawable* terrain_drawable = drawable_create(scene, terrain_indexes, (TERRAIN_SIZE - 1) * (TERRAIN_SIZE - 1) * 6, terrain_buffers, ARRAY_SIZE(terrain_buffers), terrain_material, GL_TRIANGLES, &terrain_position, NULL, 0, 0x0);

	float window1_position[] = {
		0.5f, 0.2f
	};

	float window2_position[] = {
		0.1f, 0.2f
	};

	Window* window1 = window_create(scene, 0.5f, 0.5f, window1_position, "DANTE INFERNO");
	Window* window2 = window_create(scene, 0.4f, 0.3f, window2_position, "FILES");

	Vector3 white = { 1, 1, 1 };
	Vector3 red = { 1, 0, 0 };
	Vector3 blue = { 0, 0, 1 };
	Vector3 black = { 0, 0, 0 };
	Vector3 green = { 0, 1, 0 };

	// Window 1's widgets
	Widget* label1 = widget_label_create(window1, NULL,
		"THROUGH ME THE WAY INTO THE SUFFERING CITY\n"
		"THROUGH ME THE WAY TO THE ETERNAL PAIN\n"
		"THROUGH ME THE WAY THAT RUNS AMONG THE LOST\n\n"

		"JUSTICE URGED ON MY HIGH ARTIFICER\n"
		"MY MAKER WAS DIVINE AUTHORITY\n"
		"THE HIGHEST WISDOM AND THE PRIMAL LOVE\n\n"

		"BEFORE ME NOTHING BUT ETERNAL THINGS\n"
		"WERE MADE AND I ENDURE ETERNALLY\n"
		"ABANDON EVERY HOPE WHO ENTER HERE",
		10.f, 7.f, black, LAYOUT_PACK);

	Widget* label2 = widget_label_create(window1, label1,
		"THREE RINGS FOR THE ELVEN KINGS UNDER THE SKY\n"
		"SEVEN FOR THE DWARF LORDS IN THEIR HALLS OF STONE\n"
		"NINE FOR MORTAL MEN DOOMED TO DIE\n"
		"ONE FOR THE DARK LORD ON HIS DARK THRONE\n"
		"IN THE LAND OF MORDOR WHERE THE SHADOWS LIE\n"
		"ONE RING TO RULE THEM ALL ONE RING TO FIND THEM\n"
		"ONE RING TO BRING THEM ALL AND IN THE DARKNESS BIND THEM\n"
		"IN THE LAND OF MORDOR WHERE THE SHADOWS LIE",
		10.f, 7.f, red, LAYOUT_PACK);

	Widget* button1 = widget_button_create(window1, NULL, "CLICK ME", 10.f, 7.f, 5.f, LAYOUT_PACK);
	Widget* button2 = widget_button_create(window1, label2, "CANCEL", 10.f, 7.f, 5.f, LAYOUT_PACK);
	
	Widget* label4 = widget_label_create(window1, label1, "OK", 10.f, 7.f, blue, LAYOUT_PACK);

	// Window 2's widgets
	Widget* label5 = widget_label_create(window2, NULL, "HELLO WORLD\nFUCK YOU", 10.f, 4.f, red, LAYOUT_PACK);

	clock_t spf = (1.0 / 60.0) * (double)CLOCKS_PER_SEC;
	printf("Seconds per frame: %d\n", spf);

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
