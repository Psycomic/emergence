#ifndef _WINDOW_HEADER
#define _WINDOW_HEADER

#include "misc.h"

#include <GLFW/glfw3.h>
#include <time.h>

#define KEY_MOD_ALT   (1 << 0)
#define KEY_MOD_CTRL  (1 << 1)
#define KEY_MOD_SUPER (1 << 2)

#define KEY_TAB   ((1 << 16) + 1)
#define KEY_DEL   ((1 << 16) + 2)
#define KEY_LEFT  ((1 << 16) + 3)
#define KEY_RIGHT ((1 << 16) + 4)
#define KEY_UP    ((1 << 16) + 5)
#define KEY_DOWN  ((1 << 16) + 6)

typedef struct {
	uint code;
	uint32_t modifiers;
} Key;

typedef struct {
	GLFWwindow* w;

	Vector2 size;
	Vector2 cursor_position;

	BOOL keys[GLFW_KEY_LAST + 1];
	BOOL should_close;

	int mouse_button_left_state;
	int mouse_button_right_state;

	clock_t fps;
	double delta;

	void (*update)();

	struct RHook {
		void (*fn)(void*, int, int);
		void* user_data;
		struct RHook* next;
	} *resize_hook;

	struct CHook {
		void (*fn)(void*, Key);
		void* user_data;
		struct CHook* next;
	} *character_hook;
} Window;

extern Window g_window;
extern float global_time;

int window_create(int width, int height, const char* title, void(*setup)(), void(*update)(clock_t));

void window_add_resize_hook(void (*fn)(void*, int, int), void* data);
void window_add_character_hook(void (*fn)(void*, Key), void* data);

Key key_create(uint code, uint32_t modifiers);
BOOL key_equal(Key a, Key b);
BOOL key_code_printable(uint code);
void key_repr(char* buffer, Key k, uint32_t n);
uint hash_key(void* data, uint size);

void window_mainloop();

#endif
