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

#ifndef _PSYCHE_INTERNAL
typedef void PsWindow;
typedef void PsWidget;
typedef void PsLabel;
typedef void PsButton;
typedef void PsSlider;
typedef void PsInput;

extern PsInput* ps_current_input;

void ps_init();
void ps_render();
void ps_resized_callback(void* data, int width, int height);
void ps_character_callback(uint codepoint);
void ps_scroll_callback(float yoffset);

void ps_begin_path();
void ps_line_to(float x, float y);
void ps_close_path();
void ps_fill(Vector4 color, uint32_t flags);
void ps_stroke(Vector4 color, float thickness);
void ps_fill_rect(float x, float y, float w, float h, Vector4 color);
void ps_text(const char* str, Vector2 position, float size, Vector4 color);

PsWindow* ps_window_create(char* title);
void ps_window_destroy(PsWindow* window);

PsLabel* ps_label_create(PsWidget* parent, char* text, float size);
char* ps_label_text(PsLabel* label);
void ps_label_set_text(PsLabel* label, char* text);

PsButton* ps_button_create(PsWidget* parent, char* text, float size);
uint8_t ps_button_state(PsButton* button);

PsSlider* ps_slider_create(PsWidget* parent, float* val, float min_val, float max_val, float text_size, float width, void (*callback)());

PsInput* ps_input_create(PsWidget* parent, char* value, float text_size, float width);
void ps_input_insert_at_point(PsInput* input, char* string);
char* ps_input_value(PsInput* input);
void ps_input_set_value(PsInput* input, const char* value);

void ps_draw_gui();
#endif
#endif
