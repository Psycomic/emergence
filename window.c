#include "window.h"

#include <stdio.h>
#include <string.h>

Window g_window;

void window_new_key(uint code, int mods, BOOL is_raw);

void window_size_callback(GLFWwindow* window, int width, int height) {
	g_window.size.x = (float)width;
	g_window.size.y = (float)height;

	for (struct RHook* h = g_window.resize_hook; h != NULL; h = h->next)
		h->fn(h->user_data, width, height);
}

void window_character_callback(GLFWwindow* w, uint codepoint) {
	window_new_key(codepoint, 0, GL_TRUE);
}

void window_key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
/*	const char* name = glfwGetKeyName(key, 0);
	printf("Got key %s\n", name);

	printf("Modifiers: ");
	if (mods & GLFW_MOD_SHIFT) {
		printf("Shift ");
	}
	if (mods & GLFW_MOD_CONTROL) {
		printf("Control ");
	}
	if (mods & GLFW_MOD_ALT) {
		printf("Alt ");
	}
	if (mods & GLFW_MOD_SUPER) {
		printf("Super ");
	}
	if (mods & GLFW_MOD_CAPS_LOCK) {
		printf("Caps_Lock ");
	}
	if (mods & GLFW_MOD_NUM_LOCK) {
		printf("Num_Lock ");
	}
	printf(".\n");*/

	if (action == GLFW_PRESS || action == GLFW_REPEAT) {
		window_new_key(key, mods, GL_FALSE);
		g_window.keys[key] = GL_TRUE;
	} else {
		g_window.keys[key] = GL_FALSE;
	}
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
	glfwSetKeyCallback(g_window.w, window_key_callback);

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
	if (k.modifiers & KEY_MOD_SHIFT) {
		strncpy(buffer + i, "S-", n - i);
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

uint hash_key(void* data, uint size) {
	Key* key = data;

	return key->code + (key->modifiers << 24);
}

void window_new_key(uint code, int mods, BOOL is_raw) {
	uint keycode = '?';
	BOOL is_handled = GL_TRUE;

	uint32_t modifiers = 0;
	if (is_raw) {
		keycode = code;
	}
	else {
		if (mods & GLFW_MOD_CONTROL)
			modifiers |= KEY_MOD_CTRL;
		if (mods & GLFW_MOD_ALT)
			modifiers |= KEY_MOD_ALT;
		if (mods & GLFW_MOD_SUPER)
			modifiers |= KEY_MOD_SUPER;
		if (mods & GLFW_MOD_SHIFT)
			modifiers |= KEY_MOD_SHIFT;

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

		if (!is_handled && (modifiers & ~KEY_MOD_SHIFT))
		{
			is_handled = GL_TRUE;
			const char* name = glfwGetKeyName(code, 0);
			if (name) {
				keycode = u_string_to_codepoint(name);
			} else {
				is_handled = GL_FALSE;
			}
		}
	}

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
