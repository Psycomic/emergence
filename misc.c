#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "misc.h"

int read_file(char* buffer, const char* filename) {
	FILE* file = fopen(filename, "r");

	if (file == NULL) {
		fprintf(stderr, "Could not open file !\n");
		return -1;
	}
	else {
		printf("Successfully opened file %s!\n", filename);
	}

	while ((*buffer = fgetc(file)) != EOF) {
		buffer++;
	}

	*buffer = '\0';

	fclose(file);
	return 0;
}

void random_arrayf(float* destination, uint size) {
	for (uint i = 0; i < size; ++i) {
		destination[i] = random_float();
	}
}

void memory_multiple_copy_f(float* src, float* dst, uint repeat, uint size) {
	for (uint i = 0; i < size * repeat; i += repeat)
		for (uint j = 0; j < repeat; j++)
			dst[i + j] = src[i / repeat];
}

#define DYNAMIC_ARRAY_DEFAULT_CAPACITY 10

void dynamic_array_create_fn(DynamicArray* arr, size_t element_size) {
	arr->size = 0;
	arr->element_size = element_size;
	arr->capacity = DYNAMIC_ARRAY_DEFAULT_CAPACITY * arr->element_size;
	arr->data = malloc(arr->capacity);
}

void dynamic_array_push_back(DynamicArray* arr, void* element) {
	if ((++arr->size) * arr->element_size > arr->capacity) {
		arr->data = realloc(arr->data, arr->capacity + DYNAMIC_ARRAY_DEFAULT_CAPACITY * arr->element_size);
		assert(arr->data != NULL);
	}
	
	memcpy((char*)arr->data + (arr->size - 1), element, arr->element_size);
}

void dynamic_array_at(DynamicArray* arr, uint index, void* buffer) {
	memcpy(buffer, (char*)arr->data + index * arr->element_size, arr->element_size);
}

List* cons(void* data, size_t data_size, List* next) {
	List* list = malloc(sizeof(List) + data_size);
	assert(list != NULL);

	memcpy(list->data, data, data_size);
	
	list->next = next;

	return list;
}

void* list_first(List* list) {
	return list->data;
}

void* list_rest(List* list) {
	return list->next;
}

void* list_nth(List* list, uint index) {
	uint i = 0;

	while (i++ != index) {
		list = list->next;

		if (list == NULL) {
			return NULL;
		}
	}

	return list->data;
}

void* list_destroy(List* list) {
	while (list != NULL) {
		List* next = list->next;

		free(list);

		list = next;
	}
}

void* list_map(List* list, void (*function)(void*)) {
	for (; list != NULL; list = list->next) {
		function(list->data);
	}
}

Vector3 rgb_to_vec(uchar r, uchar g, uchar b) {
	Vector3 result;

	result.x = (float)r / 255.f;
	result.y = (float)g / 255.f;
	result.z = (float)b / 255.f;

	return result;
}