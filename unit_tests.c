#include "misc.h"
#include "batch_renderer.h"
#include "render.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

void execute_tests(void) {
	// Hash table test
	HashTable* table = hash_table_create(4);

	uint value_one = 10,
		value_two = 20,
		value_three = 30,
		value_four = 40,
		value_five = 50;

	hash_table_set(table, "one", &value_one, sizeof(uint));
	hash_table_set(table, "two", &value_two, sizeof(uint));
	hash_table_set(table, "three", &value_three, sizeof(uint));
	hash_table_set(table, "four", &value_four, sizeof(uint));
	hash_table_set(table, "five", &value_five, sizeof(uint));

	value_five = 20;
	hash_table_set(table, "five", &value_five, sizeof(uint));

	assert(*(uint*)hash_table_get(table, "one") == value_one);
	assert(*(uint*)hash_table_get(table, "two") == value_two);
	assert(*(uint*)hash_table_get(table, "three") == value_three);
	assert(*(uint*)hash_table_get(table, "four") == value_four);
	assert(*(uint*)hash_table_get(table, "five") == value_five);

	glewExperimental = 1;

	glfwInit();

	glfwWindowHint(GLFW_SAMPLES, 4); // 4x antialiasing
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); // We want OpenGL 3.3
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); // We don't want the old OpenGL

	GLFWwindow* window = glfwCreateWindow(800, 600, "Unit tests", NULL, NULL);
	glfwMakeContextCurrent(window);

	glewInit();

	GLuint batch_shader = shader_create("./shaders/vertex_batch_shader.glsl", "./shaders/fragment_batch_shader.glsl");

	char* batch_uniforms[] = {
		"color"
	};

	Vector3 triangle_color = {1.f, 0.5f, 0.f};

	Material* batch_material = material_create(batch_shader, batch_uniforms, ARRAY_SIZE(batch_uniforms));
	material_set_uniform_vec3(batch_material, 0, triangle_color);

	Batch test_batch;
	batch_init(&test_batch, batch_material, 16 * sizeof(float), 16 * sizeof(uint32_t));

	Vector3 triangle_position = {
		0.f, 0.f, 0.f
	};

	float triangle_vertices[] = {
		1.f, 0.f, 0.f,
		-1.f, 0.f, 0.f,
		0.f, 1.f, 0.f
	};

	uint64_t triangle_attributes_sizes[] = {3};

	uint32_t triangle_elements[] = {
		0, 1, 2
	};

	BatchDrawable triangle_batch_drawable;
	batch_drawable_init(&test_batch, &triangle_batch_drawable, &triangle_position,
						triangle_vertices, 3, triangle_attributes_sizes, 1, triangle_elements, 3);

	while (!glfwWindowShouldClose(window)) {
		glClearColor(0.f, 0.f, 1.f, 1.f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		batch_draw(&test_batch);

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	glfwTerminate();
}
