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

#define ITERATIONS_NUMBER 32000
#define SUBSET_NUMBER 10
#define RINGS_NUMBER 3

float points[POINTS_COUNT * 2];
float* octaves[7];

void execute_tests(void);

float terrain_noise(float x, float y) {
	return octavien_noise(octaves, 7, 4, x, y, 2.f, 0.7) * sinf(x * 10.f);
}

static Scene* scene;
static Vector3* hopalong_points;
static Vector3* hopalong_color;
static Drawable* hopalong_drawable;
static Widget* terrain_presentation_label;

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
}

void update_callback(Widget* widget, Event* evt) {
	update_fractal();

	drawable_update(hopalong_drawable);
}

void quit_callback(Widget* widget, Event* evt) {
	scene_quit(scene);
}

void new_window_callback(Widget* widget, Event* evt) {
	float new_window_position[] = {
		random_float() * 800 - 400.f,
		random_float() * 600 - 300.f
	};
	Window* new_window = window_create(scene, 500.f, 400.f, new_window_position, "NEW WINDOW");
	widget_label_create(new_window, scene, NULL,
						"LOREM IPSUM DOLOR SIT\n"
						"AMET CONSECTETUR ADIPISCING\n"
						"ELIT PROIN TEMPUS PRETIUM\n"
						"SAGITTIS MORBI AT NUNC EU\n"
						"EST LAOREET SCELERISQUE PELLENTESQUE\n"
						"SED FACILISIS SAPIEN\n"
						"DUIS VEL DOLOR IN\n"
						"ODIO CONVALLIS SODALES CRAS SEMPER\n"
						"DUI PURUS NAM AUCTOR\n"
						"CONSEQUAT LIGULA EGET\n"
						"LAOREET ERAT MAXIMUS IN\n"
						"UT SED EX LOREM ETIAM\n"
						"TINCIDUNT MATTIS TURPIS QUIS\n"
						"ORNARE NISL TINCIDUNT\n"
						"AC PROIN AT ELIT\n"
						"ELEIFEND INTERDUM METUS A RHONCUS\n"
						"SAPIEN VIVAMUS AT ELEIFEND\n"
						"NISI NULLAM A\n"
						"SCELERISQUE JUSTO",
						15.f, 1.f, white, LAYOUT_PACK);
}

int main(void) {
	srand((uint)time(NULL));	// Seed for random number generation

	if (initialize_everything() != 0)
		return -1;

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

	Vector3 camera_position = { { 0.f, 0.f, 0.f } };

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
	World* physic_world = world_create(gravity, 10);

	Shape triangle1_shape = { { { 0.f, 0.f, 0.f } }, triangle1_vertices, NULL, 3, 3, };
	world_body_add(physic_world, &triangle1_shape, 0.f);

	Shape triangle2_shape = { { { 0.f, 1.f, 0.5f } }, triangle2_vertices, NULL, 3, 3 };
	world_body_add(physic_world, &triangle2_shape, 1.f);

	Vector3* terrain_vertices = malloc(sizeof(Vector3) * TERRAIN_SIZE * TERRAIN_SIZE);
	unsigned short* terrain_indexes = malloc(sizeof(unsigned short) * (TERRAIN_SIZE - 1) * (TERRAIN_SIZE - 1) * 6);
	Vector3* terrain_color = malloc(sizeof(Vector3) * (TERRAIN_SIZE - 1) * (TERRAIN_SIZE - 1) * 6);

	random_arrayf(points, 2 * POINTS_COUNT);

	octavien_initialize_gradient(octaves, 4, TERRAIN_SIZE, 2.f);

	terrain_create(terrain_vertices, TERRAIN_SIZE, 10.f, 40.f, &terrain_noise);
	terrain_elements(terrain_indexes, TERRAIN_SIZE);

	for (uint i = 0; i < (TERRAIN_SIZE - 1) * (TERRAIN_SIZE - 1) * 6; i++) {
		float height = (terrain_vertices[terrain_indexes[i]].y + 2.f) / 7.f;

		terrain_color[terrain_indexes[i]].x = height;
		terrain_color[terrain_indexes[i]].y = 0.f;
		terrain_color[terrain_indexes[i]].z = 1.f;
	}

	hopalong_points = malloc(sizeof(Vector3) * ITERATIONS_NUMBER);
	hopalong_color = malloc(sizeof(Vector3) * ITERATIONS_NUMBER);

	update_fractal();

	// Creating a window and initialize an opengl context
	if ((scene = scene_create(camera_position, 800, 600, "Emergence")) == NULL)
		return -1;

	Vector3 background_color = { { 0, 0, 0.2f } };

	GLuint texture_shader = shader_create("./shaders/vertex_texture.glsl", "./shaders/fragment_texture.glsl");
	GLuint color_shader = shader_create("./shaders/vertex_color.glsl", "./shaders/fragment_color.glsl");

	GLuint lain_texture = texture_create(&lain_image);
	image_destroy(&lain_image);

	Material* texture_material1 = material_create(texture_shader, NULL, 0);

	ArrayBufferDeclaration triangle1_buffers[] = {
		{triangle1_vertices, sizeof(triangle1_vertices), 3, 0, GL_STATIC_DRAW},
		{texture_coords, sizeof(texture_coords), 2, 1, GL_STATIC_DRAW}
	};

	Drawable* triangle1_drawable = scene_create_drawable(scene, NULL, 3, triangle1_buffers, 2, texture_material1, GL_TRIANGLES, &triangle1_shape.position, &lain_texture, 1, DRAWABLE_SHOW_AXIS);

	GLuint copland_os_texture = texture_create(&copland_os_image);
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

	Vector3 terrain_position = { { 0.f, -5.f, 0.f } };

	scene_create_drawable(scene, terrain_indexes, (TERRAIN_SIZE - 1) * (TERRAIN_SIZE - 1) * 6, terrain_buffers, ARRAY_SIZE(terrain_buffers), terrain_material, GL_TRIANGLES, &terrain_position, NULL, 0, 0x0);
	Material* hopalong_material = material_create(color_shader, NULL, 0);

	ArrayBufferDeclaration hopalong_buffers[] = {
		{hopalong_points, sizeof(Vector3) * ITERATIONS_NUMBER, 3, 0, GL_STATIC_DRAW},
		{hopalong_color, sizeof(Vector3) * ITERATIONS_NUMBER, 3, 1, GL_STATIC_DRAW}
	};

	Vector3	hopalong_position = { { 10.f, 0.f, 5.f } };

	hopalong_drawable = scene_create_drawable(scene, NULL, ITERATIONS_NUMBER, hopalong_buffers, ARRAY_SIZE(hopalong_buffers), hopalong_material, GL_POINTS, &hopalong_position, NULL, 0, 0x0);

	float window1_position[] = {
		0.5f, 0.2f
	};

	float window2_position[] = {
		0.1f, 0.2f
	};

	Window* terrain_window = window_create(scene, 478.f, 237.f, window1_position, "EDIT FRACTAL");

	// Window 1's widgets
	Widget* terrain_presentation = widget_label_create(terrain_window, scene, NULL,
													   "EDIT THE HOPALONG\nFRACTAL\n", 20.f, 0.f, red, LAYOUT_PACK);

	terrain_presentation_label = widget_label_create(terrain_window, scene, terrain_presentation,
		"THIS IS A COMPUTER GENERATED FRACTAL\nUSING THE HOPALONG FORMULA\n", 10.f, 0.f, white, LAYOUT_PACK);

	Widget* randomize_button = widget_button_create(terrain_window, scene, terrain_presentation, "RANDOMIZE", 12.f,
													5.f, 5.f, LAYOUT_PACK);
	Widget* quit_button = widget_button_create(terrain_window, scene, terrain_presentation, "QUIT GAME",
											   12.f, 5.f, 5.f, LAYOUT_PACK);
	Widget* new_window_button = widget_button_create(terrain_window, scene, terrain_presentation, "NEW WINDOW",
													 12.f, 5.f, 5.f, LAYOUT_PACK);

	widget_set_on_click_up(randomize_button, update_callback);
	widget_set_on_click_up(quit_button, quit_callback);
	widget_set_on_click_up(new_window_button, new_window_callback);

	Window* test_window = window_create(scene, 400.f, 200.f, window2_position, "HELLO WORLD");
	Widget* test_title = widget_label_create(test_window, scene, NULL, "THIS IS A BIG TITLE", 25.f, 0.f, green, LAYOUT_PACK);
	Widget* test_text = widget_label_create(test_window, scene, NULL,
											"THIS IS SOME RANDOM\n"
											"TEXT TO MAKE THIS WINDOW\n"
											"STAND OUT",
											15.f, 5.f, black, LAYOUT_PACK);

	Widget* test_button = widget_button_create(test_window, scene, test_text, "TEST BUTTON", 12.f, 5.f, 5.f, LAYOUT_PACK);

	clock_t spf = (1.0 / 60.0) * (double)CLOCKS_PER_SEC;

	glfwPollEvents();

	GLFWwindow* window = scene_context(scene);

	DynamicArray frames;
	DYNAMIC_ARRAY_CREATE(&frames, clock_t);

	uint64_t count = 0;

	clock_t start = clock();
	while (!scene_should_close(scene)) {
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
		clock_t* delta = dynamic_array_push_back(&frames);
		*delta = end - start;

		if (count++ % 10 == 0)
			printf("%ld FPS\n", CLOCKS_PER_SEC / *delta);

		clock_t wait_time = max(spf - *delta, 0);
		usleep(wait_time);

		start = clock();
	}

	clock_t average_frame = 0;

	for (uint i = 0; i < frames.size; i++)
		average_frame += *((clock_t*)dynamic_array_at(&frames, i));

	float average_frame_duration = average_frame / frames.size,
		average_fps =  CLOCKS_PER_SEC / average_frame_duration;

	printf("Average frame time: %f\nAverage FPS: %f\n", average_frame_duration, average_fps);

	glfwTerminate();

	return 0;
}
