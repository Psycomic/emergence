#include <GL/glew.h>

#include "misc.h"
#include "images.h"

#define PS_PATH_BEING_USED	(1 << 0)
#define PS_PATH_CLOSED 		(1 << 1)
#define PS_FILLED_POLY		(1 << 2)
#define PS_TEXTURED_POLY	(1 << 3)

#ifndef _PSYCHE_INTERNAL
typedef void PsWindow;

void ps_init(Vector2 display_size);
void ps_render();
void ps_resized(float width, float height);

void ps_begin_path();
void ps_line_to(float x, float y);
void ps_close_path();
void ps_fill(Vector4 color, uint32_t flags);
void ps_fill_rect(float x, float y, float w, float h, Vector4 color);
void ps_text(const char* str, Vector2 position, float size, Vector4 color);

PsWindow* ps_window_create(char* title);
void ps_window_destroy(PsWindow* window);
void ps_draw_gui();
#endif
