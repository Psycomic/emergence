#include "batch_renderer.h"

#include <stdio.h>
#include <assert.h>
#include <string.h>

void batch_init(Batch* batch, GLuint draw_type, Material* material, size_t vertex_buffer_capacity,
				size_t index_buffer_capacity, uint64_t* vertex_attributes_sizes, uint64_t vertex_attributes_count)
{
	batch->last_added_drawable = NULL;

	batch->material = material;

	batch->vertex_buffer_capacity = vertex_buffer_capacity;
	batch->index_buffer_capacity = index_buffer_capacity;
	batch->draw_type = draw_type;

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

	batch->elements = malloc(batch->index_buffer_capacity);

	glBindVertexArray(0);

	get_opengl_errors();
}

BatchDrawable* batch_drawable_create(Batch* batch, void* vertices, uint64_t vertices_count,
									 uint32_t* elements, uint64_t elements_count)
{
	assert(batch->vertex_buffer_size + vertices_count * batch->vertex_size * sizeof(float) <  batch->vertex_buffer_capacity);
	assert(batch->index_buffer_size + elements_count * sizeof(uint) < batch->index_buffer_capacity);

	BatchDrawable* batch_drawable = malloc(sizeof(BatchDrawable));

	batch_drawable->next = NULL;

	if (batch->last_added_drawable != NULL)
		batch->last_added_drawable->next = batch_drawable;

	batch->last_added_drawable = batch_drawable;

	batch_drawable->batch = batch;
	batch_drawable->vertices = vertices;

	batch_drawable->vertices = vertices;
	batch_drawable->vertices_count = vertices_count;

	batch_drawable->elements_count = elements_count;

	batch_drawable->index_buffer_offset = batch->index_buffer_size;
	batch_drawable->vertex_buffer_offset = batch->vertex_buffer_size;

	uint32_t* new_elements = (uint32_t*)(((char*)batch->elements) + batch_drawable->index_buffer_offset);
	memcpy(new_elements, elements, sizeof(uint32_t) * elements_count);

	uint64_t elements_offset = batch_drawable->vertex_buffer_offset / (sizeof(float) * batch->vertex_size);

	for (uint64_t i = 0; i < elements_count; i++)
		new_elements[i] += elements_offset;

	uint64_t vertices_size = batch->vertex_size * vertices_count;

	batch->index_buffer_size += elements_count * sizeof(uint32_t);
	batch->vertex_buffer_size += vertices_size * sizeof(float);

	batch_drawable->vertex_size = batch->vertex_size;

	// Initializing the array buffer
	glBindBuffer(GL_ARRAY_BUFFER, batch->vertex_buffer);
	glBufferSubData(GL_ARRAY_BUFFER, batch_drawable->vertex_buffer_offset, vertices_size * sizeof(float), vertices);

	// Initializing the index buffer
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, batch->index_buffer);
	glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, batch_drawable->index_buffer_offset, elements_count * sizeof(uint32_t), new_elements);

	get_opengl_errors();

	return batch_drawable;
}

// Delete the drawable from the batch, freeing GPU memory.
void batch_drawable_destroy(BatchDrawable* batch_drawable) {
	// void *glMapBufferRange(GLenum target​, GLintptr offset​, GLsizeiptr length​, GLbitfield access​);
	Batch* batch = batch_drawable->batch;

	uint64_t elements_mapped_size = batch->index_buffer_size - batch_drawable->index_buffer_offset,
		elements_size = batch_drawable->elements_count * sizeof(uint32_t);

	if (elements_mapped_size > elements_size) {
		// Displacing element buffer
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, batch->index_buffer);

		uint32_t* batch_elements = glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER,
													batch_drawable->index_buffer_offset,
													elements_mapped_size,
													GL_MAP_READ_BIT | GL_MAP_WRITE_BIT);
		get_opengl_errors();

		for (uint64_t i = 0; i < (elements_mapped_size - elements_size) / sizeof(uint32_t); i++) {
			batch_elements[i] = batch_elements[i + batch_drawable->elements_count];
			assert(i < elements_mapped_size / sizeof(uint32_t));
		}

		glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);
	}

	batch->index_buffer_size -= elements_size;

	for (BatchDrawable* drawable = batch_drawable->next; drawable != NULL; drawable = drawable->next)
		drawable->index_buffer_offset -= elements_size;

}

void batch_drawable_update(BatchDrawable* batch_drawable) {
	glBindBuffer(GL_ARRAY_BUFFER, batch_drawable->batch->vertex_buffer);
	glBufferSubData(GL_ARRAY_BUFFER, batch_drawable->vertex_buffer_offset,
					batch_drawable->vertex_size * batch_drawable->vertices_count * sizeof(float),
					batch_drawable->vertices);

	get_opengl_errors();
}

void batch_draw(Batch* batch, StateContext* gl, float* view_matrix) {
	size_t elements_size = batch->index_buffer_size / sizeof(uint32_t);

	material_use(batch->material, gl, NULL, view_matrix);

	glBindVertexArray(batch->vao);
	glDrawElements(batch->draw_type, elements_size, GL_UNSIGNED_INT, NULL);
	glBindVertexArray(0);

	get_opengl_errors();
}
