#include <stdio.h>

#define STACK_ALLOCATOR_DATA_SIZE 4096

char stack_allocator_data[STACK_ALLOCATOR_DATA_SIZE];
char* stack_allocator_ptr = stack_allocator_data;

void* stack_allocator_malloc(size_t size) {
	printf("Allocating 0x%llx bytes of memory !\n", size);
	printf("Memory left : 0x%llx !\n", STACK_ALLOCATOR_DATA_SIZE - (stack_allocator_ptr - stack_allocator_data));

	if ((stack_allocator_ptr + size) - stack_allocator_data >= STACK_ALLOCATOR_DATA_SIZE) {
		fprintf(stderr, "Ran out of memory !\n");
		return NULL;
	}

	void* data = stack_allocator_ptr;
	stack_allocator_ptr += size;

	return data;
}

void stack_allocator_free(void* block) {
	stack_allocator_ptr = block;
}