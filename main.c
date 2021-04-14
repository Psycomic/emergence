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

extern int usleep (unsigned int __useconds);

#endif // __linux__

#include "misc.h"
#include "render.h"
#include "physics.h"
#include "images.h"
#include "noise.h"
#include "psyche.h"

#define WORLD_STEP 0.1f

#define TERRAIN_SIZE 255

#define POINTS_COUNT 100

#define ITERATIONS_NUMBER 32000
#define SUBSET_NUMBER 10
#define RINGS_NUMBER 3

float points[POINTS_COUNT * 2];
float* octaves[7];
float global_time = 0.f;

void execute_tests(void);

float terrain_noise(float x, float y) {
	return octavien_noise(octaves, 7, 4, x, y, 2.f, 0.7) * sinf(x * 10.f);
}

void character_callback(GLFWwindow* window, unsigned int codepoint) {
	Scene* scene = glfwGetWindowUserPointer(window);
	scene_character_callback(scene, codepoint);
}

void resize_callback(GLFWwindow* window, int width, int height) {
	Scene* scene = glfwGetWindowUserPointer(window);
	scene_resize_callback(scene, width, height);
	ps_resized(width, height);
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
	Scene* scene = glfwGetWindowUserPointer(window);
	scene_scroll_callback(scene, xoffset, yoffset);
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

static char* main_file_contents = NULL;

void new_window_callback(Widget* widget, Event* evt) {
	float new_window_position[] = {
		random_float() * 800 - 400.f,
		random_float() * 600 - 300.f
	};

	Window* new_window = window_create(scene, 500.f, 400.f, new_window_position, "NEW WINDOW");
	widget_label_create(new_window, scene, NULL,
						main_file_contents,
						15.f, 1.f, white, LAYOUT_PACK);
}

int main(void) {
	srand((uint)time(NULL));	// Seed for random number generation

	main_file_contents = read_file("lisp/core.ul");

	if (initialize_everything() != 0)
		return -1;

 #ifdef _DEBUG
	execute_tests();			// Unit tests
 #endif

	Image lain_image;
	Image copland_os_image;

	if (image_load_bmp(&copland_os_image, "./images/copland_os_enterprise.bmp") < 0)
		goto error;
	if (image_load_bmp(&lain_image, "./images/copland_os_enterprise.bmp") < 0)
		goto error;

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
	Shape triangle2_shape = { { { 0.f, 1.f, 0.5f } }, triangle2_vertices, NULL, 3, 3 };

	world_body_add(physic_world, &triangle1_shape, 0.f);
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
	if ((scene = scene_create(camera_position, 1200, 900, "Emergence")) == NULL)
		return -1;

	glfwSetCharCallback(scene->context, character_callback);
	glfwSetWindowSizeCallback(scene->context, resize_callback);
	glfwSetScrollCallback(scene->context, scroll_callback);

	GLuint texture_shader = shader_create("./shaders/vertex_texture.glsl", "./shaders/fragment_texture.glsl");
	GLuint color_shader = shader_create("./shaders/vertex_color.glsl", "./shaders/fragment_color.glsl");

	GLuint lain_texture, copland_os_texture;

	Material *texture_material1 = material_create(texture_shader, NULL, 0),
		*texture_material2 = material_create(texture_shader, NULL, 0);

	lain_texture = texture_create(&lain_image);
	copland_os_texture = texture_create(&copland_os_image);

	image_destroy(&lain_image);
	image_destroy(&copland_os_image);

	Vector3 background_color = { { 0, 0, 0.2f } };

	ArrayBufferDeclaration triangle1_buffers[] = {
		{ triangle1_vertices, sizeof(triangle1_vertices), 3, 0, GL_STATIC_DRAW },
		{ texture_coords, sizeof(texture_coords), 2, 1, GL_STATIC_DRAW }
	};

	ArrayBufferDeclaration triangle2_buffers[] = {
		{ triangle2_vertices, sizeof(triangle2_vertices), 3, 0, GL_STATIC_DRAW },
		{ texture_coords, sizeof(texture_coords), 2, 1, GL_STATIC_DRAW }
	};

	Drawable *triangle1_drawable = scene_create_drawable(scene, NULL, 3, triangle1_buffers, 2, texture_material1,
														 GL_TRIANGLES, &triangle1_shape.position, &lain_texture, 1,
														 DRAWABLE_SHOW_AXIS),
		*triangle2_drawable = scene_create_drawable(scene, NULL, 3, triangle2_buffers, 2,
													texture_material2, GL_TRIANGLES, &triangle2_shape.position,
													&copland_os_texture, 1, DRAWABLE_SHOW_AXIS);

	Material* terrain_material = material_create(color_shader, NULL, 0);

	ArrayBufferDeclaration terrain_buffers[] = {
		{terrain_vertices, sizeof(Vector3) * TERRAIN_SIZE * TERRAIN_SIZE, 3, 0, GL_STATIC_DRAW},
		{terrain_color, sizeof(Vector3) * (TERRAIN_SIZE - 1) * (TERRAIN_SIZE - 1) * 6, 3, 1, GL_STATIC_DRAW}
	};

	Vector3 terrain_position = { { 0.f, -5.f, 0.f } };

	scene_create_drawable(scene, terrain_indexes, (TERRAIN_SIZE - 1) * (TERRAIN_SIZE - 1) * 6,
						  terrain_buffers, ARRAY_SIZE(terrain_buffers), terrain_material, GL_TRIANGLES,
						  &terrain_position, NULL, 0, 0x0);

	Material* hopalong_material = material_create(color_shader, NULL, 0);

	ArrayBufferDeclaration hopalong_buffers[] = {
		{hopalong_points, sizeof(Vector3) * ITERATIONS_NUMBER, 3, 0, GL_STATIC_DRAW},
		{hopalong_color, sizeof(Vector3) * ITERATIONS_NUMBER, 3, 1, GL_STATIC_DRAW}
	};

	Vector3	hopalong_position = { { 10.f, 0.f, 5.f } };
	hopalong_drawable = scene_create_drawable(scene, NULL, ITERATIONS_NUMBER, hopalong_buffers,
											  ARRAY_SIZE(hopalong_buffers), hopalong_material, GL_POINTS,
											  &hopalong_position, NULL, 0, 0x0);

	float window1_position[] = { 0.5f, 0.2f };
	float window2_position[] = { 0.1f, 0.2f };

	Window* terrain_window = window_create(scene, 478.f, 237.f, window1_position, "Edit fractal");

	Widget* terrain_presentation = widget_label_create(terrain_window, scene, NULL,
													   "Edit the hopalong fractal\n", 30.f, 0.f, red, LAYOUT_PACK);

	terrain_presentation_label = widget_label_create(terrain_window, scene, terrain_presentation,
													 "This is a computer-generated fractal\n"
													 "called the hopalong fractal",
													 20.f, 5.f, white, LAYOUT_PACK);

	Widget* randomize_button = widget_button_create(terrain_window, scene, terrain_presentation, "Randomize",
													20.f, 5.f, 5.f, LAYOUT_PACK);
	Widget* quit_button = widget_button_create(terrain_window, scene, terrain_presentation, "Quit game",
											   20.f, 5.f, 5.f, LAYOUT_PACK);
	Widget* new_window_button = widget_button_create(terrain_window, scene, terrain_presentation, "New window",
													 20.f, 5.f, 5.f, LAYOUT_PACK);

	widget_set_on_click_up(randomize_button, update_callback);
	widget_set_on_click_up(quit_button, quit_callback);
	widget_set_on_click_up(new_window_button, new_window_callback);

	Window* test_window = window_create(scene, 400.f, 200.f, window2_position, "Hello, world!");
	Widget* test_title = widget_label_create(test_window, scene, NULL, "This is a big title!", 30.f, 0.f, green, LAYOUT_PACK);

	Widget* test_text = widget_label_create(test_window, scene, NULL,
											"This is some random text to make\n"
											"this window stand out",
											20.f, 5.f, black, LAYOUT_PACK);

	Widget* test_button = widget_button_create(test_window, scene, test_text, "Test button", 20.f, 5.f, 5.f, LAYOUT_PACK);

	clock_t spf = (1.0 / 60.0) * (double)CLOCKS_PER_SEC;

	glfwPollEvents();

	GLFWwindow* window = scene_context(scene);

	DynamicArray frames;
	DYNAMIC_ARRAY_CREATE(&frames, clock_t);

	uint64_t count = 0;

	ps_init((Vector2) { { 1200, 900 } });

	clock_t start = clock();
	clock_t fps = 0;

	while (!scene_should_close(scene)) {
		scene_draw(scene, background_color);

		ps_begin_path();		/* Red image */
		ps_line_to(100, 100);
		ps_line_to(450, 100);
		ps_line_to(450, 450);
		ps_line_to(100, 450);
		ps_close_path();

 		ps_fill((Vector4){ { 1.f, 1.f, 1.f, 1.f } }, PS_FILLED_POLY);

		ps_begin_path();		// Blue circle
		for (float i = 0.f; i < M_PI * 2; i += 0.1f)
			ps_line_to(cosf(i) * 100 - 100, sinf(i) * 100 - 100);
		ps_close_path();

 		ps_fill((Vector4){ { 0.f, 0.f, 1.f, 1.f } }, PS_FILLED_POLY);

		char buf[256];
		snprintf(buf, sizeof(buf), "%lu FPS", fps);

		ps_text(buf, (Vector2) { { 0.f, 0.f } }, 30.f, (Vector4){ { 1.f, 1.f, 1.f, 1.f } });

		ps_render();

		scene_handle_events(scene, window);

		glfwSwapBuffers(window);
		glfwPollEvents();

		if (glfwGetKey(window, GLFW_KEY_Q)) {
			world_update(physic_world, 0.01f);

			drawable_update(triangle1_drawable);
			drawable_update(triangle2_drawable);
		}

		clock_t end = clock();
		clock_t* delta = dynamic_array_push_back(&frames, 1);
		*delta = end - start;

		global_time += ((float)*delta) / CLOCKS_PER_SEC;

		if (count++ % 10 == 0)
			fps = CLOCKS_PER_SEC / *delta;

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

error:
	printf("Something failed...\n");
	return -1;
}
