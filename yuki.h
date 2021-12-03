#ifndef YUKI_H
#define YUKI_H

#include "misc.h"

#include <inttypes.h>
#include <assert.h>
#include <stdbool.h>

typedef enum {
	yk_t_start = 0,
	/* Tagged values start */
	yk_t_list = 1 << 1,
	yk_t_int = 2 << 1,
	yk_t_float = 3 << 1,
	yk_t_symbol = 4 << 1,
	yk_t_c_proc = 5 << 1,
	yk_t_closure = 6 << 1,
	yk_t_bytecode = 7 << 1,
	/* Tagged values end */
	yk_t_continuation,
	yk_t_instance,
	yk_t_class,
	yk_t_array,
	yk_t_string,
	yk_t_string_stream,
	yk_t_file_stream
} YkType;

union YkUnion;

typedef int64_t YkInt;
typedef uint64_t YkUint;
typedef union YkUnion *YkObject;

/* Pointers are 128-bit aligned, meaning that they look like this:
 * XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX0000
 * We can store type information inside of the last 4 bits: there are
 * 7 possible values. */

#define YK_NIL YK_TAG(NULL, yk_t_list)
#define YK_NULL(x) ((x) == YK_NIL)

#define YK_IMMEDIATE(x) ((YkUint)(x) & 14)
#define YK_PTR(x) ((YkObject)((YkUint)(x) & ~(YkUint)15))
#define YK_TAG(x, t) ((YkObject)(((YkUint)x) | (t)))

#define YK_MAKE_INT(x) YK_TAG((x) << 4, yk_t_int)
#define YK_INTP(x) (YK_IMMEDIATE(x) == yk_t_int)
#define YK_INT(x) ((YkInt)((YkUint)x >> 4))

#define YK_MAKE_FLOAT(x) YK_TAG(float_as_binary(x) << 32, yk_t_float)
#define YK_FLOATP(x) (YK_IMMEDIATE(x) == yk_t_float)
#define YK_FLOAT(x) word_to_float((YkUint)x)

#define YK_TAG_LIST(x) YK_TAG(x, yk_t_list)
#define YK_LISTP(x) (YK_IMMEDIATE(x) == yk_t_list)
#define YK_CONSP(x) (!YK_NULL(x) && YK_LISTP(x))

#define YK_TAG_SYMBOL(x) YK_TAG(x, yk_t_symbol)
#define YK_SYMBOLP(x) (YK_NULL(x) || YK_IMMEDIATE(x) == yk_t_symbol)

#define YK_TAG_CPROC(x) YK_TAG(x, yk_t_c_proc)
#define YK_CPROCP(x) (YK_IMMEDIATE(x) == yk_t_c_proc)

#define YK_TAG_PROC(x) YK_TAG(x, yk_t_closure)
#define YK_CLOSUREP(x) (YK_IMMEDIATE(x) == yk_t_closure)

#define YK_TAG_BYTECODE(x) YK_TAG(x, yk_t_bytecode)
#define YK_BYTECODEP(x) (YK_IMMEDIATE(x) == yk_t_bytecode)

#define YK_CONTINUATIONP(x) ((x)->t.t == yk_t_continuation)

#define YK_FILE_STREAMP(x) ((x)->t.t == yk_t_file_stream)
#define YK_STRING_STREAMP(x) ((x)->t.t == yk_t_string_stream)
#define YK_STREAMP(x) (YK_FILE_STREAMP(x) || YK_STRING_STREAMP(x))

#define YK_TYPEOF(x) ((YK_IMMEDIATE(x) == 0) ? YK_PTR(x)->t.t : YK_IMMEDIATE(x))

#define YK_LIST_FOREACH(list, l) for (YkObject l = list; l != YK_NIL; l = YK_CDR(l))

/* List operations */
#define YK_CAR(x) (YK_PTR(x)->cons.car)
#define YK_CDR(x) (YK_PTR(x)->cons.cdr)

/* GC protection */
#define YK_GC_STACK_MAX_SIZE 1024
extern YkObject *yk_gc_stack[YK_GC_STACK_MAX_SIZE];
extern YkUint yk_gc_stack_size;

#define YK_GC_UNPROTECT yk_gc_stack_size = _yk_local_stack_ptr

#define YK_GC_PROTECT1(x) YkUint _yk_local_stack_ptr = yk_gc_stack_size; \
		yk_gc_stack[yk_gc_stack_size++] = &(x)

#define YK_GC_PROTECT2(x, y) YkUint _yk_local_stack_ptr = yk_gc_stack_size; \
	yk_gc_stack[yk_gc_stack_size++] = &(x);								\
	yk_gc_stack[yk_gc_stack_size++] = &(y);

#define YK_GC_PROTECT3(x, y, z) YkUint _yk_local_stack_ptr = yk_gc_stack_size; \
	yk_gc_stack[yk_gc_stack_size++] = &(x);								\
	yk_gc_stack[yk_gc_stack_size++] = &(y);								\
	yk_gc_stack[yk_gc_stack_size++] = &(z);

#define YK_GC_PROTECT4(x, y, z, w) YkUint _yk_local_stack_ptr = yk_gc_stack_size; \
	yk_gc_stack[yk_gc_stack_size++] = &(x);								\
	yk_gc_stack[yk_gc_stack_size++] = &(y);								\
	yk_gc_stack[yk_gc_stack_size++] = &(z);								\
	yk_gc_stack[yk_gc_stack_size++] = &(w);

#define YK_GC_PROTECT5(x, y, z, w, k) YkUint _yk_local_stack_ptr = yk_gc_stack_size; \
	yk_gc_stack[yk_gc_stack_size++] = &(x);								\
	yk_gc_stack[yk_gc_stack_size++] = &(y);								\
	yk_gc_stack[yk_gc_stack_size++] = &(z);								\
	yk_gc_stack[yk_gc_stack_size++] = &(w);								\
	yk_gc_stack[yk_gc_stack_size++] = &(k);

/* Macro utilites */

#define YK_DLET_BEGIN(var, val) YkObject _old_value = YK_PTR(var)->symbol.value; \
	YkObject _old_var = var;											\
	YK_PTR(var)->symbol.value = (val)

#define YK_DLET_END YK_PTR(_old_var)->symbol.value = _old_value

/* Opcodes */
typedef enum {
	YK_OP_FETCH_LITERAL = 0,
	YK_OP_FETCH_GLOBAL,
	YK_OP_LEXICAL_VAR,
	YK_OP_PUSH,
	YK_OP_PREPARE_CALL,
	YK_OP_CALL,
	YK_OP_TAIL_CALL,
	YK_OP_RET,
	YK_OP_JMP,
	YK_OP_JNIL,
	YK_OP_UNBIND,
	YK_OP_BIND_DYNAMIC,
	YK_OP_UNBIND_DYNAMIC,
	YK_OP_WITH_CONT,
	YK_OP_EXIT_CONT,
	YK_OP_EXIT,
	YK_OP_LEXICAL_SET,
	YK_OP_GLOBAL_SET,
	YK_OP_CLOSED_VAR,
	YK_OP_CLOSED_SET,
	YK_OP_END
} YkOpcode;

typedef struct {
	enum {
		YK_TOKEN_LEFT_PAREN,
		YK_TOKEN_RIGHT_PAREN,
		YK_TOKEN_DOT,
		YK_TOKEN_EOF,
		YK_TOKEN_INT,
		YK_TOKEN_FLOAT,
		YK_TOKEN_SYMBOL,
		YK_TOKEN_STRING,
	} type;

	union {
		struct {
			uint32_t begin_index;
			uint32_t size;
		} string_info;

		YkInt integer;
		float floating;
	} data;
} YkToken;

/* Yuki types */
typedef YkObject (*YkCfun)(YkUint nargs);

typedef struct {
	YkObject car;
	YkObject cdr;
} YkCons;

typedef struct {
	YkObject value;
	YkObject next_sym;
	enum YkSymbolType {
		yk_s_normal,
		yk_s_constant,
		yk_s_function,
		yk_s_macro
	} type;
	YkObject name;
	uint64_t hash;
	int32_t function_nargs;
	uint8_t declared;
} YkSymbol;

#define YK_PROFILE 0

typedef struct {
	YkObject name;
	YkObject docstring;
	YkInt nargs;
	YkCfun cfun;
#if YK_PROFILE
	YkUint calls;
	YkUint time_called;
#endif
} YkCProc;

typedef struct {
	YkObject ptr;
	uint16_t modifier;
	YkOpcode opcode;
} YkInstruction;

typedef struct {
	YkObject name;
	YkObject docstring;
	YkInt nargs;
	YkInstruction* code;
	YkUint code_size;
	YkUint code_capacity;
} YkBytecode;

typedef struct {
	YkObject bytecode;
	YkObject lexical_env;
} YkClosure;

typedef struct {
	YkObject symbol;
	YkObject old_value;
} YkDynamicBinding;

typedef struct {
	YkObject bytecode_register;
	YkType t;
	YkObject* lisp_stack_pointer;
	YkObject* lisp_frame_pointer;
	YkDynamicBinding* dynamic_bindings_stack_pointer;
	YkInstruction* program_counter;
	YkObject dummmy;
	uint8_t exited;
} YkContinuation;

typedef struct {
	uint8_t flags;
	uint32_t size;
	char data[];
} YkArrayAllocatorBlock;

typedef struct {
	YkObject dummy;
	YkType t;
	uint32_t size;
	uint32_t capacity;

	YkObject* data;
} YkArray;

#define YK_STREAM_FINISHED_BIT 0x1
#define YK_STREAM_BINARY_BIT   0x2
#define YK_STREAM_READ_BIT     0x4
#define YK_STREAM_WRITE_BIT    0x8

typedef struct {
	YkObject dummy;
	YkType t;

	FILE* file_ptr;
	uint8_t flags;
} YkFileStream;

typedef struct {
	YkObject dummy;
	YkType t;

	char* buffer;
	uint32_t capacity;
	uint32_t size;
	uint32_t read_bytes;
	uint8_t flags;
} YkStringStream;

typedef struct {
	YkObject dummy;
	YkType t;

	uint32_t size;
	char* data;
} YkString;

union YkUnion {
	struct {
		YkObject dummy;
		YkType t;
	} t;
	double number_double;
	YkCons cons;
	YkSymbol symbol;
	YkCProc c_proc;
	YkClosure closure;
	YkBytecode bytecode;
	YkContinuation continuation;
	YkArray array;
	YkString string;
	YkStringStream string_stream;
	YkFileStream file_stream;
};

ct_assert(sizeof(union YkUnion) % 16 == 0);

typedef struct {
	enum {
		YK_W_UNDECLARED_VARIABLE,
		YK_W_WRONG_NUMBER_OF_ARGUMENTS,
		YK_W_ASSIGNING_TO_FUNCTION,
		YK_W_DYNAMIC_BIND_FUNCTION
	} type;

	char* file;
	uint32_t line;
	uint32_t character;

	union {
		struct {
			YkObject symbol;
		} undeclared_variable;

		struct {
			YkObject function_symbol;
			YkInt expected_number;
			YkUint given_number;
		} wrong_number_of_arguments;

		struct {
			YkObject function_symbol;
		} assigning_to_function;

		struct {
			YkObject function_symbol;
		} dynamic_bind_function;
	} warning;
} YkWarning;

void yk_init();
YkObject yk_cons(YkObject car, YkObject cdr);
void yk_print(YkObject o);
YkObject yk_make_symbol(const char* name, uint size);
YkObject yk_make_bytecode_begin(YkObject name, YkInt nargs);
void yk_bytecode_emit(YkObject bytecode, YkOpcode op, uint16_t modifier, YkObject ptr);
void yk_bytecode_disassemble(YkObject bytecode);
YkObject yk_read(const char* string);
void yk_compile(YkObject forms, YkObject bytecode);
YkObject yk_run(YkObject bytecode);
void yk_repl();

YkObject yk_make_output_string_stream();
YkObject yk_stream_string(YkObject stream);
char* yk_string_to_c_str(YkObject string);
YkObject yk_make_symbol_cstr(const char* cstr);

/* Public variables */
extern YkObject yk_var_output;

#endif
