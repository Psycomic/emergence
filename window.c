#include "window.h"

#include <stdio.h>

#ifdef _WIN32
#include <windows.h>

void usleep(clock_t time) {
	Sleep(time);
}

#endif // _WIN32
#ifdef __linux__

extern int usleep (unsigned int __useconds);

#endif // __linux__

Window g_window;

void window_character_callback(GLFWwindow* window, uint codepoint) {
	for (struct CHook* h = g_window.character_hook; h != NULL; h = h->next)
		h->fn(h->user_data, codepoint);
}

void window_size_callback(GLFWwindow* window, int width, int height) {
	g_window.size.x = (float)width;
	g_window.size.y = (float)height;

	for (struct RHook* h = g_window.resize_hook; h != NULL; h = h->next)
		h->fn(h->user_data, width, height);
}

int window_create(int width, int height, const char* title, void(*setup)(), void(*update)(clock_t)) {
	glewExperimental = 1;

	if (!glfwInit()) {
		fprintf(stderr, "GLFW not initialized correctly !\n");
		return -1;
	}

	glfwWindowHint(GLFW_SAMPLES, 4); // 4x antialiasing
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); // We want OpenGL 3.3
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

	g_window.size.x = (float)width;
	g_window.size.y = (float)height;

	g_window.should_close = GL_FALSE;

	ct_assert(sizeof(g_window.keys) > 8);
	m_bzero(g_window.keys, sizeof(g_window.keys));

	g_window.update = update;

	g_window.w = glfwCreateWindow(width, height, title, NULL, NULL);

	g_window.resize_hook = NULL;
	g_window.character_hook = NULL;

	if (g_window.w == NULL) {
		fprintf(stderr, "Failed to open a window\n");
		return -1;
	}

	glfwMakeContextCurrent(g_window.w); // Make window the current context

	if (glewInit() != GLEW_OK) {
		fprintf(stderr, "Failed to initialize GLEW\n");
		return -1;
	}

	// OpenGL settings
	glEnable(GL_BLEND);			// Enable blend
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // Set blend func

	glEnable(GL_CULL_FACE);

	glViewport(0, 0, width, height); // Viewport size

	glfwSetCharCallback(g_window.w, window_character_callback);
	glfwSetWindowSizeCallback(g_window.w, window_size_callback);

	setup();

	return 0;
}

void window_add_resize_hook(void (*fn)(void*, int, int), void* data) {
	struct RHook* hook = malloc(sizeof(struct RHook));

	hook->fn = fn;
	hook->next = g_window.resize_hook;
	hook->user_data = data;

	g_window.resize_hook = hook;
}

void window_add_character_hook(void (*fn)(void*, uint), void* data) {
	struct CHook* hook = malloc(sizeof(struct CHook));

	hook->fn = fn;
	hook->next = g_window.character_hook;
	hook->user_data = data;

	g_window.character_hook = hook;
}

void window_update() {
	double xpos, ypos;
	glfwGetCursorPos(g_window.w, &xpos, &ypos);

	g_window.cursor_position.x = (float)xpos - g_window.size.x / 2;
	g_window.cursor_position.y = g_window.size.y / 2 - (float)ypos;

	for (uint i = GLFW_KEY_SPACE; i < GLFW_KEY_LAST; i++)
		g_window.keys[i] = glfwGetKey(g_window.w, i);

	g_window.should_close = glfwWindowShouldClose(g_window.w);
	g_window.mouse_button_left_state = glfwGetMouseButton(g_window.w, GLFW_MOUSE_BUTTON_LEFT);
}

void window_mainloop() {
	uint64_t count = 0;

	DynamicArray frames;
	DYNAMIC_ARRAY_CREATE(&frames, clock_t);

	clock_t start = clock();
	clock_t fps = 0;
	clock_t spf = (1.0 / 60.0) * (double)CLOCKS_PER_SEC;

	while (!g_window.should_close) {
		window_update();

		g_window.update(fps);

		glfwSwapBuffers(g_window.w);
		glfwPollEvents();

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
}
