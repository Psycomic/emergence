#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <string.h>

#include "misc.h"
#include "render.h"
#include "physics.h"
#include "images.h"

#define WORLD_STEP 0.1f
#define MAX_CUBES_NUMBER 500

#define CUBE_SIZE 50

int main(void) {
	srand((uint)time(NULL));  // Seed for random number generation

	Image copland_os_image;
	if (image_load_bmp(&copland_os_image, "./images/copland_os_enterprise.bmp") >= 0)
		printf("Success loading copland !\n");
	else
		printf("Error when loading image !\n");

	Image lain_image;
	if (image_load_bmp(&lain_image, "./images/lain.bmp") >= 0)
		printf("Success loading lain !\n");
	else
		printf("Error when loading image !\n");

	Image lain_image_png;
	if (image_load_png(&lain_image_png, "./images/copland_os_enterprise.png") >= 0)
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

	// Creating a window and initialize an opengl context
	GLFWwindow* window = opengl_window_create(800, 800, "Hello world");
	Scene* scene = scene_create(camera_position);

	Vector3 background_color = { 0, 0, 0.2 };

	GLuint texture_shader = shader_create("./shaders/vertex_shader_texture.glsl", "./shaders/fragment_shader_texture.glsl");

	GLuint lain_texture = texture_create(&lain_image, 1);
	image_destroy(&lain_image);

	Material* texture_material1 = material_create(texture_shader, 0, NULL);

	ArrayBufferDeclaration triangle1_buffers[] = {
		{triangle1_vertices, sizeof(triangle1_vertices), 3, 0},
		{texture_coords, sizeof(texture_coords), 2, 1}
	};

	Drawable* triangle1_drawable = drawable_create(scene, NULL, 3, triangle1_buffers, 2, texture_material1, &triangle1_shape.position, &lain_texture, 1, DRAWABLE_SHOW_AXIS);

	GLuint copland_os_texture = texture_create(&copland_os_image, 1);
	image_destroy(&copland_os_image);

	Material* texture_material2 = material_create(texture_shader, 0, NULL);

	ArrayBufferDeclaration triangle2_buffers[] = {
		{triangle2_vertices, sizeof(triangle2_vertices), 3, 0},
		{texture_coords, sizeof(texture_coords), 2, 1}
	};

	Drawable* triangle2_drawable = drawable_create(scene, NULL, 3, triangle2_buffers, 2, texture_material2, &triangle2_shape.position, &copland_os_texture, 1, DRAWABLE_SHOW_AXIS);

	float window1_position[] = {
		0.5f, 0.2f
	};

	float window2_position[] = {
		0.1f, 0.2f
	};

	Window* window1 = window_create(scene, 0.5f, 0.5f, window1_position, "NAVIGATOR");
	Window* window2 = window_create(scene, 0.4f, 0.3f, window2_position, "FILES");

	Vector3 label_color = { 1, 1, 1 };

	Widget* label = widget_label_create(window1, NULL, 0.f, 0.f, 
		"THROUGH ME THE WAY INTO THE SUFFERING CITY\n"
		"THROUGH ME THE WAY TO THE ETERNAL PAIN\n"
		"THROUGH ME THE WAY THAT RUNS AMONG THE LOST\n\n"

		"JUSTICE URGED ON MY HIGH ARTIFICER\n"
		"MY MAKER WAS DIVINE AUTHORITY\n"
		"THE HIGHEST WISDOM, AND THE PRIMAL LOVE\n\n"

		"BEFORE ME NOTHING BUT ETERNAL THINGS\n"
		"WERE MADE, AND I ENDURE ETERNALLY\n"
		"ABANDON EVERY HOPE, WHO ENTER HERE\n\n"


		"I NEED TO WRITE TEXT TO FILL THIS UP\n"
		"CAN ANYONE HELP ME\n"
		"I DONT THINK ANYONE HAS EVER THOUGHT OF IT\n"
		"PROBABLY ANOTHER PROBLEM\n"
		"BUGS EVERYWHERE.\n",
		0.03f, label_color, 0.01f, WIDGET_LAYOUT_PACK);

	Vector3 label2_color = { 1, 0, 0 };

	Widget* label2 = widget_label_create(window1, NULL, 0.f, 0.f,
		"HEY YOU !\n\n"
		"HOW ARE YOU DOING ?\n"
		"HEY\n",
		0.03f, label2_color, 0.01f, WIDGET_LAYOUT_PACK);

	while (!glfwWindowShouldClose(window)) {
		scene_draw(scene, background_color);

		glfwSwapBuffers(window);
		glfwPollEvents();

		scene_handle_events(scene, window);

		if (glfwGetKey(window, GLFW_KEY_Q)) {
			world_update(physic_world, 0.01f);

			drawable_update(triangle1_drawable);
			drawable_update(triangle2_drawable);
		}
	}

	glfwTerminate();

	printf("Goodbye!\n");
	return 0;
}
