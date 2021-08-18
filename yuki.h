#ifndef YUKI_H
#define YUKI_H

#include "misc.h"

#include <inttypes.h>
#include <assert.h>

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
	yk_t_array,
	yk_t_stream,
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

#define YK_IMMEDIATE(x) ((YkUint)(x) & 15)
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

#define YK_TYPEOF(x) ((YK_IMMEDIATE(x) == 0) ? (x)->t : YK_IMMEDIATE(x))

/* List operations */
#define YK_CAR(x) (YK_PTR(x)->cons.car)
#define YK_CDR(x) (YK_PTR(x)->cons.cdr)

/* Error handling */
#define YK_ASSERT(cond) assert(cond)

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


/* Opcodes */
typedef enum {
	YK_OP_FETCH_LITERAL = 0,
	YK_OP_FETCH_GLOBAL,
	YK_OP_LEXICAL_VAR,
	YK_OP_PUSH,
	YK_OP_UNBIND,
	YK_OP_CALL,
	YK_OP_RET,
	YK_OP_END
} YkOpcode;

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
		yk_s_macro,
		yk_s_constant
	} type;
	uint32_t hash;
	char* name;
	uint8_t declared;
} YkSymbol;

typedef struct {
	YkObject name;
	YkObject docstring;
	YkInt nargs;
	YkCfun cfun;
} YkCProc;

typedef struct {
	YkObject ptr;
	uint16_t modifier;
	YkOpcode opcode;
} YkInstruction;

typedef struct {
	YkObject name;
	YkObject docstring;
	YkInstruction* code;
	YkUint code_size;
	YkUint code_capacity;
	uint64_t dummy;			/* Needed for alignment */
} YkBytecode;

typedef struct {
	YkObject bytecode;
	YkObject lexical_env;
} YkClosure;

union YkUnion {
	YkType t;
	double number_double;
	YkCons cons;
	YkSymbol symbol;
	YkCProc c_proc;
	YkClosure closure;
	YkBytecode bytecode;
};

ct_assert(sizeof(union YkUnion) % 16 == 0);

void yk_init();
YkObject yk_cons(YkObject car, YkObject cdr);
void yk_print(YkObject o);
YkObject yk_make_symbol(char* name);
YkObject yk_make_bytecode_begin(YkObject name);
void yk_bytecode_emit(YkObject bytecode, YkOpcode op, uint16_t modifier, YkObject ptr);
YkObject yk_read(const char* string);
YkObject yk_run(YkObject bytecode);
void yk_repl();

#endif
