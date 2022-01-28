#include "drawable.h"
#include "random.h"

#include <string.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <float.h>

#define _PSYCHE_INTERNAL
#include "psyche.h"
#undef _PSYCHE_INTERNAL

extern void exit(int status);
extern void *calloc(size_t nmemb, size_t size);

#define OFFSET_OF(s, m) (void*)(&((s*)NULL)->m)
#define TODO() do { fprintf(stderr, "Feature not implemented!\n"); exit(-1); } while(0);

#define PS_WINDOW_SELECTED_BIT  (1 << 0)
#define PS_WINDOW_DRAGGING_BIT  (1 << 1)
#define PS_WINDOW_SELECTION_BIT (1 << 2)
#define PS_WINDOW_RESIZE_BIT    (1 << 3)
#define PS_WINDOW_RESIZABLE_BIT (1 << 4)

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
	enum {
		PS_EVENT_CHARACTER_INPUT,
		PS_EVENT_RIGHT_CLICK,
		PS_EVENT_LEFT_CLICK,
		PS_EVENT_HANDLED
	} type;

	union {
		Key key;
		Vector2 click;
	} event;
} PsEvent;

#define PS_MAX_EVENTS 1024
static PsEvent ps_event_queue[PS_MAX_EVENTS];
static uint64_t ps_event_queue_end = 0;

typedef struct {
	Key key;
	void (*callback)(void*);
	void* userptr;
} PsBinding;

static HashTable* ps_global_bindings;

typedef struct {
	DynamicArray points;		// Vector2*
	float thickness;

	uint32_t flags;
} PsPath;

typedef struct {
	GLboolean active;

	Vector2 position;
	Vector2 size;
} PsScissors;

typedef struct {
	Image text_atlas;

	uint32_t glyph_width;
	uint32_t glyph_height;

	uint32_t texture_height;
	uint32_t texture_width;
} PsFont;

struct PsWidget;

// Takes the anchor and the minimum size of the widget
// and returns the actual size that was drawn
typedef void (*PsDrawFunction)(struct PsWidget*, Vector2 anchor, Vector2 size);
typedef Vector2 (*PsMinSizeFunction)(struct PsWidget*);

typedef struct PsWidget {
	struct PsWidget* parent;
	PsDrawFunction draw;
	PsMinSizeFunction min_size;

	uint32_t flags;
} PsWidget;

typedef struct {
	PsWidget header;
	DynamicArray children;

	float spacing;
} PsContainer;

typedef struct PsWindow {
	Vector2 size;
	Vector2 position;

	char* title;

	uint32_t flags;

	PsWidget* root;
} PsWindow;

typedef struct {
	PsContainer header;
	PsDirection direction;
} PsBox;

typedef struct {
	PsWidget header;

	char* text;
	Vector4 color;
	Vector2 size;

	float text_size;
} PsLabel;

typedef struct {
	PsWidget header;

	char* text;
	float text_size;

	Vector2 size;
} PsButton;

typedef struct {
	PsWidget header;

	float* val;
	float min_val, max_val;
	float text_size;
	float width;

	void (*callback)();
} PsSlider;

#define INPUT_MAX_ENTERED_TEXT 2048

typedef struct {
	PsWidget header;

	char value[INPUT_MAX_ENTERED_TEXT];

	float text_size;
	float width;

	uint cursor_position;
	uint lines_count;

	BOOL selected;
} PsInput;

typedef struct {
	PsWidget header;
	float width, height;

	void (*draw_fn)(PsWidget*, Vector2, Vector2);
} PsCanvas;

#define PS_MAX_WINDOWS 255

static PsWindow* ps_windows[PS_MAX_WINDOWS];
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
static PsScissors ps_current_scissors;

static PsFont ps_monospaced_font;
static PsFont ps_current_font;

static BOOL ps_wireframe;

/* Key psyche_last_key; */
/* static BOOL last_character_read = GL_TRUE; */

static Vector2 ps_white_pixel = {
	.x = 0.f,
	.y = 0.f
};

extern float global_time;

void ps_draw_gui();

void ps_window_handle_events(PsWindow* selected_window);
void ps_window_draw(PsWindow* window);
Vector2 ps_window_anchor(PsWindow* window);
void ps_fill_rect(float x, float y, float w, float h, Vector4 color);

BOOL is_in_box(Vector2 point, float x, float y, float w, float h) {
	return point.x >= x && point.x <= x + w && point.y >= y && point.y <= y + h;
}

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

void ps_dispatch_event(PsEvent evt) {
	ps_event_queue[(ps_event_queue_end++ % PS_MAX_EVENTS)] = evt;
}
void ps_handled_event(uint64_t i) {
	ps_event_queue[i].type = PS_EVENT_HANDLED;
}

void ps_resized_callback(void* data, int width, int height) {
	ps_ctx.display_size.x = (float)width;
	ps_ctx.display_size.y = (float)height;
}

void ps_character_callback(void* data, Key key) {
	PsEvent new_event;
	new_event.type = PS_EVENT_CHARACTER_INPUT;
	new_event.event.key = key;

	ps_dispatch_event(new_event);
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

void ps_init() {
	ps_global_bindings = hash_table_create(512, hash_key);

	glGenBuffers(1, &ps_vbo);
	glGenBuffers(1, &ps_ibo);

	ps_wireframe = GL_FALSE;

	ps_shader = shader_create("shaders/vertex_psyche.glsl", "shaders/fragment_psyche.glsl");
	ps_matrix_location = glGetUniformLocation(ps_shader, "matrix_transform");
	ps_texture_location = glGetUniformLocation(ps_shader, "tex");

	glGenVertexArrays(1, &ps_vao);
	m_bzero(&ps_ctx, sizeof(ps_ctx));

	ps_ctx.display_size = g_window.size;

	ps_ctx.draw_lists = calloc(5, sizeof(PsDrawList*));
	ps_ctx.draw_lists[ps_ctx.draw_lists_count++] = calloc(1, sizeof(PsDrawList));

	ps_draw_list_init(ps_ctx.draw_lists[0]);

	m_bzero(&ps_current_path, sizeof(ps_current_path));
	DYNAMIC_ARRAY_CREATE(&ps_current_path.points, Vector2);
	ps_current_path.thickness = 5.f;

	m_bzero(&ps_current_scissors, sizeof(ps_current_scissors));
	ps_current_scissors.active = GL_FALSE;

	ps_font_init(&ps_monospaced_font, "./fonts/Monospace.bmp", 19, 32, 304, 512);
	ps_current_font = ps_monospaced_font;

	ps_atlas_init(&ps_current_font.text_atlas);

	window_add_character_hook(ps_character_callback, NULL);
	window_add_resize_hook(ps_resized_callback, NULL);
}

void ps_draw_list_clear(PsDrawList* cmd_list) {
	dynamic_array_clear(&cmd_list->vbo);
	dynamic_array_clear(&cmd_list->ibo);

	cmd_list->ibo_last_index = 0;
}

void ps_draw_cmd_clear(PsDrawCmd* cmd) {
	cmd->elements_count = 0;
}

void ps_toggle_wireframe() {
	ps_wireframe = !ps_wireframe;
}

void ps_add_global_binding(Key key, void (*callback)(void*), void* userptr) {
	PsBinding* binding = malloc(sizeof(PsBinding));
	binding->key = key;
	binding->callback = callback;
	binding->userptr = userptr;

	hash_table_set(ps_global_bindings, &key, sizeof(Key), binding, sizeof(PsBinding));
}

void ps_render(BOOL is_visible) {
	if (is_visible) {
		for (uint i = 0; i < ps_event_queue_end; i++) {
			PsEvent* event = ps_event_queue + i;

			if (event->type == PS_EVENT_CHARACTER_INPUT) {
				char buffer[64];
				key_repr(buffer, event->event.key, sizeof(buffer));

				PsBinding* binding = hash_table_get(ps_global_bindings, &event->event.key, sizeof(Key));
				if (binding) {
					binding->callback(binding->userptr);
					ps_handled_event(i);
				}
			}
		}

		ps_draw_gui();

		GLint last_vertex_array; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &last_vertex_array);
		GLint last_polygon_mode[2]; glGetIntegerv(GL_POLYGON_MODE, last_polygon_mode);
		GLboolean last_cull_face; glGetBooleanv(GL_CULL_FACE_MODE, &last_cull_face);

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

		if (ps_wireframe)
			glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		else
			glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

		glDisable(GL_CULL_FACE);

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

		if (last_cull_face)
			glEnable(GL_CULL_FACE);

		get_opengl_errors();

	}

	ps_event_queue_end = 0;
}

void ps_begin_scissors(float x, float y, float w, float h) {
	ps_current_scissors.active = GL_TRUE;

	ps_current_scissors.position.x = x;
	ps_current_scissors.position.y = y;
	ps_current_scissors.size.x = w;
	ps_current_scissors.size.y = h;
}

void ps_end_scissors() {
	ps_current_scissors.active = GL_FALSE;
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

void ps_stroke(Vector4 color, float thickness) {
	assert(ps_current_path.flags & PS_PATH_BEING_USED &&
		   !ps_current_scissors.active);

	if (ps_current_path.flags & PS_PATH_CLOSED) {
		Vector2* last_point = dynamic_array_push_back(&ps_current_path.points, 1);
		Vector2* first_point = dynamic_array_at(&ps_current_path.points, 0);
		last_point->x = first_point->x;
		last_point->y = first_point->y;
	}

	PsDrawList* list = ps_ctx.draw_lists[0];
	PsDrawCmd* cmd = dynamic_array_at(&list->commands, 0);

	Vector2* first_point = dynamic_array_at(&ps_current_path.points, 0);
	Vector2* second_point = dynamic_array_at(&ps_current_path.points, 1);

	Vector2 A;
	A.x = second_point->y - first_point->y;
	A.y = first_point->x - second_point->x;

	float mag0 = vector2_magnitude(A);

	float z0 = first_point->x + thickness * (A.x / mag0);
	float w0 = first_point->y + thickness * (A.y / mag0);

	float k0 = first_point->x - thickness * (A.x / mag0);
	float i0 = first_point->y - thickness * (A.y / mag0);

	PsVert* first_verts = dynamic_array_push_back(&list->vbo, 2);

	for (uint i = 0; i < 2; i++) {
		first_verts[i].uv_coords = ps_white_pixel;
		first_verts[i].color = color;
	}

	first_verts[0].position.x = k0;
	first_verts[0].position.y = i0;
	first_verts[1].position.x = z0;
	first_verts[1].position.y = w0;

	float z1, w1, k1, i1,
		z_prime_1, w_prime_1, k_prime_1, i_prime_1;

	if (ps_current_path.points.size <= 2) {
		z1 = second_point->x + thickness * (A.x / mag0);
		w1 = second_point->y + thickness * (A.y / mag0);

		k1 = second_point->x - thickness * (A.x / mag0);
		i1 = second_point->y - thickness * (A.y / mag0);
	}

	for (uint i = 2; i < ps_current_path.points.size; i++) {
		first_point = dynamic_array_at(&ps_current_path.points, i - 2);
		second_point = dynamic_array_at(&ps_current_path.points, i - 1);
		Vector2* third_point = dynamic_array_at(&ps_current_path.points, i);

		if (i != 2) {
			A.x = second_point->y - first_point->y;
			A.y = first_point->x - second_point->x;

			mag0 = vector2_magnitude(A);

			z0 = first_point->x + thickness * (A.x / mag0);
			w0 = first_point->y + thickness * (A.y / mag0);

			k0 = first_point->x - thickness * (A.x / mag0);
			i0 = first_point->y - thickness * (A.y / mag0);
		}

		float z_prime_0 = z0 + (second_point->x - first_point->x);
		float w_prime_0 = w0 + (second_point->y - first_point->y);

		float k_prime_0 = k0 + (second_point->x - first_point->x);
		float i_prime_0 = i0 + (second_point->y - first_point->x);

		A.x = third_point->y - second_point->y;
		A.y = second_point->x - third_point->x;

		float mag1 = vector2_magnitude(A);

		z1 = third_point->x + thickness * (A.x / mag1);
		w1 = third_point->y + thickness * (A.y / mag1);

		k1 = third_point->x - thickness * (A.x / mag1);
		i1 = third_point->y - thickness * (A.y / mag1);

		z_prime_1 = z1 + (third_point->x - second_point->x);
		w_prime_1 = w1 + (third_point->y - second_point->y);

		k_prime_1 = k1 + (third_point->x - second_point->x);
		i_prime_1 = i1 + (third_point->y - second_point->y);

		// Intersection between (z0, w0) -> (z'0, w'0) and (z1, w1) -> (z'1, w'1)
		Vector2 inter1 = vector2_line_intersection((Vector2) { { z0, w0 } }, (Vector2) { { z_prime_0, w_prime_0 } },
												   (Vector2) { { z1, w1 } }, (Vector2) { { z_prime_1, w_prime_1 } });

		// Intersection between (z0, w0) -> (z'0, w'0) and (z1, w1) -> (z'1, w'1)
		Vector2 inter2 = vector2_line_intersection((Vector2) { { k0, i0 } }, (Vector2) { { k_prime_0, i_prime_0 } },
												   (Vector2) { { k1, i1 } }, (Vector2) { { k_prime_1, i_prime_1 } });

		PsVert* new_verts = dynamic_array_push_back(&list->vbo, 2);

		for (uint i = 0; i < 2; i++) {
			new_verts[i].uv_coords = ps_white_pixel;
			new_verts[i].color = color;
		}

		new_verts[1].position.x = inter1.x;
		new_verts[1].position.y = inter1.y;
		new_verts[0].position.x = inter2.x;
		new_verts[0].position.y = inter2.y;
	}

	PsVert* last_verts = dynamic_array_push_back(&list->vbo, 2);

	for (uint i = 0; i < 2; i++) {
		last_verts[i].uv_coords = ps_white_pixel;
		last_verts[i].color = color;
	}

	last_verts[1].position.x = z1;
	last_verts[1].position.y = w1;
	last_verts[0].position.x = k1;
	last_verts[0].position.y = i1;

	size_t new_elements_count = (ps_current_path.points.size - 1) * 6;
	PsIndex* indicies = dynamic_array_push_back(&list->ibo, new_elements_count);

	uint index = 0;

	for (uint i = 0; i < new_elements_count; i += 6) {
		indicies[i + 0] = list->ibo_last_index + 0 + index;
		indicies[i + 1] = list->ibo_last_index + 1 + index;
		indicies[i + 2] = list->ibo_last_index + 2 + index;
		indicies[i + 3] = list->ibo_last_index + 3 + index;
		indicies[i + 4] = list->ibo_last_index + 2 + index;
		indicies[i + 5] = list->ibo_last_index + 1 + index;

		index += 2;
	}

	cmd->elements_count += new_elements_count;
	list->ibo_last_index += ps_current_path.points.size * 2;

	ps_current_path.flags &= ~(PS_PATH_BEING_USED | PS_PATH_CLOSED);
}

void clip(DynamicArray* new_points, Vector2* points, uint64_t poly_size, Vector2 v1, Vector2 v2) {
	for (uint i = 0; i < poly_size; i++) {
		int k = (i + 1) % poly_size;
		Vector2 ivec = points[i],
			kvec = points[k];

		float i_pos = (v2.x - v1.x) * (ivec.y - v1.y) - (v2.y - v1.y) * (ivec.x - v1.x);
		float k_pos = (v2.x - v1.x) * (kvec.y - v1.y) - (v2.y - v1.y) * (kvec.x - v1.x);

		if (i_pos < 0.f && k_pos < 0.f) {
			Vector2* new_point = dynamic_array_push_back(new_points, 1);
			*new_point = kvec;
		} else if (i_pos >= 0.f && k_pos < 0.f) {
			Vector2* points = dynamic_array_push_back(new_points, 2);

			points[0] = vector2_line_intersection(v1, v2, ivec, kvec);
			points[1] = kvec;
		} else if (i_pos < 0.f && k_pos >= 0.f) {
			Vector2* new_point = dynamic_array_push_back(new_points, 1);
			*new_point = vector2_line_intersection(v1, v2, ivec, kvec);
		}
	}
}

void ps_fill(Vector4 color, uint32_t flags) {
	assert(ps_current_path.flags & PS_PATH_BEING_USED);

	PsDrawList* list = ps_ctx.draw_lists[0];
	PsDrawCmd* cmd = dynamic_array_at(&list->commands, 0);

	assert(ps_current_path.points.size >= 3 && (ps_current_path.flags & PS_PATH_CLOSED));

	Vector4 aabb = get_aabb(ps_current_path.points.data, ps_current_path.points.size);
	float width = aabb.z - aabb.x,
		height = aabb.w - aabb.y;

	Vector2 mean_pos = { 0 };

	if (ps_current_scissors.active) {
		DynamicArray new_points;
		DYNAMIC_ARRAY_CREATE(&new_points, Vector2);

		const Vector2 sc_points[] = {
			ps_current_scissors.position,
			{ { ps_current_scissors.position.x,
				  ps_current_scissors.position.y + ps_current_scissors.size.y } },
			{ { ps_current_scissors.position.x + ps_current_scissors.size.x,
				  ps_current_scissors.position.y + ps_current_scissors.size.y } },
			{ { ps_current_scissors.position.x + ps_current_scissors.size.x,
				  ps_current_scissors.position.y } },
		};

		for (uint i = 0; i < 4; i++)
		{
			uint k = (i + 1) % 4;

			clip(&new_points, ps_current_path.points.data, ps_current_path.points.size,
				 sc_points[i], sc_points[k]);
			dynamic_array_copy(&new_points, &ps_current_path.points);
			dynamic_array_clear(&new_points);
		}

		dynamic_array_destroy(&new_points);
	}

	uint64_t count = 0;
	for (uint64_t i = 0; i < ps_current_path.points.size; i++) {
		Vector2* position = dynamic_array_at(&ps_current_path.points, i);
		vector2_add(&mean_pos, mean_pos, *position);

		PsVert* new_vert = dynamic_array_push_back(&list->vbo, 1);
		count++;

		new_vert->color = color;
		new_vert->position = *position;

		if (flags & PS_TEXTURED_POLY) {
			new_vert->uv_coords.x = (position->x - aabb.x) / width;
			new_vert->uv_coords.y = (position->y - aabb.y) / height;
		}
		else {
			new_vert->uv_coords = ps_white_pixel;
		}
	}

	int64_t elements_count = 3 + (count - 3) * 3;
	if (elements_count > 0) {
		PsIndex* new_elements = dynamic_array_push_back(&list->ibo, elements_count);

		new_elements[0] = list->ibo_last_index + 1;
		new_elements[1] = list->ibo_last_index + 2;
		new_elements[2] = list->ibo_last_index;

		for (int64_t j = 0; j < elements_count / 3 - 1; j++) {
			new_elements[j * 3 + 3] = list->ibo_last_index + j + 2;
			new_elements[j * 3 + 4] = list->ibo_last_index + j + 3;
			new_elements[j * 3 + 5] = list->ibo_last_index;
		}

		list->ibo_last_index += count;
		cmd->elements_count += elements_count;
	}

	ps_current_path.flags &= ~PS_PATH_BEING_USED;
}

void ps_fill_rect(float x, float y, float w, float h, Vector4 color) {
	if (ps_current_scissors.active) {
		if (x + w < ps_current_scissors.position.x ||
			x > ps_current_scissors.position.x + ps_current_scissors.size.x ||
			y + h < ps_current_scissors.position.y ||
			y > ps_current_scissors.position.y + ps_current_scissors.size.y)
			return;

		if (x < ps_current_scissors.position.x) {
			w -= ps_current_scissors.position.x - x;
			x = ps_current_scissors.position.x;
		}

		if (y < ps_current_scissors.position.y) {
			h -= ps_current_scissors.position.y - y;
			y = ps_current_scissors.position.y;
		}

		if (x + w > ps_current_scissors.position.x + ps_current_scissors.size.x) {
			w -= (x + w) - (ps_current_scissors.position.x + ps_current_scissors.size.x);
		}

		if (y + h > ps_current_scissors.position.y + ps_current_scissors.size.y) {
			h -= (y + h) - (ps_current_scissors.position.y + ps_current_scissors.size.y);
		}
	}

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

void ps_fill_rect_vertical_gradient(float x, float y, float w, float h, Vector4 top_color, Vector4 bottom_color) {
	if (ps_current_scissors.active) {
		if (x + w < ps_current_scissors.position.x ||
			x > ps_current_scissors.position.x + ps_current_scissors.size.x ||
			y + h < ps_current_scissors.position.y ||
			y > ps_current_scissors.position.y + ps_current_scissors.size.y)
			return;

		if (x < ps_current_scissors.position.x) {
			w -= ps_current_scissors.position.x - x;
			x = ps_current_scissors.position.x;
		}

		if (y < ps_current_scissors.position.y) {
			h -= ps_current_scissors.position.y - y;
			y = ps_current_scissors.position.y;
		}

		if (x + w > ps_current_scissors.position.x + ps_current_scissors.size.x) {
			w -= (x + w) - (ps_current_scissors.position.x + ps_current_scissors.size.x);
		}

		if (y + h > ps_current_scissors.position.y + ps_current_scissors.size.y) {
			h -= (y + h) - (ps_current_scissors.position.y + ps_current_scissors.size.y);
		}
	}

	PsDrawList* list = ps_ctx.draw_lists[0];
	PsDrawCmd* cmd = dynamic_array_at(&list->commands, 0);

	PsVert* rect_verts = dynamic_array_push_back(&list->vbo, 4);
	PsIndex* rect_indexes = dynamic_array_push_back(&list->ibo, 6);

	rect_verts[0].position.x = x;
	rect_verts[0].position.y = y;
	rect_verts[0].uv_coords = ps_white_pixel;
	rect_verts[0].color = bottom_color;

	rect_verts[1].position.x = x + w;
	rect_verts[1].position.y = y;
	rect_verts[1].uv_coords = ps_white_pixel;
	rect_verts[1].color = bottom_color;

	rect_verts[2].position.x = x + w;
	rect_verts[2].position.y = y + h;
	rect_verts[2].uv_coords = ps_white_pixel;
	rect_verts[2].color = top_color;

	rect_verts[3].position.x = x;
	rect_verts[3].position.y = y + h;
	rect_verts[3].uv_coords = ps_white_pixel;
	rect_verts[3].color = top_color;

	rect_indexes[0] = list->ibo_last_index + 0;
	rect_indexes[1] = list->ibo_last_index + 3;
	rect_indexes[2] = list->ibo_last_index + 1;
	rect_indexes[3] = list->ibo_last_index + 3;
	rect_indexes[4] = list->ibo_last_index + 2;
	rect_indexes[5] = list->ibo_last_index + 1;

	cmd->elements_count += 6;
	list->ibo_last_index += 4;
}

void ps_fill_rect_horizontal_gradient(float x, float y, float w, float h, Vector4 left_color, Vector4 right_color) {
	if (ps_current_scissors.active) {
		if (x + w < ps_current_scissors.position.x ||
			x > ps_current_scissors.position.x + ps_current_scissors.size.x ||
			y + h < ps_current_scissors.position.y ||
			y > ps_current_scissors.position.y + ps_current_scissors.size.y)
			return;

		if (x < ps_current_scissors.position.x) {
			w -= ps_current_scissors.position.x - x;
			x = ps_current_scissors.position.x;
		}

		if (y < ps_current_scissors.position.y) {
			h -= ps_current_scissors.position.y - y;
			y = ps_current_scissors.position.y;
		}

		if (x + w > ps_current_scissors.position.x + ps_current_scissors.size.x) {
			w -= (x + w) - (ps_current_scissors.position.x + ps_current_scissors.size.x);
		}

		if (y + h > ps_current_scissors.position.y + ps_current_scissors.size.y) {
			h -= (y + h) - (ps_current_scissors.position.y + ps_current_scissors.size.y);
		}
	}

	PsDrawList* list = ps_ctx.draw_lists[0];
	PsDrawCmd* cmd = dynamic_array_at(&list->commands, 0);

	PsVert* rect_verts = dynamic_array_push_back(&list->vbo, 4);
	PsIndex* rect_indexes = dynamic_array_push_back(&list->ibo, 6);

	rect_verts[0].position.x = x;
	rect_verts[0].position.y = y;
	rect_verts[0].uv_coords = ps_white_pixel;
	rect_verts[0].color = left_color;

	rect_verts[1].position.x = x + w;
	rect_verts[1].position.y = y;
	rect_verts[1].uv_coords = ps_white_pixel;
	rect_verts[1].color = right_color;

	rect_verts[2].position.x = x + w;
	rect_verts[2].position.y = y + h;
	rect_verts[2].uv_coords = ps_white_pixel;
	rect_verts[2].color = right_color;

	rect_verts[3].position.x = x;
	rect_verts[3].position.y = y + h;
	rect_verts[3].uv_coords = ps_white_pixel;
	rect_verts[3].color = left_color;

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

	uint32_t new_index = 0,
		new_count = 0;

	for (uint64_t i = 0, j = 0; str[j] != '\0'; j++) {
		if (str[j] == '\n') {
			y_stride--;
			i = 0;
		}
		else {
			uchar index = str[j];

			if (index > 0x7f)
				index = '?';

			float x_pos = ((index % divisor) * glyph_width) / width,
				y_pos = (1.f - 1 / half_height) - ((index / divisor) * glyph_height) / height;

			Vector2 uv_up_left = { { x_pos, y_pos } };
			Vector2 uv_up_right = { { x_pos + 1.f / half_width, y_pos } };
			Vector2 uv_down_left = { { x_pos, (y_pos + (1.f / half_height)) } };
			Vector2 uv_down_right = { { x_pos + 1.f / half_width, (y_pos + (1.f / half_height)) } };

			float x = position.x + i * size_width,
				y = position.y + y_stride * size_height - size_height,
				w = size_width,
				h = size_height;

			if (ps_current_scissors.active) {
				if (x + w < ps_current_scissors.position.x ||
					x > ps_current_scissors.position.x + ps_current_scissors.size.x ||
					y + h < ps_current_scissors.position.y ||
					y > ps_current_scissors.position.y + ps_current_scissors.size.y) {
					i++;
					continue;
				}

				if (x < ps_current_scissors.position.x) {
					float old_w = w;
					w -= ps_current_scissors.position.x - x;
					uv_up_left.x += (1.f - (w / old_w)) / half_width;
					uv_down_left.x += (1.f - (w / old_w)) / half_width;

					x = ps_current_scissors.position.x;
				}

				if (y < ps_current_scissors.position.y) {
					float old_h = h;
					h -= ps_current_scissors.position.y - y;
					uv_up_left.y += (1.f - (h / old_h)) / half_height;
					uv_up_right.y += (1.f - (h / old_h)) / half_height;

					y = ps_current_scissors.position.y;
				}

				if (x + w > ps_current_scissors.position.x + ps_current_scissors.size.x) {
					float old_w = w;
					w -= (x + w) - (ps_current_scissors.position.x + ps_current_scissors.size.x);

					uv_up_right.x -= (1.f - (w / old_w)) / half_width;
					uv_down_right.x -= (1.f - (w / old_w)) / half_width;
				}

				if (y + h > ps_current_scissors.position.y + ps_current_scissors.size.y) {
					float old_h = h;
					h -= (y + h) - (ps_current_scissors.position.y + ps_current_scissors.size.y);

					uv_down_right.y -= (1.f - (h / old_h)) / half_height;
					uv_down_left.y -= (1.f - (h / old_h)) / half_height;
				}
			}

			Vector2 vertex_up_left = { { x, y } };
			Vector2 vertex_up_right = { { x + w, y } };
			Vector2 vertex_down_left = { { x, y + h } };
			Vector2 vertex_down_right = { { x + w, y + h } };

			PsVert* vertices = dynamic_array_push_back(&list->vbo, 4);
			PsIndex* indexes = dynamic_array_push_back(&list->ibo, 6);

			vertices[0].color = color;
			vertices[0].position = vertex_up_left;
			vertices[0].uv_coords = uv_up_left;

			vertices[1].color = color;
			vertices[1].position = vertex_down_left;
			vertices[1].uv_coords = uv_down_left;

			vertices[2].color = color;
			vertices[2].position = vertex_down_right;
			vertices[2].uv_coords = uv_down_right;

			vertices[3].color = color;
			vertices[3].position = vertex_up_right;
			vertices[3].uv_coords = uv_up_right;

			indexes[0] = list->ibo_last_index + element_index * 4 + 0;
			indexes[1] = list->ibo_last_index + element_index * 4 + 3;
			indexes[2] = list->ibo_last_index + element_index * 4 + 1;
			indexes[3] = list->ibo_last_index + element_index * 4 + 3;
			indexes[4] = list->ibo_last_index + element_index * 4 + 2;
			indexes[5] = list->ibo_last_index + element_index * 4 + 1;

			element_index++;
			i++;

			new_index += 4;
			new_count += 6;
		}
	}

	list->ibo_last_index += new_index;
	cmd->elements_count += new_count;
}

float ps_font_width(float size) {
	const float	glyph_width = ps_current_font.glyph_width,
		glyph_height = ps_current_font.glyph_height;

	return size * (glyph_width / glyph_height);
}

#define WINDOW_DEFAULT_WIDTH 400.f
#define WINDOW_DEFAULT_HEIGHT 200.f

void ps_widget_init(PsWidget* widget, PsDrawFunction draw, PsMinSizeFunction min_size) {
	widget->parent = NULL;
	widget->draw = draw;
	widget->min_size = min_size;
	widget->flags = 0;
}

void ps_container_init(PsContainer* container, PsDrawFunction draw, PsMinSizeFunction min_size, float spacing) {
	ps_widget_init(PS_WIDGET(container), draw, min_size);
	DYNAMIC_ARRAY_CREATE(&container->children, PsWidget*);

	container->spacing = spacing;
}

void ps_container_add(PsWidget* container_widget, PsWidget* widget) {
	PsContainer* container = PS_CONTAINER(container_widget);

	PsWidget** new_widget = dynamic_array_push_back(&container->children, 1);
	*new_widget = widget;
	(*new_widget)->parent = container_widget;
}

PsWindow* ps_window_create(char* title) {
	PsWindow* window = malloc(sizeof(PsWindow));
	ps_windows[ps_windows_count++] = window;

	assert(ps_windows_count < PS_MAX_WINDOWS);

	window->size.x = WINDOW_DEFAULT_WIDTH;
	window->size.y = WINDOW_DEFAULT_HEIGHT;

	float half_width = ps_ctx.display_size.x / 2,
		half_height = ps_ctx.display_size.y / 2;

	window->position.x = random_uniform(-half_width, half_width - window->size.x);
	window->position.y = random_uniform(-half_height, half_height - window->size.y);

	window->title = title;
	window->flags = PS_WINDOW_RESIZABLE_BIT;

	window->root = NULL;

	return window;
}

void ps_window_set_root(PsWindow* window, PsWidget* root_widget) {
	window->root = root_widget;
}

void ps_window_destroy(PsWindow* window) {
	free(window);

	uint64_t i;
	for (i = 0; i < ps_windows_count; i++)
		if (ps_windows[i] == window)
			break;

	free(ps_windows[i]);
	ps_windows[i] = ps_windows[--ps_windows_count];
}

void ps_widget_draw(PsWidget* widget, Vector2 anchor, Vector2 min_pos) {
	widget->draw(widget, anchor, min_pos);
}

Vector2 ps_widget_min_size(PsWidget* widget) {
	return widget->min_size(widget);
}

Vector2 ps_window_anchor(PsWindow* window) {
	return (Vector2) {
		{
			window->position.x + 5.f,
			window->position.y + window->size.y - 5.f
		}
	};
}

void ps_widget_update(PsWidget* widget, float x, float y, float w, float h) {
	widget->flags &= ~PS_WIDGET_CLICKED;

	if (widget->flags & PS_WIDGET_SELECTED) {
		if (!g_window.mouse_button_left_state && widget->flags & PS_WIDGET_CLICKING)
			widget->flags |= PS_WIDGET_CLICKED;

		widget->flags &= ~PS_WIDGET_CLICKING;

		if (is_in_box(g_window.cursor_position, x, y, w, h)) {
			widget->flags |= PS_WIDGET_HOVERED;

			if (g_window.mouse_button_left_state)
				widget->flags |= PS_WIDGET_CLICKING;
		}
		else {
			widget->flags &= ~(PS_WIDGET_HOVERED | PS_WIDGET_CLICKING);
		}
	}
}

static Vector4 ps_window_background_color = { { 0.1f, 0.1f, 0.1f, 0.9f } };
static Vector4 ps_window_border_active_color = { { 0.5f, 0.5f, 0.5f, 1.f } };
static Vector4 ps_window_border_inactive_color = { { 0.f, 0.f, 1.f, 1.f } };
static Vector4 resize_triangle_color = { { 0.3f, 0.3f, 0.35f, 0.7f } };
static float ps_window_border_size = 2.f;

BOOL ps_window_inside(PsWindow* window, Vector2 point) {
	return vector2_inside_rectangle(point, window->position.x, window->position.y,
									window->size.x + ps_window_border_size,
									window->size.y + ps_window_border_size);
}

BOOL ps_window_resize_triangle_inside(PsWindow* window, Vector2 point) {
	Vector2 a = { { window->position.x + window->size.x, window->position.y } };
	Vector2 b = { { window->position.x + window->size.x - 30.f, window->position.y } };
	Vector2 c = { { window->position.x + window->size.x, window->position.y + 30.f } };

	return vector2_inside_triangle(point, a, b, c);
}

Vector2 ps_window_min_size(PsWindow* window) {
	Vector2 root_min_size = { { 100.f, 100.f } };

	if (window->root) {
		root_min_size = ps_widget_min_size(window->root);
	}

	root_min_size.x += 10.f;
	root_min_size.y += 10.f;

	return root_min_size;
}

float ps_text_width(const char* text, float size) {
	uint c = 0;
	uint max_c = 0;

	do {
		if(*text != '\n') {
			c++;
		}
		else {
			if (c > max_c)
				max_c = c;

			c = 0;
		}
	} while (*++text != '\0');

	if (c > max_c)
		max_c = c;

	return max_c * ps_font_width(size);
}

float ps_text_height(const char* text, float size) {
	uint c = 1;

	do {
		if(*text == '\n')
			c++;
	} while (*++text != '\0');

	return c * size;
}

void ps_menubar_draw() {
	PsWindow* selected_window;
	if (ps_windows_count > 0)
		selected_window = ps_windows[ps_windows_count - 1];
	else
		selected_window = NULL;

	char title_format[1024];

	if (ps_windows_count > 0) {
		strncpy(title_format, selected_window->title, sizeof(title_format));
	}
	else {
		strcpy(title_format, "Desktop");
	}

	ps_fill_rect(-g_window.size.x / 2, g_window.size.y / 2 - 24.f, g_window.size.x, 24.f, (Vector4) { { 0.2f, 0.2f, 0.2f, 0.9f } });
	ps_fill_rect_vertical_gradient(-g_window.size.x / 2, g_window.size.y / 2 - 48.f, g_window.size.x, 24.f, (Vector4) { { 0.f, 0.f, 0.f, 0.5f } }, (Vector4) { { 0.f, 0.f, 0.f, 0.f } });

	char buf[256];
	snprintf(buf, sizeof(buf), "%lu FPS", g_window.fps);

	float fps_size = ps_text_width(buf, 20.f);
	ps_text(buf, (Vector2) { { g_window.size.x / 2 - fps_size, g_window.size.y / 2 - 4.f } }, 20.f,	(Vector4){ { 0.9f, 0.9f, 0.9f, 1.f } });
	ps_text(title_format, (Vector2) { { -g_window.size.x / 2 + 10.f, g_window.size.y / 2 - 4.f } }, 20.f, (Vector4) { { 0.9f, 0.9f, 0.9f, 1.f } });
}

void ps_window_draw(PsWindow* window) {
	Vector4 border_color;

	if (window->flags & PS_WINDOW_SELECTED_BIT) {
		ps_window_handle_events(window);
		border_color = ps_window_border_active_color;
	}
	else {
		border_color = ps_window_border_inactive_color;
	}

	Vector4 brighter_border_color = border_color;
	brighter_border_color.x += 0.5;
	brighter_border_color.y += 0.5;
	brighter_border_color.z += 0.5;

	Vector2 min_window_size = ps_window_min_size(window);

	window->size.x = max(min_window_size.x, window->size.x);

	float old_size = window->size.y;
	window->size.y = max(min_window_size.y, window->size.y);
	window->position.y += old_size - window->size.y;

	ps_fill_rect(window->position.x, window->position.y, window->size.x, window->size.y, ps_window_background_color); /* Background */

	float o_ax = window->position.x - ps_window_border_size,
		o_ay = window->position.y + window->size.y + ps_window_border_size,
		o_bx = o_ax + window->size.x + 2 * ps_window_border_size,
		o_by = o_ay,
		o_cx = o_bx,
		o_cy = window->position.y - ps_window_border_size,
		o_dx = o_ax,
		o_dy = o_cy,
		i_ax = window->position.x,
		i_ay = window->position.y + window->size.y,
		i_bx = window->position.x + window->size.x,
		i_by = i_ay,
		i_cx = i_bx,
		i_cy = window->position.y,
		i_dx = i_ax,
		i_dy = i_cy;

	ps_begin_path();			/* Top bar */
	ps_line_to(i_ax, i_ay);
	ps_line_to(i_bx, i_by);
	ps_line_to(o_bx, o_by);
	ps_line_to(o_ax, o_ay);
	ps_close_path();
	ps_fill(border_color, PS_FILLED_POLY);

	ps_fill_rect_vertical_gradient(o_ax - ps_window_border_size, o_ay,
								   o_bx - o_ax + ps_window_border_size * 2, 5.f,
								   (Vector4) { { 0.f, 0.f, 0.f, 0.f } },
								   (Vector4) { { 0.f, 0.f, 0.f, 0.7f } });

	ps_begin_path();			/* Left bar */
	ps_line_to(i_ax, i_ay);
	ps_line_to(i_dx, i_dy);
	ps_line_to(o_dx, o_dy);
	ps_line_to(o_ax, o_ay);
	ps_close_path();
	ps_fill(border_color, PS_FILLED_POLY);

	ps_fill_rect_horizontal_gradient(o_ax - 5.f, o_ay, 5.f, o_dy - o_ay,
									 (Vector4) { { 0.f, 0.f, 0.f, 0.f } },
									 (Vector4) { { 0.f, 0.f, 0.f, 0.7f } });

	ps_begin_path();			/* Right bar */
	ps_line_to(i_bx, i_by);
	ps_line_to(i_cx, i_cy);
	ps_line_to(o_cx, o_cy);
	ps_line_to(o_bx, o_by);
	ps_close_path();
	ps_fill(border_color, PS_FILLED_POLY);

	ps_fill_rect_horizontal_gradient(o_bx, o_ay, 5.f, o_dy - o_ay,
									 (Vector4) { { 0.f, 0.f, 0.f, 0.7f } },
									 (Vector4) { { 0.f, 0.f, 0.f, 0.f } });

	ps_begin_path();			/* Bottom bar */
	ps_line_to(i_cx, i_cy);
	ps_line_to(i_dx, i_dy);
	ps_line_to(o_dx, o_dy);
	ps_line_to(o_cx, o_cy);
	ps_close_path();
	ps_fill(border_color, PS_FILLED_POLY);

	ps_fill_rect_vertical_gradient(o_dx - ps_window_border_size, o_dy - 5.f,
								   o_cx - o_dx + ps_window_border_size * 2, 5.f,
								   (Vector4) { { 0.f, 0.f, 0.f, 0.7f } },
								   (Vector4) { { 0.f, 0.f, 0.f, 0.f } });

	if (window->root) {
		if (window->flags & PS_WINDOW_SELECTED_BIT &&
			!(window->flags & PS_WINDOW_DRAGGING_BIT || window->flags & PS_WINDOW_RESIZE_BIT))
			window->root->flags |= PS_WIDGET_SELECTED;

		ps_widget_draw(PS_WIDGET(window->root), ps_window_anchor(window), window->size);
		window->root->flags &= ~PS_WIDGET_SELECTED;
	}

	ps_begin_path();
	ps_line_to(window->position.x + window->size.x, window->position.y);
	ps_line_to(window->position.x + window->size.x - 30.f, window->position.y);
	ps_line_to(window->position.x + window->size.x, window->position.y + 30.f);
	ps_close_path();
	ps_fill(resize_triangle_color, PS_FILLED_POLY);
}

void ps_draw_gui() {
	if (ps_windows_count > 0) {
		ps_windows[ps_windows_count - 1]->flags |= PS_WINDOW_SELECTED_BIT;

		for (uint64_t i = 0; i < ps_windows_count; i++) {
			ps_window_draw(ps_windows[i]);
			ps_windows[i]->flags &= ~PS_WINDOW_SELECTED_BIT;
		}
	}

	ps_menubar_draw();
}

void ps_window_switch_to(uint64_t id) {
	PsWindow* temp = ps_windows[id];

	for (uint32_t i = id; i < ps_windows_count - 1; i++) {
		ps_windows[i] = ps_windows[i + 1];
	}

	ps_windows[ps_windows_count - 1] = temp;
}

void ps_window_handle_events(PsWindow* selected_window) {
	Vector2 pointer_position = g_window.cursor_position;

	int state = g_window.mouse_button_left_state;

	static Vector2 window_drag_anchor = { { 0, 0 } };
	static Vector2 window_original_size = { { 0, 0 } };

	if (state == GLFW_PRESS) {
		if (selected_window->flags & PS_WINDOW_DRAGGING_BIT ||
			(ps_window_inside(selected_window, pointer_position) && g_window.keys[GLFW_KEY_LEFT_ALT]))
		{
			if (!(selected_window->flags & PS_WINDOW_DRAGGING_BIT))
				vector2_sub(&window_drag_anchor, selected_window->position, pointer_position);

			selected_window->flags |= PS_WINDOW_DRAGGING_BIT;
			vector2_add(&selected_window->position, pointer_position, window_drag_anchor);

		}
		else if (selected_window->flags & PS_WINDOW_RESIZABLE_BIT &&
				 (selected_window->flags & PS_WINDOW_RESIZE_BIT ||
				  ps_window_resize_triangle_inside(selected_window, pointer_position)))
		{
			if (!(selected_window->flags & PS_WINDOW_DRAGGING_BIT)) {
				window_original_size = selected_window->size;
				window_drag_anchor = selected_window->position;
			}

			selected_window->flags |= PS_WINDOW_RESIZE_BIT;

			Vector2 window_min_size = ps_window_min_size(selected_window);

			selected_window->size.x = max(window_min_size.x,
										  pointer_position.x - selected_window->position.x);

			float old_size = selected_window->size.y;
			selected_window->size.y = max(window_min_size.y,
										  (window_drag_anchor.y + window_original_size.y) - pointer_position.y);
			selected_window->position.y += old_size - selected_window->size.y;
		}
		else if (selected_window->flags & PS_WINDOW_SELECTION_BIT ||
				 ps_window_inside(selected_window, pointer_position))
		{
			selected_window->flags |= PS_WINDOW_SELECTION_BIT;
			// TODO
		}
		else {
			for (int i = ps_windows_count - 1; i >= 0; i--) {
				if (ps_window_inside(ps_windows[i], pointer_position)) {
					if (i != ps_windows_count - 1)
						ps_window_switch_to(i);

					break;
				}
			}
		}
	}
	if (state == GLFW_RELEASE) {
		selected_window->flags &= ~(PS_WINDOW_DRAGGING_BIT | PS_WINDOW_SELECTION_BIT | PS_WINDOW_RESIZE_BIT);
	}
}

Vector2 ps_box_size(PsWidget* widget) {
	DynamicArray* children = &PS_CONTAINER(widget)->children;

	PsBox* box = (PsBox*)widget;

	float max_min_size_x = 0.f,
		max_min_size_y = 0.f;

	for (uint i = 0; i < children->size; i++) {
		PsWidget* widget = *(PsWidget**)dynamic_array_at(children, i);

		if (box->direction == PS_DIRECTION_VERTICAL) {
			Vector2 size = ps_widget_min_size(widget);

			float displacement_size;

			if (i != children->size - 1)
				displacement_size = size.y + PS_CONTAINER(box)->spacing;
			else
				displacement_size = size.y;

			max_min_size_y += displacement_size;

			if (size.x > max_min_size_x)
				max_min_size_x = size.x;
		}
		else if (box->direction == PS_DIRECTION_HORIZONTAL) {
			Vector2 size = ps_widget_min_size(widget);

			float displacement_size;

			if (i != children->size - 1)
				displacement_size = size.x + PS_CONTAINER(box)->spacing;
			else
				displacement_size = size.x;

			max_min_size_x += displacement_size;

			if (size.y > max_min_size_y)
				max_min_size_y = size.y;
		}

		widget->flags &= ~PS_WIDGET_SELECTED;
	}

	return (Vector2) { { max_min_size_x, max_min_size_y } };
}


void ps_box_draw(PsWidget* box_widget, Vector2 anchor, Vector2 min_size) {
	DynamicArray* children = &PS_CONTAINER(box_widget)->children;

	PsBox* box = (PsBox*)box_widget;

	Vector2 max_min_size = ps_box_size(box_widget);

	for (uint i = 0; i < children->size; i++) {
		PsWidget* widget = *(PsWidget**)dynamic_array_at(children, i);

		if (box_widget->flags & PS_WIDGET_SELECTED)
			widget->flags |= PS_WIDGET_SELECTED;

		if (box->direction == PS_DIRECTION_VERTICAL) {
			widget->draw(widget, anchor, (Vector2) { { max(min_size.x, max_min_size.x), 0.f } });
			Vector2 size = ps_widget_min_size(widget);

			float displacement_size;

			if (i != children->size - 1)
				displacement_size = size.y + PS_CONTAINER(box)->spacing;
			else
				displacement_size = size.y;

			anchor.y -= displacement_size;
		}
		else if (box->direction == PS_DIRECTION_HORIZONTAL) {
			widget->draw(widget, anchor, (Vector2) { { 0.f, max(min_size.y, max_min_size.y) } });
			Vector2 size = ps_widget_min_size(widget);

			float displacement_size;

			if (i != children->size - 1)
				displacement_size = size.x + PS_CONTAINER(box)->spacing;
			else
				displacement_size = size.x;

			anchor.x += displacement_size;
		}

		widget->flags &= ~PS_WIDGET_SELECTED;
	}
}

PsWidget* ps_box_create(PsDirection direction, float spacing) {
	PsBox* box = malloc(sizeof(PsBox));
	ps_container_init(PS_CONTAINER(box), ps_box_draw, ps_box_size, spacing);
	box->direction = direction;

	return PS_WIDGET(box);
}

Vector2 ps_label_min_size(PsWidget* widget) {
	PsLabel* label = (PsLabel*)widget;

	return label->size;
}

void ps_label_draw(PsWidget* widget, Vector2 anchor, Vector2 min_size) {
	PsLabel* label = (PsLabel*)widget;
	anchor.x += max(0.f, min_size.x / 2.f - label->size.x / 2.f);
	anchor.y -= max(0.f, min_size.y / 2.f - label->size.y / 2.f);

	ps_text(label->text, anchor, label->text_size, label->color);
}

char* ps_label_text(PsLabel* label) {
	return label->text;
}

void ps_label_set_text(PsLabel* label, char* text) {
	free(label->text);
	label->text = m_strdup(text);
	label->size.x = ps_text_width(label->text, label->text_size);
	label->size.y = ps_text_height(label->text, label->text_size);
}

PsWidget* ps_label_create(char* text, float size) {
	PsLabel* label = malloc(sizeof(PsLabel));

	label->color = (Vector4) { { 1.f, 1.f, 1.f, 1.f } };
	label->text = m_strdup(text);
	label->text_size = size;

	label->size.x = ps_text_width(text, size);
	label->size.y = ps_text_height(text, size);

	ps_widget_init(PS_WIDGET(label), ps_label_draw, ps_label_min_size);

	return PS_WIDGET(label);
}

static float button_padding = 5.f,
	button_border_size = 2.f;

static Vector4 button_background_color = { { 0.5f, 0.5f, 1.f, 1.f } },
	button_text_color = { { 0.f, 0.f, 0.f, 1.f } },
	button_border_color = { { 0.1f, 0.1f, 0.1f, 0.5f } };

Vector2 ps_button_min_size(PsWidget* widget) {
	PsButton* button = (PsButton*)widget;

	return (Vector2) { { button->size.x + button_padding * 2,
			   	button->size.y + button_padding * 2 } };
}

void ps_button_draw(PsWidget* widget, Vector2 anchor, Vector2 min_size) {
	PsButton* button = (PsButton*)widget;

	float button_width = fmax(button->size.x + button_padding * 2, min_size.x - button_padding * 2);
	float button_height = fmax(button->size.y + button_padding * 2, min_size.y - button_padding * 2);

	float x = anchor.x,
		y = anchor.y - button_height,
		w = button_width,
		h = button_height;

	ps_widget_update(widget, x, y, w, h);

	Vector4 bc_color = button_background_color,
		txt_color = button_text_color;

	if (PS_WIDGET(button)->flags & PS_WIDGET_HOVERED) {
		for (uint i = 0; i < 4; i++) {
			bc_color.D[i] *= 1.4;
			txt_color.D[i] *= 0.8;
		}
	}

	if (PS_WIDGET(button)->flags & PS_WIDGET_CLICKING) {
		for (uint i = 0; i < 4; i++) {
			bc_color.D[i] *= 0.5;
			txt_color.D[i] *= 1.5;
		}
	}

	ps_fill_rect(x, y, w, h, bc_color);
	ps_fill_rect(x, y, w, button_border_size, button_border_color);
	ps_fill_rect(x, y, button_border_size, h, button_border_color);

	float text_width = ps_text_width(button->text, button->text_size);
	float text_height = ps_text_height(button->text, button->text_size);

	anchor.x += button_width / 2 - text_width / 2;
	anchor.y -= button_height / 2 - text_height / 2;

	ps_text(button->text, anchor, button->text_size, txt_color);
}

uint8_t ps_widget_state(PsWidget* widget) {
	return widget->flags;
}

PsWidget* ps_button_create(char* text, float size) {
	PsButton* button = malloc(sizeof(PsButton));

	button->text = text;
	button->text_size = size;

	button->size.x = ps_text_width(text, size);
	button->size.y = ps_text_height(text, size);

	ps_widget_init(PS_WIDGET(button), ps_button_draw, ps_button_min_size);

	return PS_WIDGET(button);
}

static float slider_margin = 3.f;
static Vector4 slider_background_color = { { 0.05f, 0.05f, 0.05f, 1.f } },
	slider_foreground_color =  { { 0.3f, 0.1f, 0.3f, 1.f } },
	slider_text_color = { { 1.f, 1.f, 1.f, 1.f } };

Vector2 ps_slider_min_size(PsWidget* widget) {
	PsSlider* slider = (PsSlider*)widget;

	char buffer[1024];
	snprintf(buffer, sizeof(buffer), "%.2f\n", *slider->val);

	return (Vector2) { { ps_text_width(buffer, slider->text_size),
				ps_text_height(buffer, slider->text_size) } };

}

void ps_slider_draw(PsWidget* widget, Vector2 anchor, Vector2 min_size) {
	PsSlider* slider = (PsSlider*)widget;

	float slider_width = max(slider->width, min_size.x - 10.f);

	float x = anchor.x,
		h = slider->text_size + slider_margin * 2,
		y = anchor.y - h,
		w = slider_width;

	Vector4 fg_color = slider_foreground_color,
		bg_color = slider_background_color,
		txt_color = slider_text_color;

	ps_widget_update(widget, x, y, w, h);

	if (PS_WIDGET(slider)->flags & PS_WIDGET_CLICKING) {
		for (uint i = 0; i < 4; i++) {
			fg_color.D[i] *= 1.6;
			bg_color.D[i] *= 1.6;
			txt_color.D[i] *= 0.7;
		}

		*slider->val = slider->min_val + ((g_window.cursor_position.x - x) / slider_width) *
			(slider->max_val - slider->min_val);

		if (slider->callback)
			slider->callback();
	}
	else if (PS_WIDGET(slider)->flags & PS_WIDGET_HOVERED) {
		for (uint i = 0; i < 4; i++) {
			fg_color.D[i] *= 1.4;
			bg_color.D[i] *= 1.6;
		}
	}

	ps_fill_rect(x, y, w, h, bg_color);
	ps_fill_rect(x, y, w * ((*slider->val - slider->min_val) / (slider->max_val - slider->min_val)), h, fg_color);

	char buffer[1024];
	snprintf(buffer, sizeof(buffer), "%.2f\n", *slider->val);

	ps_text(buffer, (Vector2) { {
				x + slider_width / 2 - ps_text_width(buffer, slider->text_size) / 2,
					anchor.y - slider_margin
					} },
		slider->text_size, slider_text_color);
}

PsWidget* ps_slider_create(float* val, float min_val, float max_val, float text_size, void (*callback)()) {
	PsSlider* slider = malloc(sizeof(PsSlider));

	slider->val = val;
	slider->min_val = min_val;
	slider->max_val = max_val;
	slider->text_size = text_size;

	char buffer[128];
	snprintf(buffer, sizeof(buffer), "%.2f", max_val);

	slider->width = ps_text_width(buffer, text_size);
	slider->callback = callback;

	ps_widget_init(PS_WIDGET(slider), ps_slider_draw, ps_slider_min_size);
	return PS_WIDGET(slider);
}

static float input_border_size = 2.f,
	input_cursor_size = 2.f;

static Vector4 input_background_color = { { 0.f, 0.f, 0.f, 1.f } },
	input_border_color = { { 1.f, 1.f, 1.f, 1.f } },
	input_cursor_color = { { 1.f, 0.f, 0.f, 1.f } },
	input_text_color = { { 1.f, 1.f, 1.f, 1.f } };

void ps_input_insert_at_point(PsInput* input, char* string) {
	char temp[INPUT_MAX_ENTERED_TEXT];

	strinsert(temp, input->value, string, input->cursor_position, sizeof(temp));
	strncpy(input->value, temp, sizeof(temp));

	input->cursor_position += strlen(string);
}

uint ps_input_beginning_of_line(PsInput* input) {
	if (input->cursor_position == 0)
		return 0;

	uint i = 0;
	do {
		i++;
		input->cursor_position--;
	} while (input->value[input->cursor_position] != '\n' &&
			 input->cursor_position > 0);

	if (input->cursor_position > 0) {
		input->cursor_position++;
		return i - 1;
	}

	return i;
}

uint ps_input_end_of_line(PsInput* input) {
	uint i = 0;

	while (input->value[input->cursor_position] != '\0' &&
		   input->value[input->cursor_position] != '\n')
	{
		i++;
		input->cursor_position++;
	}

	return i;
}

Vector2 ps_input_min_size(PsWidget* widget) {
	PsInput* input = (PsInput*)widget;

	return (Vector2) { { input->width + input_border_size * 2,
				ps_text_height(input->value, input->text_size) + input_border_size * 2 } };
}

void ps_input_draw(PsWidget* widget, Vector2 anchor, Vector2 min_size) {
	PsInput* input = (PsInput*)widget;

	input->width = ps_text_width(input->value, input->text_size) + input_border_size;

	input->lines_count = strcount(input->value, '\n') + 1;
	float input_width = max(min_size.x - 10.f, input->width);

	float x = anchor.x + input_border_size,
		h = input->text_size * input->lines_count,
		y = (anchor.y - input_border_size) - h,
		w = input_width;

	Vector4 bc_color = input_background_color;

	ps_widget_update(widget, x, y, w, h);

	if (PS_WIDGET(input)->flags & PS_WIDGET_SELECTED) {
		if (PS_WIDGET(input)->flags & PS_WIDGET_CLICKING) {
			uint y_cursor_pos = input->lines_count - (g_window.cursor_position.y - y) / input->text_size;

			uint i = 0, j = 0;
			char* c = input->value;
			while (*c != '\0' && j < y_cursor_pos) {
				if (*c++ == '\n')
					j++;
				i++;
			}

			uint line_size = 0;
			while (*c != '\0' && *c++ != '\n')
				line_size++;

			uint x_cursor_pos = roundf((g_window.cursor_position.x - x) / ps_font_width(input->text_size));
			x_cursor_pos = min(x_cursor_pos, line_size);

			input->cursor_position = x_cursor_pos + i;
			input->selected = GL_TRUE;
		}
		else if (g_window.mouse_button_left_state) {
			input->selected = GL_FALSE;
		}

		if (PS_WIDGET(input)->flags & PS_WIDGET_HOVERED || PS_WIDGET(input)->flags & PS_WIDGET_CLICKING) {
			for (uint i = 0; i < 4; i++) {
				bc_color.D[i] += 0.1;
			}
		}
	}
	else {
		input->selected = GL_FALSE;
	}

	if (input->selected) {
		for (uint i = 0; i < ps_event_queue_end; i++) {
			if (ps_event_queue[i].type == PS_EVENT_CHARACTER_INPUT) {
				Key key = ps_event_queue[i].event.key;

				if (key_equal(key, (Key) { KEY_DEL, 0 })) {
					if (input->cursor_position > 0) {
						uint index = --input->cursor_position;
						memcpy(input->value + index, input->value + index + 1, strlen(input->value) - index);
					}

					ps_handled_event(i);
				}
				else if (key_equal(key, (Key) { KEY_TAB, 0 })) {
					ps_input_insert_at_point(input, "    ");

					ps_handled_event(i);
				}
				else if (key_equal(key, (Key) { KEY_LEFT, 0 })) {
					if (input->cursor_position > 0)
						input->cursor_position--;

					ps_handled_event(i);
				}
				else if (key_equal(key, (Key) { KEY_RIGHT, 0 })) {
					if (input->cursor_position < strlen(input->value))
						input->cursor_position++;

					ps_handled_event(i);
				}
				else if (key_equal(key, (Key) { KEY_UP, 0 })) {
					uint line_size = ps_input_beginning_of_line(input);

					if (input->cursor_position > 0) {
						input->cursor_position--;
						uint second_line_size = ps_input_beginning_of_line(input);
						input->cursor_position += min(line_size, second_line_size);
					}

					ps_handled_event(i);
				}
				else if (key_equal(key, (Key) { KEY_DOWN, 0 })) {
					uint line_size = ps_input_beginning_of_line(input);
					input->cursor_position += line_size;

					ps_input_end_of_line(input);

					if (input->value[input->cursor_position] != '\0') {
						input->cursor_position++;
						uint saved_pos = input->cursor_position;
						uint second_line_size = ps_input_end_of_line(input);

						input->cursor_position = saved_pos + min(line_size, second_line_size);
					}

					ps_handled_event(i);
				}
				else if (key.modifiers == 0) {
					char new_string[5];

					u_codepoint_to_string(new_string, key.code);
					ps_input_insert_at_point(input, new_string);

					ps_handled_event(i);
				}
				else {
					char buffer[16];
					key_repr(buffer, key, sizeof(buffer) - 1);

					printf("No binding to %s\n", buffer);
				}
			}
		}
	}

	ps_fill_rect(x - input_border_size, y, input_border_size, h, input_border_color);
	ps_fill_rect(x + w, y, input_border_size, h, input_border_color);

	ps_fill_rect(x - input_border_size, y - input_border_size, w + input_border_size * 2, input_border_size, input_border_color);
	ps_fill_rect(x - input_border_size, y + h, w + input_border_size * 2, input_border_size, input_border_color);

	ps_fill_rect(x, y, w, h, bc_color);

	ps_text(input->value, (Vector2) { { x, y + h } }, input->text_size, input_text_color);

	uint pos_x = 0, pos_y = 0, update = 0;
	int i = input->cursor_position;

	while (i-- > 0) {
		if (input->value[i] == '\n') {
			update = GL_TRUE;
			pos_y++;
		}
		else if (!update) {
			pos_x++;
		}
	}

	if (input->selected)
		ps_fill_rect(x + ps_font_width(input->text_size) * pos_x, y + h - input->text_size * (pos_y + 1), input_cursor_size, input->text_size, input_cursor_color);
}

char* ps_input_value(PsInput* input) {
	return input->value;
}

void ps_input_set_value(PsInput* input, const char* value) {
	strncpy(input->value, value, INPUT_MAX_ENTERED_TEXT);
}

PsWidget* ps_input_create(char* value, float text_size) {
	PsInput* input = malloc(sizeof(PsInput));

	input->text_size = text_size;
	input->cursor_position = 0;
	input->lines_count = 0;
	input->selected = GL_FALSE;

	ps_widget_init(PS_WIDGET(input), ps_input_draw, ps_input_min_size);

	ps_input_set_value(input, value);
	return PS_WIDGET(input);
}

Vector2 ps_canvas_min_size(PsWidget* widget) {
	PsCanvas* canvas = (PsCanvas*)widget;

	return (Vector2) { { canvas->width, canvas->height } };
}

void ps_canvas_draw(PsWidget* widget, Vector2 anchor, Vector2 min_size) {
	PsCanvas* canvas = (PsCanvas*)widget;
	anchor.y -= canvas->height;

	ps_widget_update(widget, anchor.x, anchor.y, canvas->width, canvas->height);
	ps_fill_rect(anchor.x, anchor.y, canvas->width, canvas->height, (Vector4) { { 0.f, 0.f, 0.f, 1.f } });

	ps_begin_scissors(anchor.x, anchor.y, canvas->width, canvas->height);
	canvas->draw_fn(widget, anchor, (Vector2) { { canvas->width, canvas->height } });
	ps_end_scissors();
}

PsWidget* ps_canvas_create(float width, float height, void (*draw_fn)(PsWidget*, Vector2, Vector2)) {
	PsCanvas* canvas = malloc(sizeof(PsCanvas));

	canvas->width = width;
	canvas->height = height;
	canvas->draw_fn = draw_fn;

	ps_widget_init(PS_WIDGET(canvas), ps_canvas_draw, ps_canvas_min_size);
	return PS_WIDGET(canvas);
}
