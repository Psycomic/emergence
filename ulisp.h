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
	LISP_STREAM = 1 << 9,
	LISP_FILE_STREAM = 1 << 10,
	LISP_STRING_STREAM = 1 << 11,
	GC_MARKED = 1 << 13
};

#define ULISP_BYTECODE_APPLY         0
#define ULISP_BYTECODE_PUSH_CONT     1
#define ULISP_BYTECODE_LOOKUP        2
#define ULISP_BYTECODE_PUSH_EVAL     3
#define ULISP_BYTECODE_END           4
#define ULISP_BYTECODE_RESUME_CONT   5
#define ULISP_BYTECODE_FETCH_LITERAL 6
#define ULISP_BYTECODE_BIND          7
#define ULISP_BYTECODE_LIST_BIND     8
#define ULISP_BYTECODE_BRANCH_IF     9
#define ULISP_BYTECODE_BRANCH_ELSE   10
#define ULISP_BYTECODE_BRANCH		 11
#define ULISP_BYTECODE_FETCH_CC      12
#define ULISP_BYTECODE_SET			 13

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
	LispObject* name;
	uint literals_count;
	LispObject* literals[];
} LispTemplate;

typedef struct {
	LispObject* envt;
	LispTemplate* template;
} LispClosure;

typedef struct {
	LispObject* name;
	LispObject* (*fn)(LispObject*);
} LispBuiltinProc;

typedef struct LispContinuation {
	LispObject* previous_cont;
	LispObject* envt_register;
	LispObject* eval_stack;
	LispTemplate* current_template;
	uchar* rip;
} LispContinuation;

typedef struct {
	char* buffer;
	size_t size;
	size_t capacity;
} LispStringStream;

typedef struct {
	FILE *f;
} LispFileStream;

LispObject* ulisp_eval(LispObject* expression);
LispObject* ulisp_read(const char* string);
void ulisp_init(void);
void ulisp_print(LispObject* obj, LispObject* stream);
char* ulisp_debug_print(LispObject* obj);
void ulisp_add_to_environnement(char* name, LispObject* closure);

LispObject* ulisp_standard_output;
LispObject* value_register;

#define MAX_ULISP_RUN_LEVEL 10

jmp_buf ulisp_top_level;
jmp_buf ulisp_run_level_stack[MAX_ULISP_RUN_LEVEL];

size_t ulisp_run_level_top;

#define ULISP_TOPLEVEL if (setjmp(ulisp_top_level) == 0)
#define ULISP_ABORT else

/* Unit tests */
LispObject* ulisp_make_symbol(const char* string);
LispObject* ulisp_make_file_stream(FILE* f);
LispObject* ulisp_make_string_stream();
void ulisp_stream_write(char* s, LispObject* stream);
void ulisp_stream_format(LispObject* stream, const char* format, ...);
char* ulisp_stream_finish_output(LispObject* stream);
LispObject* ulisp_list(LispObject* a, ...);

#endif // __ULISP_H_
