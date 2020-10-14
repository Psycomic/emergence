#include <stdlib.h>
#include <stdio.h>
#include <string.h>

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
	if ((++arr->size) * arr->element_size > arr->capacity)
		arr->data = realloc(arr->data, arr->capacity + DYNAMIC_ARRAY_DEFAULT_CAPACITY * arr->element_size);
	
	memcpy((char*)arr->data + (arr->size - 1), element, arr->element_size);
}

void dynamic_array_at(DynamicArray* arr, uint index, void* buffer) {
	memcpy(buffer, (char*)arr->data + index * arr->element_size, arr->element_size);
}