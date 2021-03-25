#ifndef TEXT_HEADER
#define TEXT_HEADER

#include "batch_renderer.h"

// Abstraction over text. Basically a collection of drawables, each with a
// glyph texture, to end up with text
typedef struct {
	Vector2 position;
	Vector3 color;

	BatchDrawable* drawable;
	char* string;

	float size;
	float angle;
} Text;

typedef struct {
	uint glyph_width;
	uint glyph_height;

	GLuint texture;

	uint texture_height;
	uint texture_width;
} Font;

void font_init(Font* font, Image* font_image, uint glyph_width, uint glyph_height, uint image_width, uint image_height);

Text* text_create(Batch* batch, Font* font, char* string, float size, Vector2 position, Vector3 color);

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
