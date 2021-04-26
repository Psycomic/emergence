#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "misc.h"
#include "images.h"

#define PS_PATH_BEING_USED	(1 << 0)
#define PS_PATH_CLOSED 		(1 << 1)
#define PS_FILLED_POLY		(1 << 2)
#define PS_TEXTURED_POLY	(1 << 3)

#define PS_BUTTON_HOVERED	(1 << 0)
#define PS_BUTTON_CLICKING	(1 << 1)
#define PS_BUTTON_CLICKED	(1 << 2)

#ifndef _PSYCHE_INTERNAL
typedef void PsWindow;
typedef void PsWidget;
typedef void PsLabel;
typedef void PsButton;

void ps_init();
void ps_render();
void ps_resized_callback(float width, float height);
void ps_character_callback(uint codepoint);
void ps_scroll_callback(float yoffset);

void ps_begin_path();
void ps_line_to(float x, float y);
void ps_close_path();
void ps_fill(Vector4 color, uint32_t flags);
void ps_fill_rect(float x, float y, float w, float h, Vector4 color);
void ps_text(const char* str, Vector2 position, float size, Vector4 color);

PsWindow* ps_window_create(char* title);
void ps_window_destroy(PsWindow* window);

PsLabel* ps_label_create(PsWidget* parent, char* text, float size);
PsButton* ps_button_create(PsWidget* parent, char* text, float size);
uint32_t ps_button_state(PsButton* button);

void ps_draw_gui();
#endif
