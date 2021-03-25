#ifndef MISC_HEADER
#define MISC_HEADER

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif

#define ARRAY_SIZE(A) sizeof(A) / (sizeof *A)
#define DYNAMIC_ARRAY_CREATE(ARR, TYPE)	dynamic_array_create_fn(ARR, sizeof(TYPE))
#define DYNAMIC_ARRAY_AT(ARR, INDEX, TYPE) ((TYPE*)dynamic_array_at(ARR, INDEX))
#define CONS(A, B) cons(&A, sizeof(A), B)

#define SUPER(INSTANCE) &INSTANCE->header

#define max(a, b) (a) > (b) ? (a) : (b)
#define min(a, b) (a) < (b) ? (a) : (b)

#ifdef _DEBUG

#define malloc(size) debug_malloc(size, __FILE__, __LINE__)
#define free(ptr) debug_free(ptr, __FILE__, __LINE__)

#else

#include <stdlib.h>

#endif // _DEBUG

#include <stddef.h>
#include <GL/glew.h>

#include "linear_algebra.h"

typedef unsigned int uint;
typedef unsigned char uchar;

typedef struct {
	void* data;
	size_t size;
	size_t capacity;
	uint element_size;
} DynamicArray;

typedef struct HashTableEntry {
	char* key;

	struct HashTableEntry* next_entry;

	char data[];
} HashTableEntry;

typedef struct {
	HashTableEntry** entries;
	uint size;
} HashTable;

char* read_file(const char* filename);

float random_float(void);
void random_arrayf(float* destination, uint size);
float gaussian_random(void);
float clampf(float x, float min, float max);

Vector3 rgb_to_vec(uchar r, uchar g, uchar b);

void memory_multiple_copy_f(float* src, float* dst, uint repeat, uint size);

void dynamic_array_create_fn(DynamicArray* arr, size_t element_size);
void* dynamic_array_push_back(DynamicArray* arr);
void* dynamic_array_at(DynamicArray* arr, uint index);
void* dynamic_array_last(DynamicArray* arr);
int dynamic_array_pop(DynamicArray* arr, uint count);
void dynamic_array_swap(DynamicArray* arr, uint src, uint dst);
void dynamic_array_remove(DynamicArray* arr, uint id);
void dynamic_array_clear(DynamicArray* arr);
void dynamic_array_destroy(DynamicArray* arr);

void* debug_malloc(size_t size, const char* file, const uint line);
void debug_free(void* ptr, const char* file, const uint line);

HashTable* hash_table_create(uint size);
int hash_table_set(HashTable* table, char* key, void* value, uint value_size);
void* hash_table_get(HashTable* table, char* key);

#endif
