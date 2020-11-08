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

	// Creating a window and initialize an opengl context
	GLFWwindow* window = opengl_window_create(1200, 900, "Hello world");
	Scene* scene = scene_create(camera_position, window);

	Vector3 background_color = { 0, 0, 0.2f };

	GLuint texture_shader = shader_create("./shaders/vertex_texture.glsl", "./shaders/fragment_texture.glsl");

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

	clock_t spf = (1.0 / 60.0) * (double)CLOCKS_PER_SEC;
	printf("Seconds per frame: %d\n", spf);

	while (!glfwWindowShouldClose(window)) {
		clock_t start = clock();
		scene_draw(scene, background_color);

		glfwSwapBuffers(window);
		glfwPollEvents();

		scene_handle_events(scene, window);

		if (glfwGetKey(window, GLFW_KEY_Q)) {
			world_update(physic_world, 0.01f);

			drawable_update(triangle1_drawable);
			drawable_update(triangle2_drawable);
		}

		clock_t end = clock();
		usleep(max(spf - (end - start), 0));
	}

	glfwTerminate();

	printf("Goodbye!\n");
	return 0;
}
