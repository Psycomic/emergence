#ifndef MISC_HEADER
#define MISC_HEADER

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif

#define ARRAY_SIZE(A) sizeof(A) / (sizeof *A)
#define DYNAMIC_ARRAY_CREATE(ARR, TYPE)	dynamic_array_create_fn(ARR, sizeof(TYPE))
#define DYNAMIC_ARRAY_AT(ARR, INDEX, TYPE) ((TYPE*)dynamic_array_at(ARR, INDEX))
#define CONS(A, B) cons(&A, sizeof(A), B)

#define SUPER(INSTANCE) (&(INSTANCE)->header)

#ifndef _WIN32
#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

#define ASSERT_CONCAT_(a, b) a##b
#define ASSERT_CONCAT(a, b) ASSERT_CONCAT_(a, b)
#define ct_assert(e) enum { ASSERT_CONCAT(assert_line_, __LINE__) = 1/(!!(e)) }

#ifdef _DEBUG

#define malloc(size) debug_malloc(size, __FILE__, __LINE__)
#define free(ptr) debug_free(ptr, __FILE__, __LINE__)

extern void exit(int status);

#else

#include <stdlib.h>

#endif // _DEBUG

#ifdef _WIN32
#include <windows.h>

typedef DWORD clock_t;

void usleep(clock_t time);

#endif // _WIN32
#ifdef __linux__

extern int usleep (unsigned int __useconds);

#endif // __linux__

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

typedef struct {
	void** data;
	uint64_t capacity;
	uint64_t top;
} Stack;

char* read_file(const char* filename);

float clampf(float x, float min, float max);

Vector3 rgb_to_vec(uchar r, uchar g, uchar b);

void memory_multiple_copy_f(float* src, float* dst, uint repeat, uint size);

void dynamic_array_create_fn(DynamicArray* arr, size_t element_size);
void* dynamic_array_push_back(DynamicArray* arr, size_t count);
void* dynamic_array_at(DynamicArray* arr, uint index);
void* dynamic_array_last(DynamicArray* arr);
int dynamic_array_pop(DynamicArray* arr, uint count);
void dynamic_array_swap(DynamicArray* arr, uint src, uint dst);
void dynamic_array_remove(DynamicArray* arr, uint id);
void dynamic_array_clear(DynamicArray* arr);
void dynamic_array_destroy(DynamicArray* arr);

char* m_strdup(const char* str);
char* m_strndup(const char* str, size_t count);
int m_scanf(const char* fmt, ...);
void strinsert(char* dest, const char* src, const char* substr, size_t index, size_t n);
size_t strcount(const char* str, char c);

// Returns 0 if integer, 1 if float, and -1 if error
int parse_number(char* str, long* integer, double* floating);

void m_bzero(void* dst, size_t size);

uint hash(uchar *str);

void* debug_malloc(size_t size, const char* file, const uint line);
void debug_free(void* ptr, const char* file, const uint line);

HashTable* hash_table_create(uint size);
int hash_table_set(HashTable* table, char* key, void* value, uint value_size);
void* hash_table_get(HashTable* table, char* key);

void stack_init(Stack* stack);
void stack_push(Stack* stack, void* value);
void stack_pop(Stack* stack, uint64_t count);

int powi(int base, int exp);
uint powu(uint base, uint exp);
int modi(int a, int b);

#endif
