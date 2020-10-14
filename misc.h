#ifndef MISC_HEADER
#define MISC_HEADER

#define array_size(A) sizeof(A) / (sizeof *A)
#define M_PI 3.1415926535897932384626433832795

#include <GL/glew.h>

typedef unsigned int uint;

typedef struct {
	void* data;
	uint size, capacity;
	size_t element_size;
} DynamicArray;

int read_file(char* buffer, const char* filename);

float random_float(void);
void random_arrayf(float* destination, uint size);

void memory_multiple_copy_f(float* src, float* dst, uint repeat, uint size);

void dynamic_array_create_fn(DynamicArray* arr, size_t element_size);

#define dynamic_array_create(ARR, TYPE) dynamic_array_create_fn(ARR, sizeof(TYPE))

#endif
