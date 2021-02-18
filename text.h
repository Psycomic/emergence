#ifndef TEXT_HEADER
#define TEXT_HEADER

#include "batch_renderer.h"

typedef void Text;

Text* text_create(Batch* batch, char* string, float size, Vector2 position, Vector3 color);

void text_set_color(Text* text, Vector3 color);
void text_set_angle(Text* text, float angle);
void text_set_position(Text* text, Vector2 position);
void text_set_transparency(Text* text, float transparency);
void text_set_depth(Text* text, float depth);

void text_destroy(Text* text);
void text_update(Text* text);

float text_get_width(Text* text);
float text_get_height(Text* text);
float text_get_size(Text* text);

#endif
