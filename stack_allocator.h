#ifndef STACK_ALLOCATOR_HEADER
#define STACK_ALLOCATOR_HEADER

void* stack_allocator_malloc(size_t size);
void stack_allocator_free(void* block);

#endif