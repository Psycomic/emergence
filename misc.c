#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "misc.h"

int read_file(char* buffer, const char* filename) {
	FILE* file = fopen(filename, "r");

	if (file == NULL) {
		fprintf(stderr, "Could not open file %s!\n", filename);
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

float gaussian_random() {
	float sum = 0.f;

	for (uint i = 0; i < 12; i++)
		sum += random_float();

	return sum - 6.f;
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

Vector3 rgb_to_vec(uchar r, uchar g, uchar b) {
	Vector3 result;

	result.x = (float)r / 255.f;
	result.y = (float)g / 255.f;
	result.z = (float)b / 255.f;

	return result;
}

#define DYNAMIC_ARRAY_DEFAULT_CAPACITY ((size_t)16)

void dynamic_array_create_fn(DynamicArray* arr, size_t element_size) {
	arr->size = 0;
	arr->element_size = element_size;
	arr->capacity = DYNAMIC_ARRAY_DEFAULT_CAPACITY;
	arr->data = malloc(arr->capacity * arr->element_size);
}

void* dynamic_array_push_back(DynamicArray* arr) {
	if (arr->size == arr->capacity) {
		arr->capacity *= 2;

		arr->data = realloc(arr->data, arr->capacity * arr->element_size);
	}

	return ((uchar*)arr->data) + arr->size++ * arr->element_size;
}

void* dynamic_array_at(DynamicArray* arr, uint index) {
	assert(index < arr->size);

	return (uchar*)arr->data + (size_t)index * arr->element_size;
}

void* dynamic_array_last(DynamicArray* arr) {
	return (uchar*)arr->data + (arr->size - 1) * arr->element_size;
}

int dynamic_array_pop(DynamicArray* arr, uint count) {
	if (count > arr->size)
		return -1;

	arr->size -= count;

	return 0;
}

void dynamic_array_swap(DynamicArray* arr, uint src, uint dst) {
	assert(src < arr->size && dst < arr->size);

	memcpy((uchar*)arr->data + (size_t)dst * arr->element_size,
		   (uchar*)arr->data + (size_t)src * arr->element_size,
		   arr->element_size);
}

void dynamic_array_remove(DynamicArray* arr, uint id) {
	dynamic_array_swap(arr, arr->size - 1, id);
	arr->size--;
}

void dynamic_array_clear(DynamicArray* arr) {
	arr->size = 0;
	arr->capacity = DYNAMIC_ARRAY_DEFAULT_CAPACITY;

	arr->data = realloc(arr->data, arr->element_size * arr->capacity);
}

void dynamic_array_destroy(DynamicArray* arr) {
	free(arr->data);

	arr->size = 0;
	arr->capacity = 0;
	arr->data = NULL;
}

uint str_hash(char* str, uint maxval) {
	uint hash = 12;

	do {
		hash = hash << 8;
		hash += *str;
	} while (*(++str) != '\0');

	return hash % maxval;
}

HashTable* hash_table_create(uint size) {
	assert(size >= 1);

	HashTable* hash_table = malloc(sizeof(HashTable));

	if (hash_table == NULL)
		return NULL;

	hash_table->size = size;

	if ((hash_table->entries = malloc(sizeof(HashTableEntry*) * size)) == NULL)
		return NULL;

	for (uint i = 0; i < size; i++)
		hash_table->entries[i] = NULL;

	return hash_table;
}

int hash_table_set(HashTable* table, char* key, void* value, uint value_size) {
	uint index = str_hash(key, table->size);

	if (table->entries[index] == NULL) {	// If no collisions
		if ((table->entries[index] = malloc(sizeof(HashTableEntry) + value_size)) == NULL)
			return -1;

		HashTableEntry* entry = table->entries[index];

		entry->key = key;
		entry->next_entry = NULL;

		if (memcpy(entry->data, value, value_size) != 0)
			return -1;
	}
	else {	// Collision
		HashTableEntry* entry = table->entries[index];

		for (; entry->next_entry != NULL; entry = entry->next_entry) {
			if (strcmp(entry->key, key) == 0) {	// If already exists
				if (memcpy(entry->data, value, value_size) != 0)
					return -1;

				return 0;
			}
		}

		if (strcmp(entry->key, key) == 0) {
			if (memcpy(entry->data, value, value_size) != 0)
				return -1;

			return 0;
		}

		if ((entry->next_entry = malloc(sizeof(HashTableEntry) + value_size)) == NULL)
			return -1;

		entry->next_entry->key = key;
		entry->next_entry->next_entry = NULL;

		if (memcpy(entry->next_entry->data, value, value_size))
			return -1;
	}

	return 0;
}

void* hash_table_get(HashTable* table, char* key) {
	uint index = str_hash(key, table->size);

	HashTableEntry* entry = table->entries[index];

	if (entry == NULL)
		return NULL;

	if (entry->next_entry == NULL && strcmp(key, entry->key) == 0)	// Sole entry in list
		return entry->data;
	else if (entry->next_entry == NULL)
		return NULL;

	for (; strcmp(entry->key, key) != 0; entry = entry->next_entry);

	return entry->data;
}

#undef malloc
#undef free

void* debug_malloc(size_t size, const char* file, const uint line) {
	void* return_value = malloc(size);

	printf("Malloc %p at %s:%d\n", return_value, file, line);

	return return_value;
}

void debug_free(void* ptr, const char* file, const uint line) {
	printf("Free %p at %s:%d\n", ptr, file, line);

	free(ptr);
}
