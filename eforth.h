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

void eforth_object_print(EForthObject* obj);
void eforth_parse(char* stream, DynamicArray* words);

#endif // !FORTH_HEADER