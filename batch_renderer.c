#include "batch_renderer.h"

#include <stdio.h>

void batch_init(Batch* batch, Material* material, size_t vertex_buffer_capacity, size_t index_buffer_capacity,
				uint64_t* vertex_attributes_sizes, uint64_t vertex_attributes_count)
{
	batch->material = material;

	batch->vertex_buffer_capacity = vertex_buffer_capacity;
	batch->index_buffer_capacity = index_buffer_capacity;

	batch->vertex_attributes_sizes = vertex_attributes_sizes;
	batch->vertex_attributes_count = vertex_attributes_count;

	batch->vertex_buffer_size = 0;
	batch->index_buffer_size = 0;

	uint64_t vertex_size = 0;

	for (uint64_t i = 0; i < vertex_attributes_count; i++)
		vertex_size += vertex_attributes_sizes[i];

	batch->vertex_size = vertex_size;

	glGenVertexArrays(1, &batch->vao);

	glBindVertexArray(batch->vao);

	uint64_t offset = 0;

	// Allocating the buffers
	glGenBuffers(1, &batch->vertex_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, batch->vertex_buffer);
	glBufferData(GL_ARRAY_BUFFER, batch->vertex_buffer_capacity, NULL, GL_STREAM_DRAW);

	for (uint i = 0; i < batch->vertex_attributes_count; i++) {
		glEnableVertexAttribArray(i);
		glVertexAttribPointer(i, batch->vertex_attributes_sizes[i], GL_FLOAT, GL_FALSE,
							  batch->vertex_size * sizeof(float), (void*)offset);

		offset += batch->vertex_attributes_sizes[i] * sizeof(float);
	}

	glGenBuffers(1, &batch->index_buffer);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, batch->index_buffer);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, batch->index_buffer_capacity, NULL, GL_STREAM_DRAW);

	glBindVertexArray(0);
}

void batch_drawable_init(
	Batch* batch, BatchDrawable* batch_drawable, Vector3* position,
	void* vertices, uint64_t vertices_count, uint32_t* elements, uint64_t elements_count)
{
	batch_drawable->vertices = vertices;
	batch_drawable->position = position;

	batch_drawable->vertices = vertices;
	batch_drawable->vertices_count = vertices_count;

	batch_drawable->elements = elements;
	batch_drawable->elements_count = elements_count;

	batch_drawable->index_buffer_offset = batch->index_buffer_size;
	batch_drawable->vertex_buffer_offset = batch->vertex_buffer_size;

	uint64_t elements_offset = batch_drawable->vertex_buffer_offset / (sizeof(float) * batch->vertex_size);

	for (uint64_t i = 0; i < elements_count; i++)
		elements[i] += elements_offset;

	uint64_t vertices_size = batch->vertex_size * vertices_count;

	batch->index_buffer_size += elements_count * sizeof(uint32_t);
	batch->vertex_buffer_size += vertices_size * sizeof(float);

	// Initializing the array buffer
	glBindBuffer(GL_ARRAY_BUFFER, batch->vertex_buffer);
	glBufferSubData(GL_ARRAY_BUFFER, batch_drawable->vertex_buffer_offset, vertices_size * sizeof(float), vertices);

	// Initializing the index buffer
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, batch->index_buffer);
	glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, batch_drawable->index_buffer_offset, elements_count * sizeof(uint32_t), elements);
}

void batch_draw(Batch* batch) {
	size_t elements_size = batch->index_buffer_size / sizeof(uint32_t);

	material_use(batch->material, NULL, NULL);

	glBindVertexArray(batch->vao);
	glDrawElements(GL_TRIANGLES, elements_size, GL_UNSIGNED_INT, NULL);
	glBindVertexArray(0);
}
