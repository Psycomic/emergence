#ifndef __ULISP_H_
#define __ULISP_H_

#include "misc.h"

#include <stdio.h>

enum ObjectType {
	LISP_CONS,
	LISP_SYMBOL,
	LISP_PROC,
	LISP_PROC_BUILTIN
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
} LispProc;

extern LispObject *environnement;

LispObject* ulisp_read_list(const char* string);
LispObject* ulisp_eval(LispObject* expression, LispObject* env);
void ulisp_init(void);
void ulisp_print(LispObject* obj, FILE* stream);

#endif // __ULISP_H_
