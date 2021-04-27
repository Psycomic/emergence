#ifndef _WINDOW_HEADER
#define _WINDOW_HEADER

#include "misc.h"

#include <GLFW/glfw3.h>
#include <time.h>

typedef struct {
	GLFWwindow* w;

	Vector2 size;
	Vector2 cursor_position;

	BOOL keys[GLFW_KEY_LAST + 1];
	BOOL should_close;

	int mouse_button_left_state;
	int mouse_button_right_state;

	void (*update)(clock_t);

	struct RHook {
		void (*fn)(void*, int, int);
		void* user_data;
		struct RHook* next;
	} *resize_hook;

	struct CHook {
		void (*fn)(void*, uint);
		void* user_data;
		struct CHook* next;
	} *character_hook;
} Window;

extern Window g_window;
extern float global_time;

int window_create(int width, int height, const char* title, void(*setup)(), void(*update)(clock_t));

void window_add_resize_hook(void (*fn)(void*, int, int), void* data);
void window_add_character_hook(void (*fn)(void*, uint), void* data);

void window_mainloop();

#endif
