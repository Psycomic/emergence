#include <GL/glew.h>

#include "misc.h"
#include "images.h"

#include <stdio.h>

extern void exit(int status);
extern void *calloc(size_t nmemb, size_t size);

#define OFFSET_OF(s, m) (void*)(&((s*)NULL)->m)
#define TODO() do { fprintf(stderr, "Feature not implemented!\n"); exit(-1); } while(0);

typedef struct {
	Vector2 position;
	Vector2 uv_coords;
	Vector4 color;
} PsVert;

typedef uint PsIndex;

typedef struct {
	GLuint texture_id;

	uint32_t width;
	uint32_t height;
} PsAtlas;

typedef struct {
	Vector4 clip_rect;

	uint64_t ibo_offset;
	uint64_t elements_count;
} PsDrawCmd;

typedef struct {
	DynamicArray vbo;			// 	PsVert* vbo
	DynamicArray ibo;			// 	PsIndex* ibo
	DynamicArray commands;		//  PsDrawCmd* commands

	uint64_t ibo_last_index;
	uint32_t vbo_last_size;
	uint32_t ibo_last_size;
} PsDrawList;

typedef struct {
	PsDrawList** draw_lists;
	uint64_t draw_lists_count;

	Vector2 display_size;
} PsDrawData;

#define PS_PATH_BEING_USED	(1 << 0)
#define PS_PATH_CLOSED 		(1 << 1)
#define PS_FILLED_POLY		(1 << 2)
#define PS_TEXTURED_POLY	(1 << 3)

typedef struct {
	DynamicArray points;		// Vector2*
	float thickness;

	uint32_t flags;
} PsPath;

typedef struct {
	Image text_atlas;

	uint32_t glyph_width;
	uint32_t glyph_height;

	uint32_t texture_height;
	uint32_t texture_width;
} PsFont;

void ps_init(Vector2 display_size);
void ps_render();
void ps_resized(float width, float height);

void ps_font_init(PsFont* font, const char* path, uint32_t glyph_width, uint32_t glyph_height, uint32_t width, uint32_t height);

void ps_begin_path();
void ps_line_to(float x, float y);
void ps_close_path();
void ps_fill(Vector4 color, uint32_t flags);
void ps_text(const char* str, Vector2 position, float size, Vector4 color);
