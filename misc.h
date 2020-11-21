#ifndef MISC_HEADER
#define MISC_HEADER

#define M_PI 3.1415926535897932384626433832795

#define ARRAY_SIZE(A)					sizeof(A) / (sizeof *A)
#define DYNAMIC_ARRAY_CREATE(ARR, TYPE)	dynamic_array_create_fn(ARR, sizeof(TYPE))
#define CONS(A, B)						cons(&A, sizeof(A), B)

#include <GL/glew.h>

#include "linear_algebra.h"

typedef unsigned int uint;
typedef unsigned char uchar;

typedef struct {
	void* data;
	uint size, capacity;
	size_t element_size;
} DynamicArray;

typedef struct List {
	struct List* next;
	char data[];
} List;

int read_file(char* buffer, const char* filename);

float random_float(void);
void random_arrayf(float* destination, uint size);

Vector3 rgb_to_vec(uchar r, uchar g, uchar b);

void memory_multiple_copy_f(float* src, float* dst, uint repeat, uint size);

void dynamic_array_create_fn(DynamicArray* arr, size_t element_size);
void dynamic_array_push_back(DynamicArray* arr, void* element);
void dynamic_array_at(DynamicArray* arr, uint index, void* buffer);

List* cons(void* data, size_t data_size, List* next);

void* list_first(List* list);
void* list_rest(List* list);
void* list_nth(List* list, uint index);
void* list_destroy(List* list);

void* list_map(List* list, void (*function)(void*));

#endif
