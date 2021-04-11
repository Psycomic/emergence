#include "psyche.h"
#include "drawable.h"

#include <string.h>
#include <assert.h>
#include <math.h>

GLuint ps_vbo;
GLuint ps_ibo;

GLuint ps_vao;

GLuint ps_shader;
GLuint ps_matrix_location;
GLuint ps_texture_location;

PsDrawData ps_ctx;
PsPath ps_current_path;
PsAtlas ps_atlas;

GLuint ps_default_texture;

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

void ps_atlas_init(const char* image_path) {
	Image image;
	if (image_load_bmp(&image, image_path) != 0)
		assert(0);

	ps_atlas.width = image.width;
	ps_atlas.height = image.height;

	image.data[0] = 255;
	image.data[1] = 255;
	image.data[2] = 255;

	ps_atlas.texture_id = texture_create(&image);
}

void ps_init(Vector2 display_size) {
	ps_atlas_init("./images/lain.bmp");

	glGenBuffers(1, &ps_vbo);
	glGenBuffers(1, &ps_ibo);

	ps_shader = shader_create("shaders/vertex_psyche.glsl", "shaders/fragment_psyche.glsl");
	ps_matrix_location = glGetUniformLocation(ps_shader, "matrix_transform");
	ps_texture_location = glGetUniformLocation(ps_shader, "tex");

	glGenVertexArrays(1, &ps_vao);

	m_bzero(&ps_ctx, sizeof(ps_ctx));

	ps_ctx.display_size = display_size;

	// Initializing draw lists with one draw list
	ps_ctx.draw_lists = calloc(32, sizeof(PsDrawList*));
	ps_ctx.draw_lists[ps_ctx.draw_lists_count++] = calloc(1, sizeof(PsDrawList));

	ps_draw_list_init(ps_ctx.draw_lists[0]);

	m_bzero(&ps_current_path, sizeof(ps_current_path));
	DYNAMIC_ARRAY_CREATE(&ps_current_path.points, Vector2);
	ps_current_path.thickness = 5.f;
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

	Vector2 white_pixel = {
		.x = 0.f,
		.y = 0.f
	};

	printf("Pixel %.2f %.2f\n", white_pixel.x, white_pixel.y);

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
			new_verts[i].uv_coords = white_pixel;
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
