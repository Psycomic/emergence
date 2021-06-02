#ifndef __ULISP_H_
#define __ULISP_H_

#include "misc.h"

#include <stdio.h>
#include <setjmp.h>

enum ObjectType {
	LISP_CONS = 1 << 0,
	LISP_SYMBOL = 1 << 1,
	LISP_PROC = 1 << 2,
	LISP_PROC_BUILTIN = 1 << 3,
	LISP_INTEGER = 1 << 4,
	LISP_FLOAT = 1 << 5,
	LISP_NUMBER = 1 << 6,
	LISP_LIST = 1 << 7,
	LISP_CONTINUATION = 1 << 8,
	GC_MARKED = 1 << 9
};

#define ULISP_BYTECODE_APPLY         0
#define ULISP_BYTECODE_PUSH_CONT     1
#define ULISP_BYTECODE_LOOKUP        2
#define ULISP_BYTECODE_PUSH_EVAL     3
#define ULISP_BYTECODE_END           4
#define ULISP_BYTECODE_RESUME_CONT   5
#define ULISP_BYTECODE_FETCH_LITERAL 6
#define ULISP_BYTECODE_BIND          7
#define ULISP_BYTECODE_BRANCH_IF     8
#define ULISP_BYTECODE_BRANCH_ELSE   9
#define ULISP_BYTECODE_BRANCH		 10

typedef struct {
	enum ObjectType type;
	char data[];
} LispObject;

typedef struct {
	LispObject *car, *cdr;
} ConsCell;

typedef struct {
	uint hash;
	char str[];
} LispSymbol;

typedef struct {
	uchar* code;
	uint literals_count;
	LispObject* literals[];
} LispTemplate;

typedef struct {
	LispObject* envt;
	LispTemplate* template;
} LispClosure;

typedef struct LispContinuation {
	LispObject* previous_cont;
	LispObject* envt_register;
	LispObject* eval_stack;
	LispTemplate* current_template;
	uchar* rip;
} LispContinuation;

extern LispObject *nil, *tee;
extern LispObject *value_register;

LispObject* ulisp_compile(LispObject* expression);
LispObject* ulisp_make_integer(long val);
LispObject* ulisp_make_float(double val);
LispObject* ulisp_read_list(const char* string);
LispObject* ulisp_read(const char* string);
void ulisp_run(LispTemplate* template);
LispObject* ulisp_make_closure(LispTemplate* template, LispObject* envt);
LispTemplate* ulisp_assembly_compile(LispObject* expressions);
void ulisp_init(void);
void ulisp_print(LispObject* obj, FILE* stream);
char* ulisp_debug_print(LispObject* obj);
void ulisp_add_to_environnement(char* name, LispObject* closure);

#endif // __ULISP_H_
