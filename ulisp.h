#ifndef __ULISP_H_
#define __ULISP_H_

#include "misc.h"

typedef struct {
	enum {
		LISP_LIST,
		LISP_SYMBOL
	} type;

	union {
		List* list;
		char* symbol;
	} data;
} LispObject;

LispObject* ulisp_read(const char* stream);

#endif // __ULISP_H_
