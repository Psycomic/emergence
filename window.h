#ifndef _WINDOW_HEADER
#define _WINDOW_HEADER

#include "misc.h"

#include <GLFW/glfw3.h>
#include <time.h>

typedef struct {
	GLFWwindow* w;

	Vector2 size;
	Vector2 cursor_position;

	int last_character;

	BOOL keys[GLFW_KEY_LAST + 1];
	BOOL should_close;

	int mouse_button_left_state;
	int mouse_button_right_state;

	void (*update)(clock_t);
} Window;

extern Window g_window;
extern float global_time;

int window_create(int width, int height, const char* title, void(*setup)(), void(*update)(clock_t));
void window_mainloop();

#endif
