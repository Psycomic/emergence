#ifndef EFORTH_HEADER
#define EFORTH_HEADER

#include "misc.h"

typedef struct {
	enum {
		FORTH_TYPE_WORD,
		FORTH_TYPE_INT
	} type;

	union {
		char* word;
		int integer;
	} data;
} EForthObject;

typedef enum {
	EFORTH_NO_ERROR,
	EFORTH_STACK_UNDERFLOW,
	EFORTH_UNKNOWN_WORD,
	EFORTH_NO_CLOSING_DELIMITER,
	EFORTH_PANIC
} EForthException;

void eforth_initialize(void);

void eforth_object_print(EForthObject* obj);

EForthException eforth_parse(char* stream, DynamicArray* words);
EForthException eforth_execute(DynamicArray* program, DynamicArray* stack);

EForthException eforth_eval(char* stream, DynamicArray* stack);

#endif // !FORTH_HEADER