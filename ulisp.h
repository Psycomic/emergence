#ifndef __ULISP_H_
#define __ULISP_H_

#include "misc.h"

#include <stdio.h>

enum ObjectType {
	LISP_CONS = 1 << 0,
	LISP_SYMBOL = 1 << 1,
	LISP_PROC = 1 << 2,
	LISP_PROC_BUILTIN = 1 << 3,
	LISP_INTEGER = 1 << 4,
	LISP_FLOAT = 1 << 5,
	LISP_NUMBER = 1 << 6,
	LISP_LIST = 1 << 7,
	GC_MARKED = 1 << 8
};

typedef struct {
	enum ObjectType type;
	char data[];
} LispObject;

typedef struct {
	LispObject *car, *cdr;
} ConsCell;

typedef struct {
	LispObject* expression;
	LispObject* arguments;
	LispObject* environnement;
	GLboolean is_macro;
} LispProc;

typedef struct {
	uint hash;
	char str[];
} LispSymbol;

extern LispObject *nil;

LispObject* ulisp_read_list(const char* string);
LispObject* ulisp_eval(LispObject* expression, LispObject* env);
void ulisp_init(void);
void ulisp_print(LispObject* obj, FILE* stream);
char* ulisp_debug_print(LispObject* obj);

#endif // __ULISP_H_
