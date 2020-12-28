#ifndef __BATCH_RENDERER_H_
#define __BATCH_RENDERER_H_

#include <GL/glew.h>

#include "linear_algebra.h"
#include "misc.h"
#include "drawable.h"

typedef struct {
	void* vertices;
	uint32_t* elements;

	Vector3* position;

	uint64_t* vertex_attributes_sizes;
	uint64_t vertex_attributes_count;

	uint64_t vertices_count;
	uint64_t elements_count;

	uint64_t index_buffer_offset;
	uint64_t vertex_buffer_offset;
} BatchDrawable;

typedef struct {
	Material* material;

	GLuint vao;

	GLuint vertex_buffer;
	GLuint index_buffer;

	size_t vertex_buffer_capacity;
	size_t index_buffer_capacity;

	size_t vertex_buffer_size;
	size_t index_buffer_size;
} Batch;

void batch_init(Batch* batch, Material* material, size_t vertex_buffer_capacity, size_t index_buffer_capacity);
void batch_drawable_init(
	Batch* batch, BatchDrawable* batch_drawable, Vector3* position,
	void* vertices, uint64_t vertices_count, uint64_t* vertex_attributes_sizes,
	uint64_t vertex_attributes_count, uint32_t* elements, uint64_t elements_count);
void batch_draw(Batch* batch);

#endif // __BATCH_RENDERER_H_
