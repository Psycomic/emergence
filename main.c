/* Date of creation:
 * Sun Jun 14 09:30:50 2020 */

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <locale.h>

#include "misc.h"
#include "render.h"
#include "physics.h"
#include "images.h"
#include "noise.h"
#include "psyche.h"
#include "random.h"
#include "window.h"
#include "workers.h"
#include "protocol7.h"
#include "yuki.h"

#define WORLD_STEP 0.1f

#define TERRAIN_SIZE 500

#define POINTS_COUNT 100

#define ITERATIONS_NUMBER 32000
#define SUBSET_NUMBER 10
#define RINGS_NUMBER 3

static float points[POINTS_COUNT * 2];
static Octaves octaves;
static BOOL octaves_initialized = GL_FALSE;

char little_endian;
char* locale;

float global_time = 0.f;

char bible[] = "At the beggining, there was darkness.\n"
	"And god created light, and light was.\n"
	"And he saw that light was good, so he created the day";

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

static float hopalong_a = 0.5f,
	hopalong_b = -0.2f,
	hopalong_c = 0.96f;

static Vector3 hopalong_subsets[SUBSET_NUMBER];

void** stack_end;

void update_fractal() {
	static const float scale = 4.f;

	float a = hopalong_a * scale,
		b = hopalong_b * scale,
		c = hopalong_c * scale;

	hopalong_fractal(hopalong_points, ITERATIONS_NUMBER, a, b, c, 0.1f);

	for (uint i = 0; i < ITERATIONS_NUMBER; i++)
		hopalong_color[i] = hopalong_subsets[i * SUBSET_NUMBER / ITERATIONS_NUMBER];

	drawable_update(hopalong_drawable);
}

static Vector3* terrain_color;
static uint* terrain_indexes;
static Vector3* terrain_vertices;
static Drawable* terrain_drawable = NULL;
static float terrain_frequency = 2.f,
	terrain_amplitude = 0.5f;

int update_terrain(WorkerData* data) {
	if (octaves_initialized)
		octaves_destroy(&octaves);

	octaves_init(&octaves, 8, 5, terrain_frequency, terrain_amplitude);

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

	octaves_initialized = GL_TRUE;

	return 0;
}

static World* physic_world;
static Drawable* triangle1_drawable;
static Drawable* triangle2_drawable;
static Vector3 background_color = { { 0, 0, 0.2f } };
static PsWidget* randomize_btn;
static PsWidget* regenerate_button;
static PsWidget* eval_button;
static PsWidget* lisp_input;
static PsWidget* result_label;
static PsWidget* wireframe_button;
static Worker* terrain_worker = NULL;

void update() {
	p7_loop();
	scene_draw(scene, background_color);

	if (ps_widget_state(randomize_btn) & PS_WIDGET_CLICKED) {
		random_arrayf((float*)&hopalong_subsets, SUBSET_NUMBER * 3);
		hopalong_a = clampf(gaussian_random(), -1.f, 1.f);
		hopalong_b = clampf(gaussian_random(), -1.f, 1.f);
		hopalong_c = clampf(gaussian_random(), -1.f, 1.f);

		update_fractal();
	}

	if (ps_widget_state(wireframe_button) & PS_WIDGET_CLICKED) {
		scene_toggle_wireframe(scene);
	}

	if (terrain_worker != NULL && worker_finished(terrain_worker)) {
		printf("Finished with code %d!\n", worker_return_code(terrain_worker));
		drawable_update(terrain_drawable);
		terrain_worker = NULL;
	}

	if (ps_widget_state(regenerate_button) & PS_WIDGET_CLICKED) {
		if (terrain_worker == NULL) {
			terrain_worker = worker_create(update_terrain, NULL);
			printf("Started worker!\n");
		}
		else {
			printf("Already started!\n");
		}
	}

	if (ps_widget_state(eval_button) & PS_WIDGET_CLICKED) {
		YkObject forms = YK_NIL, stream = YK_NIL, bytecode = YK_NIL, r = YK_NIL;
		YK_GC_PROTECT2(forms, stream);

		forms = yk_read(ps_input_value(lisp_input));

		bytecode = yk_make_bytecode_begin(yk_make_symbol_cstr("input"), 0);
		YkObject error = yk_compile(forms, bytecode);

		stream = yk_make_output_string_stream();

		if (error == YK_NIL) {
			yk_run(bytecode);
			r = yk_value_register;

			YK_DLET_BEGIN(yk_var_output, stream);
			yk_print(r);
			YK_DLET_END;

			ps_label_set_text(result_label, yk_string_to_c_str(yk_stream_string(stream)));
		} else {
			YK_DLET_BEGIN(yk_var_output, stream);
			yk_print(error);
			YK_DLET_END;
			ps_label_set_text(result_label, yk_string_to_c_str(yk_stream_string(stream)));
		}

		YK_GC_UNPROTECT;
	}

	ps_render(scene->flags & SCENE_GUI_MODE);
	scene_handle_events(scene);

	if (g_window.keys[GLFW_KEY_Q] && !(scene->flags & SCENE_GUI_MODE)) {
		world_update(physic_world, 0.01f);

		drawable_update(triangle1_drawable);
		drawable_update(triangle2_drawable);
	}
}

void setup() {
	scene = scene_create((Vector3) { { 0.f, 0.f, 0.f } });
	ps_init();
}

static Vector4 canvas_draw_color = { { 1.f, 0.f, 0.f, 1.f } };

void canvas_draw(PsWidget* canvas, Vector2 anchor, Vector2 size) {
	float x = anchor.x + 10,
		y = anchor.y + 20;

	x = g_window.cursor_position.x;
	y = g_window.cursor_position.y;

	ps_fill_rect(x, y, 100, 100, canvas_draw_color);
	ps_text("Hello, world!", (Vector2) { { x, y } }, 14.f, (Vector4) { { 1.f, 1.f, 1.f, 1.f } });

	ps_begin_path();
	ps_line_to(x, y);
	ps_line_to(x - 30.f, y - 60.f);
	ps_line_to(x, y - 120.f);
	ps_line_to(x + 30.f, y - 60.f);
	ps_close_path();
	ps_fill((Vector4) { { 1.f, 1.f, 1.f, 1.f } }, PS_FILLED_POLY);
}

void toggle_wireframe(void* data) {
	ps_toggle_wireframe();
}

int do_main(int argc, char** argv) {
	locale = setlocale(LC_ALL, "");

	ushort a = 0xeeff;
	uchar* a_ptr = (uchar*)&a;
	if (a_ptr[0] == 0xff)
		little_endian = 1;
	else
		little_endian = 0;

#ifdef _WIN32
	SetConsoleOutputCP(65001);
#endif

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

	terrain_vertices = malloc(sizeof(Vector3) * TERRAIN_SIZE * TERRAIN_SIZE);
	terrain_indexes = malloc(sizeof(uint) * (TERRAIN_SIZE - 1) * (TERRAIN_SIZE - 1) * 6);
	terrain_color = malloc(sizeof(Vector3) * (TERRAIN_SIZE - 1) * (TERRAIN_SIZE - 1) * 6);

	random_arrayf(points, 2 * POINTS_COUNT);

	update_terrain(NULL);

	hopalong_points = malloc(sizeof(Vector3) * ITERATIONS_NUMBER);
	hopalong_color = malloc(sizeof(Vector3) * ITERATIONS_NUMBER);

	if (window_create(1200, 800, "Emergence", setup, update) < 0)
		goto error;

	random_init();

	execute_tests();			// Unit tests

	window_add_resize_hook(scene_resize_callback, scene);
	ps_add_global_binding(key_create('w', KEY_MOD_ALT), toggle_wireframe, NULL);

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
	terrain_drawable = scene_create_drawable(scene, terrain_indexes, (TERRAIN_SIZE - 1) * (TERRAIN_SIZE - 1) * 6, terrain_buffers, ARRAY_SIZE(terrain_buffers), terrain_material, GL_TRIANGLES, &terrain_position, NULL, 0, 0x0);

	Material* hopalong_material = material_create(color_shader, NULL, 0);
	ArrayBufferDeclaration hopalong_buffers[] = {
		{hopalong_points, sizeof(Vector3) * ITERATIONS_NUMBER, 3, 0, GL_STATIC_DRAW},
		{hopalong_color, sizeof(Vector3) * ITERATIONS_NUMBER, 3, 1, GL_STATIC_DRAW}
	};

	Vector3	hopalong_position = { { 10.f, 0.f, 5.f } };
	hopalong_drawable = scene_create_drawable(scene, NULL, ITERATIONS_NUMBER, hopalong_buffers, ARRAY_SIZE(hopalong_buffers), hopalong_material, GL_POINTS, &hopalong_position, NULL, 0, 0x0);

	random_arrayf((float*)&hopalong_subsets, SUBSET_NUMBER * 3);
	update_fractal();

	PsWindow* hopalong_window = ps_window_create("Hopalong fractal");
	PsWidget* vbox = ps_box_create(PS_DIRECTION_VERTICAL, 5);

	ps_window_set_root(hopalong_window, vbox);

	PsWidget* label = ps_label_create("Variable A:", 15);
	PsWidget* slider = ps_slider_create(&hopalong_a, -2.f, 2.f, 16, update_fractal);

	ps_container_add(vbox, label);
	ps_container_add(vbox, slider);

	label = ps_label_create("Variable B:", 15);
	slider = ps_slider_create(&hopalong_b, -2.f, 2.f, 16, update_fractal);

	ps_container_add(vbox, label);
	ps_container_add(vbox, slider);

	label = ps_label_create("Variable C:", 15);
	slider = ps_slider_create(&hopalong_c, -2.f, 2.f, 16, update_fractal);

	ps_container_add(vbox, label);
	ps_container_add(vbox, slider);

	randomize_btn = ps_button_create("Randomize fractal", 16);

	PsWindow* terrain_window = ps_window_create("Terrain");
	vbox = ps_box_create(PS_DIRECTION_VERTICAL, 5);

	ps_window_set_root(terrain_window, vbox);

	slider = ps_slider_create(&terrain_frequency, 1.f, 2.f, 14, NULL);
	ps_container_add(vbox, slider);

	slider = ps_slider_create(&terrain_amplitude, 0.f, 1.f, 14, NULL);
	ps_container_add(vbox, slider);

	regenerate_button = ps_button_create("Re-generate terrain", 16);
	wireframe_button = ps_button_create("Toggle Wireframe", 16);
	lisp_input = ps_input_create("(aref \"????????????\" 2)", 16);

	ps_container_add(vbox, regenerate_button);
	ps_container_add(vbox, wireframe_button);
	ps_container_add(vbox, lisp_input);

	PsWidget* hbox = ps_box_create(PS_DIRECTION_HORIZONTAL, 5);
	ps_container_add(vbox, hbox);

	eval_button = ps_button_create("Eval", 15);
	result_label = ps_label_create("Results will be here", 16);

	ps_container_add(hbox, eval_button);
	ps_container_add(hbox, result_label);

	PsWindow* canvas_window = ps_window_create("Canvas");
	PsWidget* canvas_window_vbox = ps_box_create(PS_DIRECTION_VERTICAL, 5);

	ps_window_set_root(canvas_window, canvas_window_vbox);

	slider = ps_slider_create(&canvas_draw_color.x, 0.f, 1.f, 16, NULL);
	ps_container_add(canvas_window_vbox, slider);

	slider = ps_slider_create(&canvas_draw_color.y, 0.f, 1.f, 16, NULL);
	ps_container_add(canvas_window_vbox, slider);

	slider = ps_slider_create(&canvas_draw_color.z, 0.f, 1.f, 16, NULL);
	ps_container_add(canvas_window_vbox, slider);

	PsWidget* canvas = ps_canvas_create(300, 200, canvas_draw);
	ps_container_add(canvas_window_vbox, canvas);

	window_mainloop();

	return 0;

error:
	fprintf(stderr, "Something failed...\n");
	return -1;
}

int main(int argc, char **argv) {
	void* dummy;
	stack_end = &dummy;

	return do_main(argc, argv);
}
