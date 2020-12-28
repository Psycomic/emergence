#include "batch_renderer.h"

#include <stdio.h>

void batch_init(Batch* batch, Material* material, size_t vertex_buffer_capacity, size_t index_buffer_capacity)  {
	batch->material = material;

	batch->vertex_buffer_capacity = vertex_buffer_capacity;
	batch->index_buffer_capacity = index_buffer_capacity;

	batch->vertex_buffer_size = 0;
	batch->index_buffer_size = 0;

	DYNAMIC_ARRAY_CREATE(&batch->batch_drawables, BatchDrawable*);

	glGenVertexArrays(1, &batch->vao);

	// Allocating the buffers
	glGenBuffers(1, &batch->vertex_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, batch->vertex_buffer);
	glBufferData(GL_ARRAY_BUFFER, batch->vertex_buffer_capacity, NULL, GL_STREAM_DRAW);

	glGenBuffers(1, &batch->index_buffer);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, batch->index_buffer);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, batch->index_buffer_capacity * sizeof(uint32_t), NULL, GL_STREAM_DRAW);
}

void batch_drawable_init(
	Batch* batch, BatchDrawable* batch_drawable, Vector3* position,
	void* vertices, uint64_t vertices_count, uint64_t* vertex_attributes_sizes,
	uint64_t vertex_attributes_count, uint32_t* elements, uint64_t elements_count)
{
	batch_drawable->vertices = vertices;
	batch_drawable->position = position;

	batch_drawable->vertex_attributes_sizes = vertex_attributes_sizes;
	batch_drawable->vertex_attributes_count = vertex_attributes_count;

	batch_drawable->vertices = vertices;
	batch_drawable->vertices_count = vertices_count;

	batch_drawable->elements = elements;
	batch_drawable->elements_count = elements_count;

	batch_drawable->index_buffer_offset = batch->index_buffer_size;
	batch_drawable->vertex_buffer_offset = batch->vertex_buffer_size;

	uint64_t vertex_size = 0, vertices_size;
	uint64_t elements_offset = batch_drawable->vertex_buffer_offset / sizeof(float);

	for (uint64_t i = 0; i < vertex_attributes_count; i++)
		vertex_size += vertex_attributes_sizes[i];

	for (uint64_t i = 0; i < elements_count; i++)
		elements[i] += elements_offset;

	vertices_size = vertex_size * vertices_count;

	batch->index_buffer_size += elements_count * sizeof(uint32_t);
	batch->vertex_buffer_size += vertices_size * sizeof(float);

	glBindVertexArray(batch->vao);

	// Initializing the array buffer
	glBindBuffer(GL_ARRAY_BUFFER, batch->vertex_buffer);
	glBufferSubData(GL_ARRAY_BUFFER, batch_drawable->vertex_buffer_offset, vertices_size * sizeof(float), vertices);

	for (uint i = 0; i < vertices_count; i++) {
		glEnableVertexAttribArray(i);

		glVertexAttribPointer(i, vertex_attributes_sizes[i], GL_FLOAT, GL_FALSE,
							  vertex_size * sizeof(float), (void*)batch_drawable->vertex_buffer_offset);
	}

	// Initializing the index buffer
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, batch->index_buffer);
	glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, batch_drawable->index_buffer_offset, elements_count * sizeof(uint32_t), elements);

	glBindVertexArray(0);

	*((BatchDrawable**)dynamic_array_push_back(&batch->batch_drawables)) = batch_drawable;
}

void batch_draw(Batch* batch) {
	material_use(batch->material, NULL, NULL);

	glBindVertexArray(batch->vao);

	glDrawElements(GL_TRIANGLES, batch->index_buffer_size / sizeof(uint32_t), GL_UNSIGNED_INT, NULL);
}
