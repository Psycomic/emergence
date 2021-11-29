#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>

#include "misc.h"

#ifdef _WIN32
void usleep(clock_t time) {
	Sleep(time);
}
#endif

FILE* m_fopen(const char* filename, const char* mode) {
#ifdef _WIN32
	wchar_t* w_filename = u_utf8_to_utf16(filename);
	wchar_t* w_mode = u_utf8_to_utf16(mode);

	FILE* file = _wfopen(w_filename, w_mode);
	free(w_filename);
	free(w_mode);

	return file;
#else
	return fopen(filename, mode);
#endif
}

char* read_file(const char* filename) {
	FILE* file = m_fopen(filename, "r");

	fseek(file, 0L, SEEK_END);
	size_t sz = ftell(file);
	fseek(file, 0L, SEEK_SET);

	char* buffer = malloc(sz + 1);

	if (file == NULL) {
		fprintf(stderr, "Could not open file %s!\n", filename);
		return NULL;
	}
	else
		printf("Successfully opened file %s!\n", filename);

	fread(buffer, 1, sz, file);
	buffer[sz] = '\0';

	fclose(file);

	return buffer;
}

float clampf(float x, float min, float max) {
	if (x < min)
		return min;
	else if (x > max)
		return max;
	else
		return x;
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

void* dynamic_array_push_back(DynamicArray* arr, size_t count) {
	if (arr->size + count >= arr->capacity) {
		arr->capacity = arr->capacity * 2 + count;

		arr->data = realloc(arr->data, arr->capacity * arr->element_size);
	}

	void* return_data = ((uchar*)arr->data) + arr->size * arr->element_size;
	arr->size += count;

	return return_data;
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
}

void dynamic_array_destroy(DynamicArray* arr) {
	free(arr->data);

	arr->size = 0;
	arr->capacity = 0;
	arr->data = NULL;
}

uint64_t hash_string(uchar *str, uint size) {
    uint64_t hash = 5381;

    for (uint i = 0; i < size; i++)
        hash = ((hash << 5) + hash) + str[i];

    return hash;
}

HashTable* hash_table_create(uint size, uint (*hash)(void*, uint)) {
	assert(size >= 1);

	HashTable* hash_table = malloc(sizeof(HashTable));

	if (hash_table == NULL)
		return NULL;

	hash_table->size = size;
	hash_table->hash_function = hash;

	if ((hash_table->entries = malloc(sizeof(HashTableEntry*) * size)) == NULL)
		return NULL;

	for (uint i = 0; i < size; i++)
		hash_table->entries[i] = NULL;

	return hash_table;
}

int hash_table_set(HashTable* table, void* key, uint key_size, void* value, uint value_size) {
	uint key_hash = table->hash_function(key, key_size);
	uint index = key_hash % table->size;

	if (table->entries[index] == NULL) {	// If no collisions
		if ((table->entries[index] = malloc(sizeof(HashTableEntry) + value_size)) == NULL)
			return -1;

		HashTableEntry* entry = table->entries[index];

		entry->key_hash = key_hash;
		entry->next_entry = NULL;

		if (memcpy(entry->data, value, value_size) != 0)
			return -1;
	}
	else {	// Collision
		HashTableEntry* entry = table->entries[index];

		for (; entry->next_entry != NULL; entry = entry->next_entry) {
			if (key_hash == entry->key_hash) {	// If already exists
				if (memcpy(entry->data, value, value_size) != 0)
					return -1;

				return 0;
			}
		}

		if (key_hash == entry->key_hash) {
			if (memcpy(entry->data, value, value_size) != 0)
				return -1;

			return 0;
		}

		if ((entry->next_entry = malloc(sizeof(HashTableEntry) + value_size)) == NULL)
			return -1;

		entry->next_entry->key_hash = key_hash;
		entry->next_entry->next_entry = NULL;

		if (memcpy(entry->next_entry->data, value, value_size))
			return -1;
	}

	return 0;
}

void* hash_table_get(HashTable* table, void* key, uint key_size) {
	uint key_hash = table->hash_function(key, key_size);
	uint index = key_hash % table->size;

	HashTableEntry* entry = table->entries[index];

	if (entry == NULL)
		return NULL;

	if (entry->next_entry == NULL || (key_hash == entry->key_hash))	// Sole entry in list
		return entry->data;
	else if (entry->next_entry == NULL)
		return NULL;

	for (; key_hash != entry->key_hash; entry = entry->next_entry);

	return entry->data;
}

void m_bzero(void* dst, size_t size) {
	char* dest = dst;

	for (size_t i = 0; i < size; i++)
		dest[i] = 0;
}

char* m_strdup(const char* str) {
	char *n = malloc(strlen(str) + 1);
	strcpy(n, str);

	return n;
}

char* m_strndup(const char* str, size_t count) {
	char *n = malloc(count + 1);

	size_t i;
	for (i = 0; i < count; i++)
		n[i] = str[i];

	n[i] = '\0';

	return n;
}

int m_scanf(const char* fmt, ...) {
	va_list args;
	char buffer[128];

	va_start(args, fmt);
	if (fgets(buffer, sizeof(buffer), stdin)) {
		if (vsscanf(buffer, fmt, args) == 1) {
			return 0;
		}
	}
	va_end(args);

	return -1;
}

char* m_snprintf_dup(const char* fmt, ...) {
	va_list args;

	va_start(args, fmt);
	size_t string_size = vsnprintf(NULL, 0, fmt, args);
	char* string = malloc(string_size);
	va_end(args);

	va_start(args, fmt);
	vsnprintf(string, string_size, fmt, args);
	va_end(args);

	return string;
}

int parse_number(const char* str, uint size, long* integer, double* floating) {
	int str_len = size;
	GLboolean is_floating = GL_FALSE;
	GLboolean is_negative = GL_FALSE;

	*integer = 0;

	if (str_len <= 0)
		return -1;

	long base = 1;
	if (*str == '-') {
		if (size == 1)
			return -1;

		base = -1;
		is_negative = GL_TRUE;
		str++;
		str_len--;
	}

	for (const char* c = str + str_len; c-- != str;) {
		if (!is_floating && *c == '.') {
			is_floating = GL_TRUE;
			*floating = ((double)*integer) / base;
			base = 1;
			continue;
		}

		if (*c < '0' || *c > '9')
			return -1;

		if (is_floating)
			*floating += (*c - '0') * base;
		else
			*integer += (*c - '0') * base;

		base *= 10;
	}

	if (is_floating) {
		if (is_negative)
			*floating = -(*floating);

		return 1;
	}

	return 0;
}

#define STACK_DEFAULT_CAPACITY 1024

void stack_init(Stack* stack) {
	stack->capacity = STACK_DEFAULT_CAPACITY;
	stack->data = malloc(sizeof(void*) * stack->capacity);
	stack->top = 0;
}

void stack_push(Stack* stack, void* value) {
	stack->data[stack->top++] = value;

	if (stack->top >= stack->capacity) {
		stack->capacity *= 2;
		stack->data = realloc(stack->data, sizeof(void*) * stack->capacity);
	}
}

void stack_pop(Stack* stack, uint64_t count) {
	stack->top -= count;
}

void strinsert(char* dest, const char* src, const char* substr, size_t index, size_t n) {
	m_bzero(dest, n);

	if (index >= n) {
		*dest = '\0';
		return;
	}

	size_t substr_length = strlen(substr);

	memcpy(dest, src, index);
	strncpy(dest + index, substr, n - index);

	if (substr_length > n - index) {
		dest[index + (substr_length - (n - index)) + 1] = '\0';
		return;
	}

	strncpy(dest + index + substr_length, src + index, n - index - substr_length);
}

size_t strcount(const char* str, char c) {
	size_t count = 0;
	while (*str++ != '\0') {
		if (*(str - 1) == c)
			count++;
	}

	return count;
}

int u_codepoint_to_string(char* dst, uint code) {
	m_bzero(dst, 5);

	if (code <= 0x7f) {
		dst[0] = (char)code;
		return 0;
	}
	else if (code <= 0x07FF) {
		dst[0] = (char) (((code >> 6) & 0x1F) | 0xC0);
		dst[1] = (char) (((code >> 0) & 0x3F) | 0x80);
		return 0;
	}
	else if (code <= 0xFFFF) {
		dst[0] = (char) (((code >> 12) & 0x0F) | 0xE0);
		dst[1] = (char) (((code >>  6) & 0x3F) | 0x80);
		dst[2] = (char) (((code >>  0) & 0x3F) | 0x80);
		return 0;
	}
	else if (code <= 0x10FFFF) {
		dst[0] = (char) (((code >> 18) & 0x07) | 0xF0);
		dst[1] = (char) (((code >> 12) & 0x3F) | 0x80);
		dst[2] = (char) (((code >>  6) & 0x3F) | 0x80);
		dst[3] = (char) (((code >>  0) & 0x3F) | 0x80);
		return 0;
	}
	else {
		return -1;
	}
}

uint u_string_to_codepoint(const uchar* string) {
	if (*string <= 0x7f) {
		return *string;
	} else if (*string >> 3 == 0x1e) {
		return ((string[0] & 0x7)  << 18 |
				(string[1] & 0x3f) << 12 |
				(string[2] & 0x3f) << 6  |
				(string[3] & 0x3f));
	} else if (*string >> 4 == 0xe) {
		return ((string[0] & 0xf)  << 12 |
				(string[1] & 0x3f) << 6  |
				(string[2] & 0x3f));
	} else if (*string >> 5 == 6) {
		return ((string[0] & 0x1f) << 6 |
				(string[1] & 0x3f));
	}

	return 0;
}

int powi(int base, int exp) {
    int result = 1;
    while (exp) {
        if (exp % 2)
           result *= base;
        exp /= 2;
        base *= base;
    }
    return result;
}

uint powu(uint base, uint exp) {
    uint result = 1;
    while (exp) {
        if (exp % 2)
           result *= base;
        exp /= 2;
        base *= base;
    }

    return result;
}

int modi(int a, int b) {
    int r = a % b;
    return r < 0 ? r + b : r;
}

void print_as_binary(uint64_t w) {
	printf("0b");
	for (int i = 63; i >= 0; i--) {
		if (w & (1UL << i))
			putchar('1');
		else
			putchar('0');
	}
}

inline uint32_t float_as_binary(float f) {
	return *(uint32_t*)&f;
}

inline float word_to_float(uint64_t w) {
	return ((float*)&w)[1];
}

#undef malloc
#undef free
#undef realloc

inline void* debug_malloc(size_t size, const char* file, const uint line) {
	void* return_value = malloc(size);
	printf("Malloc %p, 0x%lx bytes at %s:%d\n", return_value, size, file, line);

	return return_value;
}

inline void debug_free(void* ptr, const char* file, const uint line) {
	printf("Free %p at %s:%d\n", ptr, file, line);

	free(ptr);
}

inline void* debug_realloc(void* ptr, size_t size, const char* file, const uint line) {
	printf("Realloc %p at %s:%d\n", ptr, file, line);

	return realloc(ptr, size);
}
