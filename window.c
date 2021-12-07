#include "window.h"

#include <stdio.h>
#include <string.h>

#define UINT_NO_LAST_BIT(x) ((x) & 0x0fffffff)
#define UINT_LAST_BIT(x)    ((x) & 0x10000000)

Window g_window;

void window_new_key(uint code, BOOL is_raw);

void window_size_callback(GLFWwindow* window, int width, int height) {
	g_window.size.x = (float)width;
	g_window.size.y = (float)height;

	for (struct RHook* h = g_window.resize_hook; h != NULL; h = h->next)
		h->fn(h->user_data, width, height);
}

void window_character_callback(GLFWwindow* w, uint codepoint) {
	window_new_key(codepoint, GL_TRUE);
}

int window_create(int width, int height, const char* title, void(*setup)(), void(*update)()) {
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
	m_bzero(g_window.keys_delay, sizeof(g_window.keys_delay));

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

	glfwSetWindowSizeCallback(g_window.w, window_size_callback);
	glfwSetCharCallback(g_window.w, window_character_callback);

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

void window_add_character_hook(void (*fn)(void*, Key), void* data) {
	struct CHook* hook = malloc(sizeof(struct CHook));

	hook->fn = fn;
	hook->next = g_window.character_hook;
	hook->user_data = data;

	g_window.character_hook = hook;
}

Key key_create(uint code, uint32_t modifiers) {
	return (Key) {
		.code = code,
		.modifiers = modifiers
	};
}

BOOL key_equal(Key a, Key b) {
	return a.code == b.code && a.modifiers == b.modifiers;
}

BOOL key_code_printable(uint code) {
	return ('a' <= code && code <= 'z') ||
		('A' <= code && code <= 'Z');
}

void key_repr(char* buffer, Key k, uint32_t n) {
	uint32_t i =0;

	if (k.modifiers & KEY_MOD_CTRL) {
		strncpy(buffer, "C-", n);
		i += 2;
	}
	if (k.modifiers & KEY_MOD_ALT) {
		strncpy(buffer + i, "M-", n - i);
		i += 2;
	}
	if (k.modifiers & KEY_MOD_SUPER) {
		strncpy(buffer + i, "s-", n - i);
		i += 2;
	}

	if (key_code_printable(k.code) && i < n) {
		buffer[i] = k.code;
	}
	else {
		switch (k.code) {
		case '\n':
			strncpy(buffer + i, "RET", n - i);
			break;
		case KEY_LEFT:
			strncpy(buffer + i, "LEFT", n - i);
			break;
		case KEY_RIGHT:
			strncpy(buffer + i, "RIGHT", n - i);
			break;
		case KEY_TAB:
			strncpy(buffer + i, "TAB", n - i);
			break;
		default:
			if (i < n) buffer[i] = '?';
			break;
		}
	}
}

void window_new_key(uint code, BOOL is_raw) {
	uint keycode = '?';
	BOOL is_handled = GL_TRUE;

	if (is_raw) {
		keycode = code;
	}
	else {
		switch (code) {
		case GLFW_KEY_ENTER:
			keycode = '\n';
			break;
		case GLFW_KEY_TAB:
			keycode = KEY_TAB;
			break;
		case GLFW_KEY_BACKSPACE:
			keycode = KEY_DEL;
			break;
		case GLFW_KEY_LEFT:
			keycode = KEY_LEFT;
			break;
		case GLFW_KEY_RIGHT:
			keycode = KEY_RIGHT;
			break;
		case GLFW_KEY_UP:
			keycode = KEY_UP;
			break;
		case GLFW_KEY_DOWN:
			keycode = KEY_DOWN;
			break;
		default:
			is_handled = GL_FALSE;
			break;
		}
	}

	uint32_t modifiers = 0;
	if (g_window.keys[GLFW_KEY_LEFT_CONTROL] || g_window.keys[GLFW_KEY_RIGHT_CONTROL])
		modifiers |= KEY_MOD_CTRL;
	if (g_window.keys[GLFW_KEY_LEFT_ALT])
		modifiers |= KEY_MOD_ALT;
	if (g_window.keys[GLFW_KEY_LEFT_SUPER] || g_window.keys[GLFW_KEY_RIGHT_SUPER])
		modifiers |= KEY_MOD_SUPER;

	Key key = key_create(keycode, modifiers);

	if (is_handled) {
		for (struct CHook* h = g_window.character_hook; h != NULL; h = h->next)
			h->fn(h->user_data, key);
	}
}

void window_update() {
	double xpos, ypos;
	glfwGetCursorPos(g_window.w, &xpos, &ypos);

	g_window.cursor_position.x = (float)xpos - g_window.size.x / 2;
	g_window.cursor_position.y = g_window.size.y / 2 - (float)ypos;

	for (uint i = GLFW_KEY_SPACE; i < GLFW_KEY_LAST; i++) {
		g_window.keys[i] = glfwGetKey(g_window.w, i);

		if (g_window.keys[i]) {
			if (UINT_NO_LAST_BIT(g_window.keys_delay[i]) <= 0) {
				if (UINT_LAST_BIT(g_window.keys_delay[i]))
					g_window.keys_delay[i] = 2 | 0x10000000;
				else
					g_window.keys_delay[i] = 20 | 0x10000000;

				window_new_key(i, GL_FALSE);
			}
			else {
				g_window.keys_delay[i] = (UINT_NO_LAST_BIT(g_window.keys_delay[i]) - 1) |
					UINT_LAST_BIT(g_window.keys_delay[i]);
			}
		}
		else {
			g_window.keys_delay[i] = 0;
		}
	}

	g_window.should_close = glfwWindowShouldClose(g_window.w);
	g_window.mouse_button_left_state = glfwGetMouseButton(g_window.w, GLFW_MOUSE_BUTTON_LEFT);
}

void window_mainloop() {
	uint64_t count = 0;

	double start = glfwGetTime(),
		end = 0.0;

	while (!g_window.should_close) {
		window_update();
		g_window.update();

		glfwSwapBuffers(g_window.w);
		glfwPollEvents();

		end = glfwGetTime();

		double delta = end - start;
		global_time += delta;

		if (count++ % 10 == 0) {
			g_window.fps = (clock_t)(1.0 / delta);
			g_window.delta = delta;
		}

		start = glfwGetTime();
	}

	glfwTerminate();
}

BOOL window_key_as_text_evt(uint key) {
	if (g_window.keys[key]) {
		if (UINT_NO_LAST_BIT(g_window.keys_delay[key]) <= 0) {
			if (UINT_LAST_BIT(g_window.keys_delay[key]))
				g_window.keys_delay[key] = 2 | 0x10000000;
			else
				g_window.keys_delay[key] = 20 | 0x10000000;

			return GL_TRUE;
		}
		else {
			g_window.keys_delay[key] = (UINT_NO_LAST_BIT(g_window.keys_delay[key]) - 1) |
				UINT_LAST_BIT(g_window.keys_delay[key]);

			return GL_FALSE;
		}
	}

	g_window.keys_delay[key] = 0;
	return GL_FALSE;
}
