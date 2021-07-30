#include "yuki.h"

#include <stdio.h>

static char alloc_region[1024];
static char* alloc_ptr;

void yk_allocator_init() {
	alloc_ptr = alloc_region;

	if ((uint64_t)alloc_ptr % 8 != 0)
		alloc_ptr += 8 - (uint64_t)alloc_ptr % 8;
}

void* yk_alloc(size_t size) {
	if (size % 8 != 0)
		size += 8 - size % 8;

	printf("Allocating %ld bytes!\n", size);

	void* mem = alloc_ptr;
	alloc_ptr += size;
	return mem;
}
