#ifndef _PSYCHE_HEADER
#define _PSYCHE_HEADER

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "misc.h"
#include "images.h"

#define PS_PATH_BEING_USED	(1 << 0)
#define PS_PATH_CLOSED 		(1 << 1)
#define PS_FILLED_POLY		(1 << 2)
#define PS_TEXTURED_POLY	(1 << 3)

#define PS_WIDGET_SELECTED	(1 << 0)
#define PS_WIDGET_HOVERED	(1 << 1)
#define PS_WIDGET_CLICKING	(1 << 2)
#define PS_WIDGET_CLICKED	(1 << 3)

#define PS_WIDGET(o) ((PsWidget*)o)
#define PS_CONTAINER(o) ((PsContainer*)o)

typedef enum {
	PS_DIRECTION_VERTICAL,
	PS_DIRECTION_HORIZONTAL
} PsDirection;

#ifndef _PSYCHE_INTERNAL
typedef void PsWindow;
typedef void PsWidget;

void ps_init();
void ps_render(BOOL is_visible);
void ps_resized_callback(void* data, int width, int height);
void ps_character_callback(uint codepoint);
void ps_scroll_callback(float yoffset);

void ps_toggle_wireframe();

void ps_begin_scissors(float x, float y, float w, float h);
void ps_end_scissors();

void ps_begin_path();
void ps_line_to(float x, float y);
void ps_close_path();
void ps_fill(Vector4 color, uint32_t flags);
void ps_stroke(Vector4 color, float thickness);
void ps_fill_rect(float x, float y, float w, float h, Vector4 color);
void ps_text(const char* str, Vector2 position, float size, Vector4 color);

PsWindow* ps_window_create(char* title);
void ps_window_destroy(PsWindow* window);
void ps_window_set_root(PsWindow* window, PsWidget* root_widget);

void ps_container_add(PsWidget* container_widget, PsWidget* widget);

uint8_t ps_widget_state(PsWidget* widget);

PsWidget* ps_box_create(PsDirection direction, float spacing);

PsWidget* ps_label_create(char* text, float size);
char* ps_label_text(PsWidget* label);
void ps_label_set_text(PsWidget* label, char* text);

PsWidget* ps_button_create(char* text, float size);

PsWidget* ps_slider_create(float* val, float min_val, float max_val, float text_size, void (*callback)());

PsWidget* ps_input_create(char* value, float text_size);
void ps_input_insert_at_point(PsWidget* input, char* string);
char* ps_input_value(PsWidget* input);
void ps_input_set_value(PsWidget* input, const char* value);

PsWidget* ps_canvas_create(float width, float height, void (*draw_fn)(PsWidget*, Vector2, Vector2));

void ps_draw_gui();
#endif
#endif
