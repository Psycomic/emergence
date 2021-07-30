#ifndef YUKI_H
#define YUKI_H

#include "misc.h"

#include <inttypes.h>

typedef enum {
	yk_t_start = 0,
	/* Tagged values start */
	yk_t_cons = 1,
	yk_t_int = 2,
	yk_t_float = 3,
	yk_t_double = 4,
	yk_t_symbol = 5,
	yk_t_c_proc = 6,
	yk_t_proc = 7
	/* Tagged values end */
} yk_type;

typedef uint64_t yk_int;
typedef void *yk_object;

/* Pointers are 64-bit aligned, meaning that they look like this:
 * XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX000
 * We can store type information inside of the last 3 bits: there are
 * 7 possible values. */

#define IMMEDIATE(x) ((yk_int)(x) & 7)
#define PTR(x) (void*)((yk_int)(x) & ~7)
#define TAG(x, t) ((yk_int)(x) | (t))
#define INTP(x) (IMMEDIATE(x) == yk_t_int)
#define INT(x) (x >> 3)
#define CONSP(x) (IMMEDIATE(x) == yk_t_cons)
#define FLOATP(x) (IMMEDIATE(x) == yk_t_float)
#define DOUBLEP(x) (IMMEDIATE(x) == yk_t_double)
#define SYMBOLP(x) (IMMEDIATE(x) == yk_t_symbol)
#define CPROCP(x) (IMMEDIATE(x) == yk_t_c_proc)
#define PROCP(x) (IMMEDIATE(x) == yk_t_proc)

typedef struct {
	yk_object car;
	yk_object cdr;
} yk_cons;

void yk_allocator_init();
void* yk_alloc(size_t size);

#endif
