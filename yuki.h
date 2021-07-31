#ifndef YUKI_H
#define YUKI_H

#include "misc.h"

#include <inttypes.h>
#include <assert.h>

typedef enum {
	yk_t_start = 0,
	/* Tagged values start */
	yk_t_list = 1,
	yk_t_int = 2,
	yk_t_float = 3,
	yk_t_symbol = 4,
	yk_t_c_proc = 5,
	yk_t_closure = 6,
	yk_t_bytecode = 7,
	/* Tagged values end */
	yk_t_array,
	yk_t_stream,
} YkType;

union YkUnion;

typedef int64_t YkInt;
typedef uint64_t YkUint;
typedef union YkUnion *YkObject;

/* Pointers are 64-bit aligned, meaning that they look like this:
 * XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX000
 * We can store type information inside of the last 3 bits: there are
 * 7 possible values. */

#define YK_NIL YK_TAG(NULL, yk_t_list)
#define YK_NULL(x) ((x) == YK_NIL)

#define YK_IMMEDIATE(x) ((YkUint)(x) & 7)
#define YK_PTR(x) ((YkObject)((YkUint)(x) & ~(YkUint)7))
#define YK_TAG(x, t) ((YkObject)(((YkUint)x) | (t)))

#define YK_MAKE_INT(x) YK_TAG((x) << 3, yk_t_int)
#define YK_INTP(x) (YK_IMMEDIATE(x) == yk_t_int)
#define YK_INT(x) ((YkInt)((YkUint)x >> 3))

#define YK_MAKE_FLOAT(x) YK_TAG(float_as_binary(x) << 3, yk_t_float)
#define YK_FLOATP(x) (YK_IMMEDIATE(x) == yk_t_float)
#define YK_FLOAT(x) word_to_float((((YkUint)x) >> 3))

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
#define YK_CAR(x) (YK_NULL(x) ? YK_NIL : YK_PTR(x)->cons.car)
#define YK_CDR(x) (YK_NULL(x) ? YK_NIL : YK_PTR(x)->cons.cdr)

/* Error handling */
#define YK_ASSERT(cond) assert(cond)

/* Yuki types */
typedef YkObject (*YkCfun)(YkUint nargs, YkObject* args);

typedef struct {
	YkObject car;
	YkObject cdr;
} YkCons;

typedef struct {
	enum YkSymbolType {
		yk_s_normal,
		yk_s_macro,
		yk_s_constant
	} type;
	uint32_t hash;
	char* name;
	YkObject value;
	YkObject next_sym;
} YkSymbol;

typedef struct {
	YkObject name;
	YkUint nargs;
	YkCfun cfun;
} YkCProc;

typedef struct {
	YkObject bytecode;
	YkObject lexical_env;
} YkClosure;

typedef struct {
	YkObject name;
	uchar* code;
	YkUint code_size;
} YkBytecode;

union YkUnion {
	YkType t;
	double number_double;
	YkCons cons;
	YkSymbol symbol;
	YkCProc c_proc;
	YkClosure closure;
	YkBytecode bytecode;
};

void yk_init();
YkObject yk_read_list(const char* string);
YkObject yk_cons(YkObject car, YkObject cdr);
void yk_print(YkObject o);
YkObject yk_make_symbol(char* name);

#endif
