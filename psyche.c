#include "drawable.h"

#include <string.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>

#define _PSYCHE_INTERNAL
#include "psyche.h"
#undef _PSYCHE_INTERNAL

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

typedef void PsWidget;

typedef struct {
	DynamicArray widgets;		// PsWidget*

	Vector2 size;
	Vector2 position;

	char* title;
} PsWindow;

#define PS_MAX_WINDOWS 256

static PsWindow* ps_windows[PS_MAX_WINDOWS] = {};
static uint64_t ps_windows_count = 0;

static GLuint ps_vbo;
static GLuint ps_ibo;

static GLuint ps_vao;

static GLuint ps_shader;
static GLuint ps_matrix_location;
static GLuint ps_texture_location;

static PsDrawData ps_ctx;
static PsAtlas ps_atlas;

static PsPath ps_current_path;

static PsFont ps_monospaced_font;
static PsFont ps_current_font;

static Vector2 ps_white_pixel = {
	.x = 0.f,
	.y = 0.f
};

extern float global_time;

void ps_draw_cmd_init(PsDrawCmd* cmd, uint64_t ibo_offset) {
	m_bzero(cmd, sizeof(PsDrawCmd));

	cmd->ibo_offset = ibo_offset;
}

void ps_draw_list_init(PsDrawList* list) {
	DYNAMIC_ARRAY_CREATE(&list->commands, PsDrawCmd);
	DYNAMIC_ARRAY_CREATE(&list->vbo, PsVert);
	DYNAMIC_ARRAY_CREATE(&list->ibo, PsIndex);

	list->ibo_last_index = 0;
	list->vbo_last_size = 0;
	list->ibo_last_size = 0;

	PsDrawCmd* cmd = dynamic_array_push_back(&list->commands, 1);
	ps_draw_cmd_init(cmd, 0);
}

void ps_atlas_init(Image* image) {
	ps_atlas.width = image->width;
	ps_atlas.height = image->height;

	image->data[0] = 255;
	image->data[1] = 255;
	image->data[2] = 255;
	image->data[3] = 255;

	ps_atlas.texture_id = texture_create(image);
}

void ps_resized(float width, float height) {
	ps_ctx.display_size.x = width;
	ps_ctx.display_size.y = height;
}

void ps_font_init(PsFont* font, const char* path, uint32_t glyph_width, uint32_t glyph_height, uint32_t width, uint32_t height) {
	font->glyph_width = glyph_width;
	font->glyph_height = glyph_height;
	font->texture_width = width;
	font->texture_height = height;

	if (image_load_bmp(&font->text_atlas, path)) {
		fprintf(stderr, "Failed to load font %s!\n", path);
		exit(-1);
	}

	uint8_t* new_data = malloc(font->text_atlas.height * font->text_atlas.width * 4);
	uint8_t* old_data = font->text_atlas.data;

	for (uint64_t i = 0; i < font->text_atlas.height; i++) {
		for (uint64_t j = 0; j < font->text_atlas.width; j++) {
			new_data[i * font->text_atlas.width * 4 + j * 4]     = 255;
			new_data[i * font->text_atlas.width * 4 + j * 4 + 1] = 255;
			new_data[i * font->text_atlas.width * 4 + j * 4 + 2] = 255;
			new_data[i * font->text_atlas.width * 4 + j * 4 + 3] = old_data[i * font->text_atlas.width * 3 + j * 3 + 2];
		}
	}

	free(font->text_atlas.data);

	font->text_atlas.data = new_data;
	font->text_atlas.color_encoding = GL_BGRA;
}

void ps_init(Vector2 display_size) {
	// Creating buffers
	glGenBuffers(1, &ps_vbo);
	glGenBuffers(1, &ps_ibo);

	// Creating shader
	ps_shader = shader_create("shaders/vertex_psyche.glsl", "shaders/fragment_psyche.glsl");
	ps_matrix_location = glGetUniformLocation(ps_shader, "matrix_transform");
	ps_texture_location = glGetUniformLocation(ps_shader, "tex");

	glGenVertexArrays(1, &ps_vao);
	m_bzero(&ps_ctx, sizeof(ps_ctx));
	ps_ctx.display_size = display_size;

	// Initializing draw lists with one draw list
	ps_ctx.draw_lists = calloc(5, sizeof(PsDrawList*));
	ps_ctx.draw_lists[ps_ctx.draw_lists_count++] = calloc(1, sizeof(PsDrawList));

	ps_draw_list_init(ps_ctx.draw_lists[0]);

	m_bzero(&ps_current_path, sizeof(ps_current_path));
	DYNAMIC_ARRAY_CREATE(&ps_current_path.points, Vector2);
	ps_current_path.thickness = 5.f;

	// Initializing font
	ps_font_init(&ps_monospaced_font, "./fonts/Monospace.bmp", 19, 32, 304, 512);
	ps_current_font = ps_monospaced_font;

	ps_atlas_init(&ps_current_font.text_atlas);
}

void ps_draw_list_clear(PsDrawList* cmd_list) {
	dynamic_array_clear(&cmd_list->vbo);
	dynamic_array_clear(&cmd_list->ibo);

	cmd_list->ibo_last_index = 0;
}

void ps_draw_cmd_clear(PsDrawCmd* cmd) {
	cmd->elements_count = 0;
}

void ps_render() {
	GLint last_vertex_array; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &last_vertex_array);
	GLint last_polygon_mode[2]; glGetIntegerv(GL_POLYGON_MODE, last_polygon_mode);

    glBindVertexArray(ps_vao);

	glBindBuffer(GL_ARRAY_BUFFER, ps_vbo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ps_ibo);

	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glEnableVertexAttribArray(2);

	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(PsVert), OFFSET_OF(PsVert, position));
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(PsVert), OFFSET_OF(PsVert, uv_coords));
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(PsVert), OFFSET_OF(PsVert, color));

	Mat4 ortho_matrix;
	mat4_create_orthogonal(ortho_matrix, -ps_ctx.display_size.x / 2, ps_ctx.display_size.x / 2,
						   -ps_ctx.display_size.y / 2, ps_ctx.display_size.y / 2, -1.f, 1.f);

	glUseProgram(ps_shader);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	glUniformMatrix4fv(ps_matrix_location, 1, GL_FALSE, ortho_matrix);
	glUniform1i(ps_texture_location, 0);

	for (uint64_t i = 0; i < ps_ctx.draw_lists_count; i++) {
		PsDrawList* cmd_list = ps_ctx.draw_lists[i];

		if (cmd_list->vbo.size != cmd_list->vbo_last_size)
			glBufferData(GL_ARRAY_BUFFER, cmd_list->vbo.size * sizeof(PsVert), cmd_list->vbo.data, GL_STREAM_DRAW);
		else
			glBufferSubData(GL_ARRAY_BUFFER, 0, cmd_list->vbo.size * sizeof(PsVert), cmd_list->vbo.data);

		if (cmd_list->ibo.size != cmd_list->ibo_last_size)
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, cmd_list->ibo.size * sizeof(PsIndex), cmd_list->ibo.data, GL_STREAM_DRAW);
		else
			glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, cmd_list->ibo.size * sizeof(PsIndex), cmd_list->ibo.data);

		cmd_list->ibo_last_size = cmd_list->ibo.size;
		cmd_list->vbo_last_size = cmd_list->vbo.size;

		for (uint64_t j = 0; j < cmd_list->commands.size; j++) {
			PsDrawCmd* cmd = dynamic_array_at(&cmd_list->commands, j);

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, ps_atlas.texture_id);
			glDrawElements(GL_TRIANGLES, cmd->elements_count, sizeof(PsIndex) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, (const void*)cmd->ibo_offset);

			ps_draw_cmd_clear(cmd);
		}

		ps_draw_list_clear(cmd_list);
	}

	glPolygonMode(GL_FRONT_AND_BACK, last_polygon_mode[0]);
	glBindVertexArray(last_vertex_array);

	get_opengl_errors();
}

void ps_begin_path() {
	assert(!(ps_current_path.flags & PS_PATH_BEING_USED));

	ps_current_path.flags |= PS_PATH_BEING_USED;
	dynamic_array_clear(&ps_current_path.points);
}

void ps_line_to(float x, float y) {
	assert(ps_current_path.flags & PS_PATH_BEING_USED);

	Vector2* p = dynamic_array_push_back(&ps_current_path.points, 1);
	p->x = x;
 	p->y = y;
}

void ps_close_path() {
	assert(ps_current_path.flags & PS_PATH_BEING_USED);

	ps_current_path.flags |= PS_PATH_CLOSED;
}

void ps_fill(Vector4 color, uint32_t flags) {
	assert(ps_current_path.flags & PS_PATH_BEING_USED);

	PsDrawList* list = ps_ctx.draw_lists[0];
	PsDrawCmd* cmd = dynamic_array_at(&list->commands, 0);

	assert(ps_current_path.points.size >= 3 && (ps_current_path.flags & PS_PATH_CLOSED));

	Vector4 aabb = get_aabb(ps_current_path.points.data, ps_current_path.points.size);
	float width = aabb.z - aabb.x,
		height = aabb.w - aabb.y;

	PsVert* new_verts = dynamic_array_push_back(&list->vbo, ps_current_path.points.size);
	Vector2 mean_pos;

	uint64_t i;
	for (i = 0; i < ps_current_path.points.size; i++) {
		Vector2* position = dynamic_array_at(&ps_current_path.points, i);
		vector2_add(&mean_pos, mean_pos, *position);

		new_verts[i].color = color;
		new_verts[i].position = *position;

		if (flags & PS_TEXTURED_POLY) {
			new_verts[i].uv_coords.x = (position->x - aabb.x) / width;
			new_verts[i].uv_coords.y = (position->y - aabb.y) / height;
		}
		else {
			new_verts[i].uv_coords = ps_white_pixel;
		}
	}

	uint64_t elements_count = 3 + (ps_current_path.points.size - 3) * 3;
	PsIndex* new_elements = dynamic_array_push_back(&list->ibo, elements_count);

	new_elements[0] = list->ibo_last_index + 1;
	new_elements[1] = list->ibo_last_index + 2;
	new_elements[2] = list->ibo_last_index;

	for (uint64_t j = 0; j < elements_count / 3 - 1; j++) {
		new_elements[j * 3 + 3] = list->ibo_last_index + j + 2;
		new_elements[j * 3 + 4] = list->ibo_last_index + j + 3;
		new_elements[j * 3 + 5] = list->ibo_last_index;
	}

	list->ibo_last_index += ps_current_path.points.size;
	cmd->elements_count += elements_count;

	ps_current_path.flags &= ~PS_PATH_BEING_USED;
}

void ps_fill_rect(float x, float y, float w, float h, Vector4 color) {
	PsDrawList* list = ps_ctx.draw_lists[0];
	PsDrawCmd* cmd = dynamic_array_at(&list->commands, 0);

	PsVert* rect_verts = dynamic_array_push_back(&list->vbo, 4);
	PsIndex* rect_indexes = dynamic_array_push_back(&list->ibo, 6);

	rect_verts[0].position.x = x;
	rect_verts[0].position.y = y;
	rect_verts[0].uv_coords = ps_white_pixel;
	rect_verts[0].color = color;

	rect_verts[1].position.x = x + w;
	rect_verts[1].position.y = y;
	rect_verts[1].uv_coords = ps_white_pixel;
	rect_verts[1].color = color;

	rect_verts[2].position.x = x + w;
	rect_verts[2].position.y = y + h;
	rect_verts[2].uv_coords = ps_white_pixel;
	rect_verts[2].color = color;

	rect_verts[3].position.x = x;
	rect_verts[3].position.y = y + h;
	rect_verts[3].uv_coords = ps_white_pixel;
	rect_verts[3].color = color;

	rect_indexes[0] = list->ibo_last_index + 0;
	rect_indexes[1] = list->ibo_last_index + 3;
	rect_indexes[2] = list->ibo_last_index + 1;
	rect_indexes[3] = list->ibo_last_index + 3;
	rect_indexes[4] = list->ibo_last_index + 2;
	rect_indexes[5] = list->ibo_last_index + 1;

	cmd->elements_count += 6;
	list->ibo_last_index += 4;
}

void ps_text(const char* str, Vector2 position, float size, Vector4 color) {
	uint64_t text_length = strlen(str),
		line_return_count;

	const char* s = str;

	for (line_return_count = 0; s[line_return_count];
		 s[line_return_count] == '\n' ? line_return_count++ : *s++);

	text_length -= line_return_count;

	PsDrawList* list = ps_ctx.draw_lists[0];
	PsDrawCmd* cmd = dynamic_array_at(&list->commands, 0);

	PsVert* text_vertices = dynamic_array_push_back(&list->vbo, text_length * 4);
	PsIndex* text_indexes = dynamic_array_push_back(&list->ibo, text_length * 6);

	const float	glyph_width = ps_current_font.glyph_width,
		glyph_height = ps_current_font.glyph_height,
		height = ps_current_font.texture_height,
		width = ps_current_font.texture_width,
		half_width = width / glyph_width,
		half_height = height / glyph_height;

	const int divisor = width / glyph_width;

	int y_stride = 0;
	uint64_t element_index = 0;

	const float size_height = size,
		size_width = size * (glyph_width / glyph_height);

	for (uint64_t i = 0, j = 0; str[j] != '\0'; j++) {
		if (str[j] == '\n') {
			y_stride--;
			i = 0;
		}
		else {
			char index = str[j];

			float x_pos = ((index % divisor) * glyph_width) / width,
				y_pos = (1.f - 1 / half_height) - ((index / divisor) * glyph_height) / height;

			Vector2 uv_up_left = { { x_pos, y_pos } };
			Vector2 uv_up_right = { { x_pos + 1.f / half_width, y_pos } };
			Vector2 uv_down_left = { { x_pos, (y_pos + (1.f / half_height)) } };
			Vector2 uv_down_right = { { x_pos + 1.f / half_width, (y_pos + (1.f / half_height)) } };

			Vector2 vertex_up_left = { { i * size_width, -size_height + y_stride * size_height } };
			Vector2 vertex_up_right = { { i * size_width + size_width, -size_height + y_stride * size_height } };
			Vector2 vertex_down_left = { { i * size_width, y_stride * size_height } };
			Vector2 vertex_down_right = { { i * size_width + size_width, y_stride * size_height } };

			vector2_add(&vertex_up_left, vertex_up_left, position);
			vector2_add(&vertex_down_left, vertex_down_left, position);
			vector2_add(&vertex_up_right, vertex_up_right, position);
			vector2_add(&vertex_down_right, vertex_down_right, position);

#define VERTEX(x) text_vertices[element_index * 4 + x]
#define INDEX(x)  text_indexes[element_index * 6 + x]

			VERTEX(0).color = color;
			VERTEX(0).position = vertex_up_left;
			VERTEX(0).uv_coords = uv_up_left;

			VERTEX(1).color = color;
			VERTEX(1).position = vertex_down_left;
			VERTEX(1).uv_coords = uv_down_left;

			VERTEX(2).color = color;
			VERTEX(2).position = vertex_down_right;
			VERTEX(2).uv_coords = uv_down_right;

			VERTEX(3).color = color;
			VERTEX(3).position = vertex_up_right;
			VERTEX(3).uv_coords = uv_up_right;

			INDEX(0) = list->ibo_last_index + element_index * 4 + 0;
			INDEX(1) = list->ibo_last_index + element_index * 4 + 3;
			INDEX(2) = list->ibo_last_index + element_index * 4 + 1;
			INDEX(3) = list->ibo_last_index + element_index * 4 + 3;
			INDEX(4) = list->ibo_last_index + element_index * 4 + 2;
			INDEX(5) = list->ibo_last_index + element_index * 4 + 1;

#undef VERTEX
#undef INDEX

			i++;
			element_index++;
		}
	}

	list->ibo_last_index += text_length * 4;
	cmd->elements_count += text_length * 6;
}

PsWindow* ps_window_create(char* title) {
	PsWindow* window = malloc(sizeof(PsWindow));
	ps_windows[ps_windows_count++] = window;

	window->size.x = 400.f;
	window->size.y = 200.f;

	window->position.x = -window->size.x / 2;
	window->position.y = -window->size.y / 2;

	window->title = title;

	DYNAMIC_ARRAY_CREATE(&window->widgets, PsWidget*);

	return window;
}

void ps_window_destroy(PsWindow* window) {
	free(window);

	uint64_t i;
	for (i = 0; i < ps_windows_count; i++)
		if (ps_windows[i] == window)
			break;

	ps_windows[i] = ps_windows[--ps_windows_count];
}

void ps_widget_draw(PsWidget* widget) {}

static Vector4 ps_window_background_color = { { 0.1f, 0.1f, 1.f, 1.f } };
static Vector4 ps_window_title_color = { { 1.f, 1.f, 1.f, 1.f } };
static float ps_window_border_size = 5.f;

void ps_window_draw(PsWindow* window) {
	ps_fill_rect(window->position.x, window->position.y, window->size.x, window->size.y, ps_window_background_color);

	Vector2 title_position;
	title_position.y = window->position.y + window->size.y;
	title_position.x = window->position.x;

	ps_fill_rect(window->position.x, window->position.y, window->size.x, ps_window_border_size, ps_window_title_color);
	ps_fill_rect(window->position.x, window->position.y + window->size.y, window->size.x, ps_window_border_size, ps_window_title_color);
	ps_fill_rect(window->position.x, window->position.y, ps_window_border_size, window->size.y, ps_window_title_color);
	ps_fill_rect(window->position.x + window->size.x - ps_window_border_size, window->position.y, ps_window_border_size, window->size.y, ps_window_title_color);

	ps_text(window->title, title_position, 20.f, ps_window_title_color);

	for (uint64_t i = 0; i < window->widgets.size; i++)
		ps_widget_draw(dynamic_array_at(&window->widgets, i));
}

void ps_draw_gui() {
	for (uint64_t i = 0; i < ps_windows_count; i++)
		ps_window_draw(ps_windows[i]);
}
