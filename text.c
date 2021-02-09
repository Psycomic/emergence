#include <string.h>
#include <assert.h>

#include "batch_renderer.h"

#define TEXT_VERTEX_SIZE 5		/* 2 + 2 + 1 */

// Abstraction over text. Basically a collection of drawables, each with a
// glyph texture, to end up with text
typedef struct {
	Vector2 position;
	Vector3 color;

	BatchDrawable drawable;
	char* string;

	float size;
	float angle;
} Text;

void text_update(Text* text);

void text_set_color(Text* text, Vector3 color) {
	/* TODO */
}

void text_set_transparency(Text* text, float transparency) {
	/* TODO */
}

void text_set_angle(Text* text, float angle) {
	/* TODO */
}

void text_set_position(Text* text, Vector2 position) {
	Vector2 translation;
	vector2_sub(&translation, position, text->position);

	for (uint i = 0; i < text->drawable.vertices_count; i += TEXT_VERTEX_SIZE) {
		float* vertices = text->drawable.vertices;
		vector2_add((Vector2*)(vertices + i), *(Vector2*)(vertices + i), translation);
	}

	text->position = position;
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
			width += text->size;
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

	return text->size * (count + 1);
}

void text_update(Text* text) {
	batch_drawable_update(&text->drawable);
}

float text_get_size(Text* text) {
	return text->size;
}

// Every glyph will be a 32 x 32 texture. This means the image is 64 x 416
Text* text_create(Batch* batch, char* string, float size, Vector2 position, Vector3 color) {
	Text* text = malloc(sizeof(Text));
	assert(text != NULL);

	text->string = string;
	text->size = size;
	text->color = color;

	uint text_length = strlen(string);

	const uint vertex_size = 2 + 2 + 1,
		vertices_number = vertex_size * 4 * text_length;

	float* drawable_vertices = malloc(sizeof(float) * vertices_number);
	uint* drawable_elements = malloc(sizeof(uint) * 6 * text_length);

	const float height = 512,
		half_height = height / 32,
		width = 64,
		half_width = width / 32;

	int y_stride = 0;

	for (uint i = 0, j = 0; j < text_length; j++) {
		if (string[j] == '\n') {
			y_stride--;
			i = 0;
		}
		else {
			assert((string[j] >= 'A' && string[j] <= 'Z') || string[j] == ' ' || string[j] == '\n');

			uint index = string[j] == ' ' ? 31 : string[j] - 'A';

			float x_pos = ((index % 2) * 32) / 64.f,
				y_pos = (1.f - 1 / half_height) - ((index / 2) * 32) / height;

			float uv_down_left[] = { x_pos, (y_pos + (1.f / half_height)) };
			float uv_down_right[] = { x_pos + 1.f / half_width, (y_pos + (1.f / half_height)) };
			float uv_up_left[] = { x_pos, y_pos };
			float uv_up_right[] = { x_pos + 1.f / half_width, y_pos };

			float vertex_up_left[2] = { i * size, -size + y_stride * size };
			float vertex_up_right[2] = { i * size + size, -size + y_stride * size };
			float vertex_down_left[2] = { i * size, y_stride * size };
			float vertex_down_right[2] = { i * size + size, y_stride * size };

#define GET_INDEX(arr, index) arr[j * vertex_size * 4 + index]

			// Initializing vertices: two triangles
			GET_INDEX(drawable_vertices, 0) = vertex_up_left[0]; GET_INDEX(drawable_vertices, 1) = vertex_up_left[1]; // Set position
			GET_INDEX(drawable_vertices, 2) = uv_up_left[0]; GET_INDEX(drawable_vertices, 3) = uv_up_left[1];
			GET_INDEX(drawable_vertices, 4) = 1.f;

			GET_INDEX(drawable_vertices, 5) = vertex_down_left[0]; GET_INDEX(drawable_vertices, 6) = vertex_down_left[1];
			GET_INDEX(drawable_vertices, 7) = uv_down_left[0]; GET_INDEX(drawable_vertices, 8) = uv_down_left[1];
			GET_INDEX(drawable_vertices, 9) = 1.f;

			GET_INDEX(drawable_vertices, 10) = vertex_down_right[0]; GET_INDEX(drawable_vertices, 11) = vertex_down_right[1];
			GET_INDEX(drawable_vertices, 12) = uv_down_right[0]; GET_INDEX(drawable_vertices, 13) = uv_down_right[1];
			GET_INDEX(drawable_vertices, 14) = 1.f;

			GET_INDEX(drawable_vertices, 15) = vertex_up_right[0]; GET_INDEX(drawable_vertices, 16) = vertex_up_right[1];
			GET_INDEX(drawable_vertices, 17) = uv_up_right[0]; GET_INDEX(drawable_vertices, 18) = uv_up_right[1];
			GET_INDEX(drawable_vertices, 19) = 1.f;

			// Initializing elements
			drawable_elements[j * 6 + 0] = j * 4 + 0; drawable_elements[j * 6 + 1] = j * 4 + 3;
			drawable_elements[j * 6 + 2] = j * 4 + 1; drawable_elements[j * 6 + 3] = j * 4 + 3;
			drawable_elements[j * 6 + 4] = j * 4 + 2; drawable_elements[j * 6 + 5] = j * 4 + 1;

#undef GET_INDEX

			i++;
		}
	}

	batch_drawable_init(batch, &text->drawable, drawable_vertices, vertices_number / vertex_size,
						drawable_elements, 6 * text_length);
	text_set_position(text, position);

	return text;
}

/* Free resources associated with a text object */
void text_destroy(Text* text) {
	free(text);
}
