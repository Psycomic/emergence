#ifndef __BATCH_RENDERER_H_
#define __BATCH_RENDERER_H_

#include <GL/glew.h>

#include "linear_algebra.h"
#include "misc.h"
#include "drawable.h"

struct _BatchDrawable;

// Batch of multiple objects
typedef struct {
	Material* material;
	uint32_t* elements;
	uint64_t* vertex_attributes_sizes;
	struct _BatchDrawable* last_added_drawable;

	uint64_t vertex_attributes_count;
	uint64_t vertex_size;
	uint64_t vertex_buffer_capacity;
	uint64_t index_buffer_capacity;
	uint64_t vertex_buffer_size;
	uint64_t index_buffer_size;

	GLuint vao;
	GLuint vertex_buffer;
	GLuint index_buffer;

	GLuint draw_type;
} Batch;

// Element of a Batch, has vertices, elements
// and information needed to modify its state
typedef struct _BatchDrawable {
	void* vertices;
	Batch* batch;

	struct _BatchDrawable* next;
	struct _BatchDrawable* previous;

	uint64_t vertex_size;

	uint64_t vertices_count;
	uint64_t elements_count;

	uint64_t index_buffer_offset;
	uint64_t vertex_buffer_offset;
} BatchDrawable;

void batch_init(Batch* batch, GLuint draw_type, Material* material, size_t vertex_buffer_capacity,
				size_t index_buffer_capacity, uint64_t* vertex_attributes_sizes, uint64_t vertex_attributes_count);
BatchDrawable* batch_drawable_create(Batch* batch, void* vertices, uint64_t vertices_count,
									 uint32_t* elements, uint64_t elements_count);
void batch_draw(Batch* batch, StateContext* gl, float* view_matrix);
void batch_draw_drawable(BatchDrawable* batch_drawable, StateContext* gl, float* view_matrix);
void batch_drawable_update(BatchDrawable* batch_drawable);
void batch_drawable_destroy(BatchDrawable* batch_drawable);

#endif // __BATCH_RENDERER_H_
