#include <string.h>
#include <assert.h>

#include "batch_renderer.h"
#include "text.h"

#define TEXT_VERTEX_SIZE 9		// 3 + 2 + 1 + 3

void text_set_color(Text* text, Vector3 color) {
	for (uint i = 0; i < text->drawable->vertices_count; i++) {
		Vector3* vertex_color = (Vector3*)((float*)text->drawable->vertices + i * TEXT_VERTEX_SIZE + 6);
		*vertex_color = color;
	}

	text->color = color;
	text_update(text);
}

void text_set_transparency(Text* text, float transparency) {
	for (uint i = 0; i < text->drawable->vertices_count; i++) {
		float* vertex_transparency = (float*)text->drawable->vertices + i * TEXT_VERTEX_SIZE + 5;
		*vertex_transparency = transparency;
	}

	text_update(text);
}

void text_set_angle(Text* text, float angle) {
	for (uint i = 0; i < text->drawable->vertices_count; i++) {
		Vector2* vertex_position = (Vector2*)((float*)text->drawable->vertices + i * TEXT_VERTEX_SIZE);

		Vector2 origin_position;
		vector2_sub(&origin_position, *vertex_position, text->position);

		float rotation = angle - text->angle;
		vector2_rotate(&origin_position, origin_position, rotation);
		vector2_add(vertex_position, origin_position, text->position);
	}

	text->angle = angle;
	batch_drawable_update(text->drawable);
}

void text_set_position(Text* text, Vector2 position) {
	Vector2 translation;
	vector2_sub(&translation, position, text->position);

	for (uint i = 0; i < text->drawable->vertices_count; i++) {
		Vector2* vertex_position = (Vector2*)((float*)text->drawable->vertices + i * TEXT_VERTEX_SIZE);
		vector2_add(vertex_position, *vertex_position, translation);
	}

	text->position = position;
	text_update(text);
}

void text_set_depth(Text* text, float depth) {
	for (uint i = 0; i < text->drawable->vertices_count; i++) {
		float* vertex_depth = (float*)text->drawable->vertices + i * TEXT_VERTEX_SIZE + 2;
		*vertex_depth = depth;
	}

	text_update(text);
}

float text_get_width(Text* text) {
	float max_width = 0.f, width = 0.f;
	char* c = text->string;

	do {
		if (*c == '\n' || *c == '\0') {
			if (width > max_width)
				max_width = width;

			width = 0.f;
		}
		else {
			width += text->width;
		}
	} while (*(c++) != '\0');

	return max_width;
}

float text_get_height(Text* text) {
	char* c = text->string;
	uint count = 0;

	do {
		if (*c == '\n')
			count++;
	} while (*(++c) != '\0');

	return text->height * (count + 1);
}

void text_update(Text* text) {
	batch_drawable_update(text->drawable);
}

float text_get_size(Text* text) {
	return text->height;
}

// Every glyph will be a 32 x 32 texture. This means the image is 64 x 416
Text* text_create(Batch* batch, Font* font, char* string, float size, Vector2 position, Vector3 color) {
	Text* text = malloc(sizeof(Text));
	if (text == NULL) return text;

	m_bzero(text, sizeof(Text));

	text->string = string;

	text->height = size;

	uint text_length = strlen(string);
	uint line_return_count;

	const char *s = string;
	for (line_return_count = 0; s[line_return_count]; s[line_return_count] == '\n' ? line_return_count++ : *s++);

	text_length -= line_return_count;

	const uint vertex_size = TEXT_VERTEX_SIZE,
		vertices_number = vertex_size * 4 * text_length;

	float* drawable_vertices = malloc(sizeof(float) * vertices_number);
	uint* drawable_elements = malloc(sizeof(uint) * 6 * text_length);

	m_bzero(drawable_vertices, sizeof(float) * vertices_number);

	const float	glyph_width = font->glyph_width,
		glyph_height = font->glyph_height,
		height = font->texture_height,
		width = font->texture_width,
		half_width = width / glyph_width,
		half_height = height / glyph_height;

	text->width = size * (glyph_width / glyph_height);

	const int divisor = width / glyph_width;

	int y_stride = 0;
	uint element_index = 0;

	float size_height = size,
		size_width = text->width;

	for (uint i = 0, j = 0; string[j] != '\0'; j++) {
		if (string[j] == '\n') {
			y_stride--;
			i = 0;
		}
		else {
			uint index = string[j];

			float x_pos = ((index % divisor) * glyph_width) / width,
				y_pos = (1.f - 1 / half_height) - ((index / divisor) * glyph_height) / height;

			float uv_down_left[] = { x_pos, (y_pos + (1.f / half_height)) };
			float uv_down_right[] = { x_pos + 1.f / half_width, (y_pos + (1.f / half_height)) };
			float uv_up_left[] = { x_pos, y_pos };
			float uv_up_right[] = { x_pos + 1.f / half_width, y_pos };

			float vertex_up_left[] = { i * size_width, -size_height + y_stride * size_height };
			float vertex_up_right[] = { i * size_width + size_width, -size_height + y_stride * size_height };
			float vertex_down_left[] = { i * size_width, y_stride * size_height };
			float vertex_down_right[] = { i * size_width + size_width, y_stride * size_height };

#define GET_INDEX(arr, index) arr[element_index * vertex_size * 4 + index]

			// Initializing vertices: two triangles
			GET_INDEX(drawable_vertices, 0) = vertex_up_left[0];
			GET_INDEX(drawable_vertices, 1) = vertex_up_left[1]; // Set position
			GET_INDEX(drawable_vertices, 3) = uv_up_left[0];
			GET_INDEX(drawable_vertices, 4) = uv_up_left[1];

			GET_INDEX(drawable_vertices, 9) = vertex_down_left[0];
			GET_INDEX(drawable_vertices, 10) = vertex_down_left[1];
			GET_INDEX(drawable_vertices, 12) = uv_down_left[0];
			GET_INDEX(drawable_vertices, 13) = uv_down_left[1];

			GET_INDEX(drawable_vertices, 18) = vertex_down_right[0];
			GET_INDEX(drawable_vertices, 19) = vertex_down_right[1];
			GET_INDEX(drawable_vertices, 21) = uv_down_right[0];
			GET_INDEX(drawable_vertices, 22) = uv_down_right[1];

			GET_INDEX(drawable_vertices, 27) = vertex_up_right[0];
			GET_INDEX(drawable_vertices, 28) = vertex_up_right[1];
			GET_INDEX(drawable_vertices, 30) = uv_up_right[0];
			GET_INDEX(drawable_vertices, 31) = uv_up_right[1];

			// Initializing elements
			drawable_elements[element_index * 6 + 0] = element_index * 4 + 0;
			drawable_elements[element_index * 6 + 1] = element_index * 4 + 3;
			drawable_elements[element_index * 6 + 2] = element_index * 4 + 1;
			drawable_elements[element_index * 6 + 3] = element_index * 4 + 3;
			drawable_elements[element_index * 6 + 4] = element_index * 4 + 2;
			drawable_elements[element_index * 6 + 5] = element_index * 4 + 1;

#undef GET_INDEX

			i++;
			element_index++;
		}
	}

	text->drawable = batch_drawable_create(batch, drawable_vertices, 4 * text_length,
										   drawable_elements, 6 * text_length);

	free(drawable_elements);

	text_set_position(text, position);
	text_set_transparency(text, 1.f);
	text_set_color(text, color);

	return text;
}

/* Free resources associated with a text object */
void text_destroy(Text* text) {
	batch_drawable_destroy(text->drawable);
	free(text);
}

void font_init(Font* font, Image* font_image, uint glyph_width, uint glyph_height, uint image_width, uint image_height) {
	font->texture = texture_create(font_image);
	font->glyph_width = glyph_width;
	font->glyph_height = glyph_height;
	font->texture_width = image_width;
	font->texture_height = image_height;
}
