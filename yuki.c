#include "yuki.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#define YK_MARK_BIT ((YkUint)1)
#define YK_MARKED(x) (((YkUint)YK_CAR(x)) & YK_MARK_BIT)

#define YK_WORKSPACE_SIZE 2048

static union YkUnion* yk_workspace;
static union YkUnion* yk_free_list;
static YkUint yk_workspace_size;
static YkUint yk_free_space;

#define YK_SYMBOL_TABLE_SIZE 4096
static YkObject *yk_symbol_table;

/* Registers */
static YkObject yk_value_register;
static YkInstruction* yk_program_counter;
static YkObject yk_bytecode_register;

/* Symbols */
YkObject yk_tee, yk_nil;

/* Stacks */
YkObject *yk_gc_stack[YK_GC_STACK_MAX_SIZE];
YkUint yk_gc_stack_size;

#define YK_STACK_MAX_SIZE 1024
static YkObject yk_lisp_stack[YK_STACK_MAX_SIZE];
static YkObject* yk_lisp_stack_top;

static YkObject yk_return_stack[YK_STACK_MAX_SIZE];
static YkObject* yk_return_stack_top;

static YkDynamicBinding yk_dynamic_bindings_stack[YK_STACK_MAX_SIZE];
static YkDynamicBinding* yk_dynamic_bindings_stack_top;

static YkObject yk_continuations_stack[YK_STACK_MAX_SIZE];
static YkObject* yk_continuations_stack_top;

#define YK_PUSH(stack, x) *(--(stack)) = x
#define YK_POP(stack, x) x = *((stack)++)

#define YK_LISP_STACK_PUSH(x) YK_PUSH(yk_lisp_stack_top, x)
#define YK_LISP_STACK_POP(x) YK_POP(yk_lisp_stack_top, x)

#define YK_RET_PUSH(x) *(--yk_return_stack_top) = (void*)(x)
#define YK_RET_POP(x) x = (void*)(*(yk_return_stack_top++))

static void yk_gc();

static void yk_allocator_init() {
	yk_workspace = malloc(sizeof(union YkUnion) * YK_WORKSPACE_SIZE);
    yk_workspace_size = YK_WORKSPACE_SIZE;
	yk_free_list = yk_workspace;

	if ((uint64_t)yk_free_list % 16 != 0) {
		uint align_pad = 16 - (uint64_t)yk_free_list % 16;
		yk_free_list = (union YkUnion*)((char*)yk_free_list + align_pad);
		yk_workspace_size--;
	}

	for (uint i = 0; i < yk_workspace_size; i++) {
		if (i != yk_workspace_size - 1)
			yk_free_list[i].cons.car = yk_free_list + i + 1;
		else
			yk_free_list[i].cons.car = YK_NIL;
	}

	yk_free_space = yk_workspace_size;
}

#define YK_GC_STRESS 1

static YkObject yk_alloc() {
	if (YK_GC_STRESS || yk_free_space < 20)
		yk_gc();

	assert(yk_free_space > 0);

	YkObject first = yk_free_list;
	yk_free_list = first->cons.car;
	yk_free_space--;

	return first;
}

static void yk_mark(YkObject o) {
mark:
	if (YK_FLOATP(o) || YK_INTP(o) ||
		YK_PTR(o) < yk_workspace ||
		YK_PTR(o) > yk_workspace + yk_workspace_size ||
		YK_MARKED(o))
		return;

	if (YK_CONSP(o)) {
		yk_mark(YK_CAR(o));
		YK_CAR(o) = YK_TAG(YK_CAR(o), YK_MARK_BIT);

		o = YK_CDR(o);
		goto mark;
	}
	else if (YK_SYMBOLP(o)) {
		YK_CAR(o) = YK_TAG(YK_CAR(o), YK_MARK_BIT);

		o = YK_PTR(o)->symbol.value;
		goto mark;
	}
	else if (YK_CPROCP(o)) {
		YkObject docstring = YK_PTR(o)->c_proc.docstring;
		YK_CAR(o) = YK_TAG(YK_CAR(o), YK_MARK_BIT);

		o = docstring;
		goto mark;
	}
	else if (YK_BYTECODEP(o)) {
		YkObject bytecode = YK_PTR(o);

		YkObject docstring = bytecode->bytecode.docstring;
		YK_CAR(o) = YK_TAG(YK_CAR(o), YK_MARK_BIT);

		for (uint i = 0; i < bytecode->bytecode.code_size; i++) {
			yk_mark(bytecode->bytecode.code[i].ptr);
		}

		o = docstring;
		goto mark;
	}
	else if (YK_CONTINUATIONP(o)) {
		YkObject bytecode = YK_PTR(o)->continuation.bytecode_register;
		YK_CAR(o) = YK_TAG(YK_CAR(o), YK_MARK_BIT);

		o = bytecode;
		goto mark;
	}
	else {
		YK_CAR(o) = YK_TAG(YK_CAR(o), YK_MARK_BIT);
	}
}

static void yk_free(YkObject o) {
	o->cons.car = yk_free_list;
	yk_free_list = o;

	yk_free_space++;
}

static void yk_sweep() {
	printf("before: %ld free space\n", yk_free_space);
	yk_free_space = 0;

	for (uint i = 0; i < yk_workspace_size; i++) {
		YkObject o = yk_workspace + i;

		if (!YK_MARKED(o))
			yk_free(o);
		else
			YK_CAR(o) = (YkObject)((YkUint)YK_CAR(o) & ~YK_MARK_BIT);
	}

	printf("after: %ld free space\n", yk_free_space);
}

static void yk_gc() {
	yk_mark(yk_value_register);
	yk_mark(yk_bytecode_register);

	for (size_t i = 0; i < YK_SYMBOL_TABLE_SIZE; i++)
		yk_mark(yk_symbol_table[i]);

	for (size_t i = 0; i < yk_gc_stack_size; i++)
		yk_mark(*yk_gc_stack[i]);

	for (long i = 0; i < yk_lisp_stack_top - yk_lisp_stack - YK_STACK_MAX_SIZE; i++)
		yk_mark(yk_lisp_stack_top[i]);

	for (long i = 0; i < yk_continuations_stack_top - yk_continuations_stack -
			 YK_STACK_MAX_SIZE; i++)
		yk_mark(yk_continuations_stack_top[i]);

	for (long i = 0; i < yk_dynamic_bindings_stack_top -
			 yk_dynamic_bindings_stack - YK_STACK_MAX_SIZE;
		 i++)
	{
		yk_mark(yk_dynamic_bindings_stack_top[i].symbol);
		yk_mark(yk_dynamic_bindings_stack_top[i].old_value);
	}

	for (long i = 0; i < yk_return_stack_top - yk_return_stack - YK_STACK_MAX_SIZE;
		 i += 2)
		yk_mark(yk_return_stack_top[i]);

	yk_sweep();
}

static void yk_symbol_table_init() {
	yk_symbol_table = malloc(sizeof(YkObject) * YK_SYMBOL_TABLE_SIZE);

	for (uint i = 0; i < YK_SYMBOL_TABLE_SIZE; i++) {
		yk_symbol_table[i] = NULL;
	}
}

static void yk_make_builtin(char* name, YkInt nargs, YkCfun fn) {
	YkObject proc = YK_NIL, sym = YK_NIL;
	YK_GC_PROTECT2(proc, sym);

	sym = yk_make_symbol(name);
	sym->symbol.declared = 1;

	proc = yk_alloc();
	proc->c_proc.name = sym;
	proc->c_proc.nargs = nargs;
	proc->c_proc.cfun = fn;

	YK_PTR(sym)->symbol.value = YK_TAG(proc, yk_t_c_proc);

	YK_GC_UNPROTECT;
}

static YkInt yk_signed_fixnum_to_long(YkInt i) {
	if (i & (1L << 59))
		i |= 1L << 63 | 1L << 62 | 1L << 61 | 1L << 60;

	return i;
}

static float yk_fixnum_to_float(YkObject fix) {
	return (float)yk_signed_fixnum_to_long(YK_INT(fix));
}

static YkObject yk_builtin_neq(YkUint nargs) {
	YK_ASSERT(YK_INTP(yk_lisp_stack_top[0]) || YK_FLOATP(yk_lisp_stack_top[0]));
	YK_ASSERT(YK_INTP(yk_lisp_stack_top[1]) || YK_FLOATP(yk_lisp_stack_top[1]));

	return yk_lisp_stack_top[0] == yk_lisp_stack_top[1] ? yk_tee : YK_NIL;
}

static YkObject yk_builtin_add(YkUint nargs) {
	YkInt i_result = 0;
	float f_result = 0.f;

	char isfloat = 0;

	for (uint i = 0; i < nargs; i++) {
		if (isfloat) {
			if (YK_INTP(yk_lisp_stack_top[i])) {
				f_result += yk_fixnum_to_float(yk_lisp_stack_top[i]);
			} else if (YK_FLOATP(yk_lisp_stack_top[i])) {
				f_result += YK_FLOAT(yk_lisp_stack_top[i]);
			} else {
				YK_ASSERT(0);
			}
		} else {
			if (YK_INTP(yk_lisp_stack_top[i])) {
				i_result += YK_INT(yk_lisp_stack_top[i]);
			} else if (YK_FLOATP(yk_lisp_stack_top[i])) {
				f_result = (float)yk_signed_fixnum_to_long(i_result);
				isfloat = 1;
				f_result += YK_FLOAT(yk_lisp_stack_top[i]);
			} else {
				YK_ASSERT(0);
			}
		}
	}

	if (isfloat)
		return YK_MAKE_FLOAT(f_result);
	else
		return YK_MAKE_INT(i_result);
}

static YkObject yk_builtin_sub(YkUint nargs) {
	if (nargs == 1) {
		YK_ASSERT(YK_INTP(yk_lisp_stack_top[0]) || YK_FLOATP(yk_lisp_stack_top[0]));

		if (YK_INTP(yk_lisp_stack_top[0]))
			return YK_MAKE_INT(-YK_INT(yk_lisp_stack_top[0]));
		else
			return YK_MAKE_FLOAT(-YK_FLOAT(yk_lisp_stack_top[0]));
	}

	YkInt i_result;
	float f_result;
	char isfloat;

	if (YK_INTP(yk_lisp_stack_top[0])) {
		i_result = YK_INT(yk_lisp_stack_top[0]);
		isfloat = 0;
	} else {
		f_result = YK_FLOAT(yk_lisp_stack_top[0]);
		isfloat = 1;
	}

	for (uint i = 1; i < nargs; i++) {
		if (isfloat) {
			if (YK_INTP(yk_lisp_stack_top[i])) {
				f_result -= yk_signed_fixnum_to_long(YK_INT(yk_lisp_stack_top[i]));
			} else if (YK_FLOATP(yk_lisp_stack_top[i])) {
				f_result -= YK_FLOAT(yk_lisp_stack_top[i]);
			} else {
				YK_ASSERT(0);
			}
		} else {
			if (YK_INTP(yk_lisp_stack_top[i])) {
				i_result -= YK_INT(yk_lisp_stack_top[i]);
			} else if (YK_FLOATP(yk_lisp_stack_top[i])) {
				f_result = (float)yk_signed_fixnum_to_long(i_result);
				isfloat = 1;
				f_result -= YK_FLOAT(yk_lisp_stack_top[i]);
			} else {
				YK_ASSERT(0);
			}
		}
	}

	if (isfloat)
		return YK_MAKE_FLOAT(f_result);
	else
		return YK_MAKE_INT(i_result);
}

static YkObject yk_builtin_mul(YkUint nargs) {
	YkInt i_result = 1;
	float f_result = 1.f;

	char isfloat = 0;

	for (uint i = 0; i < nargs; i++) {
		if (isfloat) {
			if (YK_INTP(yk_lisp_stack_top[i])) {
				f_result *= yk_fixnum_to_float(yk_lisp_stack_top[i]);
			} else if (YK_FLOATP(yk_lisp_stack_top[i])) {
				f_result *= YK_FLOAT(yk_lisp_stack_top[i]);
			} else {
				YK_ASSERT(0);
			}
		} else {
			if (YK_INTP(yk_lisp_stack_top[i])) {
				i_result *= YK_INT(yk_lisp_stack_top[i]);
			} else if (YK_FLOATP(yk_lisp_stack_top[i])) {
				f_result = (float)yk_signed_fixnum_to_long(i_result);
				isfloat = 1;
				f_result *= YK_FLOAT(yk_lisp_stack_top[i]);
			} else {
				YK_ASSERT(0);
			}
		}
	}

	if (isfloat)
		return YK_MAKE_FLOAT(f_result);
	else
		return YK_MAKE_INT(i_result);
}

static YkObject yk_builtin_div(YkUint nargs) {
	if (nargs == 1) {
		YK_ASSERT(YK_INTP(yk_lisp_stack_top[0]) || YK_FLOATP(yk_lisp_stack_top[0]));

		if (YK_INTP(yk_lisp_stack_top[0]))
			return YK_MAKE_FLOAT(1.f / yk_fixnum_to_float(yk_lisp_stack_top[0]));
		else
			return YK_MAKE_FLOAT(1.f / YK_FLOAT(yk_lisp_stack_top[0]));
	}

	YkInt i_result;
	float f_result;
	char isfloat;

	if (YK_INTP(yk_lisp_stack_top[0])) {
		i_result = YK_INT(yk_lisp_stack_top[0]);
		isfloat = 0;
	} else {
		f_result = YK_FLOAT(yk_lisp_stack_top[0]);
		isfloat = 1;
	}

	for (uint i = 1; i < nargs; i++) {
		if (isfloat) {
			if (YK_INTP(yk_lisp_stack_top[i])) {
				f_result /= yk_fixnum_to_float(yk_lisp_stack_top[i]);
			} else if (YK_FLOATP(yk_lisp_stack_top[i])) {
				f_result /= YK_FLOAT(yk_lisp_stack_top[i]);
			} else {
				YK_ASSERT(0);
			}
		} else {
			if (YK_INTP(yk_lisp_stack_top[i])) {
				i_result /= YK_INT(yk_lisp_stack_top[i]);
			} else if (YK_FLOATP(yk_lisp_stack_top[i])) {
				f_result = (float)yk_signed_fixnum_to_long(i_result);
				isfloat = 1;
				f_result /= YK_FLOAT(yk_lisp_stack_top[i]);
			} else {
				YK_ASSERT(0);
			}
		}
	}

	if (isfloat)
		return YK_MAKE_FLOAT(f_result);
	else
		return YK_MAKE_INT(i_result);
}

static YkObject yk_builtin_pow(YkUint nargs) {
	YK_ASSERT(YK_INTP(yk_lisp_stack_top[0]) || YK_FLOATP(yk_lisp_stack_top[0]));
	YK_ASSERT(YK_INTP(yk_lisp_stack_top[1]) || YK_FLOATP(yk_lisp_stack_top[1]));

	if (YK_INTP(yk_lisp_stack_top[0])) {
		if (YK_INTP(yk_lisp_stack_top[1])) {
			return YK_MAKE_INT(powl(YK_INT(yk_lisp_stack_top[0]),
									YK_INT(yk_lisp_stack_top[1])));
		} else {
			return YK_MAKE_FLOAT(powf(yk_fixnum_to_float(yk_lisp_stack_top[0]),
									  YK_FLOAT(yk_lisp_stack_top[1])));
		}
	} else {
		if (YK_INTP(yk_lisp_stack_top[1])) {
			return YK_MAKE_FLOAT(powf(YK_FLOAT(yk_lisp_stack_top[0]),
									  yk_fixnum_to_float(yk_lisp_stack_top[1])));
		} else {
			return YK_MAKE_FLOAT(powf(YK_FLOAT(yk_lisp_stack_top[0]),
									  YK_FLOAT(yk_lisp_stack_top[1])));
		}
	}
}

static YkObject yk_builtin_cons(YkUint nargs) {
	return yk_cons(yk_lisp_stack_top[0], yk_lisp_stack_top[1]);
}

static YkObject yk_builtin_head(YkUint nargs) {
	if (yk_lisp_stack_top[0] == YK_NIL) return YK_NIL;

	return YK_CAR(yk_lisp_stack_top[0]);
}

static YkObject yk_builtin_tail(YkUint nargs) {
	if (yk_lisp_stack_top[0] == YK_NIL) return YK_NIL;

	return YK_CDR(yk_lisp_stack_top[0]);
}

static YkObject yk_builtin_second(YkUint nargs) {
	if (yk_lisp_stack_top[0] == YK_NIL) return YK_NIL;
	YkObject cdr = YK_CDR(yk_lisp_stack_top[0]);
	if (cdr == YK_NIL) return YK_NIL;

	return YK_CAR(cdr);
}

static YkObject yk_builtin_third(YkUint nargs) {
	if (yk_lisp_stack_top[0] == YK_NIL) return YK_NIL;
	YkObject cdr = YK_CDR(yk_lisp_stack_top[0]);
	if (cdr == YK_NIL) return YK_NIL;
	cdr = YK_CDR(cdr);
	if (cdr == YK_NIL) return YK_NIL;

	return YK_CAR(cdr);
}

static YkObject yk_builtin_intp(YkUint nargs) {
	return YK_INTP(yk_lisp_stack_top[0]) ? yk_tee : YK_NIL;
}

static YkObject yk_builtin_floatp(YkUint nargs) {
	return YK_FLOATP(yk_lisp_stack_top[0]) ? yk_tee : YK_NIL;
}

static YkObject yk_builtin_consp(YkUint nargs) {
	return YK_CONSP(yk_lisp_stack_top[0]) ? yk_tee : YK_NIL;
}

static YkObject yk_builtin_nullp(YkUint nargs) {
	return yk_lisp_stack_top[0] == YK_NIL ? yk_tee : YK_NIL;
}

static YkObject yk_builtin_listp(YkUint nargs) {
	return YK_LISTP(yk_lisp_stack_top[0]) ? yk_tee : YK_NIL;
}

static YkObject yk_builtin_symbolp(YkUint nargs) {
	return YK_SYMBOLP(yk_lisp_stack_top[0]) ? yk_tee : YK_NIL;
}

static YkObject yk_builtin_closurep(YkUint nargs) {
	return YK_CLOSUREP(yk_lisp_stack_top[0]) ? yk_tee : YK_NIL;
}

static YkObject yk_builtin_bytecodep(YkUint nargs) {
	return YK_BYTECODEP(yk_lisp_stack_top[0]) ? yk_tee : YK_NIL;
}

static YkObject yk_builtin_compiled_functionp(YkUint nargs) {
	return YK_CPROCP(yk_lisp_stack_top[0]) ? yk_tee : YK_NIL;
}

static YkObject yk_builtin_boundp(YkUint nargs) {
	YkObject sym = yk_lisp_stack_top[0];
	YK_ASSERT(YK_SYMBOLP(sym));

	if (YK_PTR(sym)->symbol.value == NULL)
		return YK_NIL;
	else
		return yk_tee;
}

void yk_init() {
	yk_gc_stack_size = 0;
	yk_lisp_stack_top = yk_lisp_stack + YK_STACK_MAX_SIZE;
	yk_return_stack_top = yk_return_stack + YK_STACK_MAX_SIZE;
	yk_dynamic_bindings_stack_top = yk_dynamic_bindings_stack + YK_STACK_MAX_SIZE;
	yk_continuations_stack_top = yk_continuations_stack + YK_STACK_MAX_SIZE;
	yk_value_register = YK_NIL;
	yk_program_counter = NULL;

	yk_allocator_init();
	yk_symbol_table_init();

	/* Special symbols */
	yk_tee = yk_make_symbol("t");
	YK_PTR(yk_tee)->symbol.value = yk_tee;
	YK_PTR(yk_tee)->symbol.type = yk_s_constant;

	yk_nil = yk_make_symbol("nil");
	YK_PTR(yk_nil)->symbol.value = YK_NIL;
	YK_PTR(yk_nil)->symbol.type = yk_s_constant;

	/* Builtin functions */
	yk_make_builtin("+", -1, yk_builtin_add);
	yk_make_builtin("*", -1, yk_builtin_mul);
	yk_make_builtin("/", -1, yk_builtin_div);
	yk_make_builtin("-", -2, yk_builtin_sub);
	yk_make_builtin("**", 2, yk_builtin_pow);

	yk_make_builtin("=", 2, yk_builtin_neq);

	yk_make_builtin(":", 2, yk_builtin_cons);
	yk_make_builtin("head", 1, yk_builtin_head);
	yk_make_builtin("tail", 1, yk_builtin_tail);
	yk_make_builtin("first", 1, yk_builtin_head);
	yk_make_builtin("second", 1, yk_builtin_second);
	yk_make_builtin("third", 1, yk_builtin_third);

	yk_make_builtin("int?", 1, yk_builtin_intp);
	yk_make_builtin("float?", 1, yk_builtin_floatp);
	yk_make_builtin("cons?", 1, yk_builtin_consp);
	yk_make_builtin("null?", 1, yk_builtin_nullp);
	yk_make_builtin("list?", 1, yk_builtin_listp);
	yk_make_builtin("symbol?", 1, yk_builtin_symbolp);
	yk_make_builtin("closure?", 1, yk_builtin_closurep);
	yk_make_builtin("bytecode?", 1, yk_builtin_bytecodep);
	yk_make_builtin("compiled-function?", 1, yk_builtin_compiled_functionp);

	yk_make_builtin("bound?", 1, yk_builtin_boundp);
}

YkObject yk_make_symbol(char* name) {
	uint32_t hash = hash_string((uchar*)name) % YK_SYMBOL_TABLE_SIZE;
	YkObject sym;

	if (yk_symbol_table[hash] == NULL) {
		sym = yk_alloc();

		sym->symbol.name = name;
		sym->symbol.hash = hash;
		sym->symbol.value = NULL;
		sym->symbol.next_sym = NULL;
		sym->symbol.type = yk_s_normal;
		sym->symbol.declared = 0;

		sym = YK_TAG_SYMBOL(sym);
		yk_symbol_table[hash] = sym;

		return sym;
	} else {
		YkObject s;
		for (s = yk_symbol_table[hash];
			 YK_PTR(s)->symbol.hash != hash &&
				 YK_PTR(s)->symbol.next_sym != NULL;
			 s = s->symbol.next_sym);

		if (YK_PTR(s)->symbol.hash == hash) {
			if (s == yk_nil)
				return YK_NIL;
			else
				return s;
		} else {
			sym = yk_alloc();

			sym->symbol.name = name;
			sym->symbol.hash = hash;
			sym->symbol.value = NULL;
			sym->symbol.next_sym = NULL;
			sym->symbol.type = yk_s_normal;
			sym->symbol.declared = 0;

			sym = YK_TAG_SYMBOL(sym);
			s->symbol.next_sym = sym;

			return sym;
		}
	}
}

YkObject yk_make_continuation(uint16_t offset) {
	YkObject cont = yk_alloc();
	cont->t = yk_t_continuation;

	cont->continuation.lisp_stack_pointer = yk_lisp_stack_top;
	cont->continuation.return_stack_pointer = yk_return_stack_top;
	cont->continuation.dynamic_bindings_stack_pointer = yk_dynamic_bindings_stack_top;
	cont->continuation.bytecode_register = yk_bytecode_register;
	cont->continuation.program_counter = YK_PTR(yk_bytecode_register)->bytecode.code + offset;
	cont->continuation.exited = 0;

	return cont;
}

YkObject yk_cons(YkObject car, YkObject cdr) {
	YK_GC_PROTECT2(car, cdr);

	YkObject o = yk_alloc();
	o->cons.car = car;
	o->cons.cdr = cdr;

	YK_GC_UNPROTECT;
	return YK_TAG_LIST(o);
}

#define BYTECODE_DEFAULT_SIZE 8

YkObject yk_make_bytecode_begin(YkObject name, YkInt nargs) {
	YK_GC_PROTECT1(name);		/* This function can GC */

	YkObject bytecode = yk_alloc();
	bytecode->bytecode.name = name;
	bytecode->bytecode.docstring = YK_NIL;
	bytecode->bytecode.code = malloc(8 * sizeof(YkInstruction));
	bytecode->bytecode.code_size = 0;
	bytecode->bytecode.code_capacity = 8;
	bytecode->bytecode.nargs = nargs;

	YK_GC_UNPROTECT;
	return YK_TAG(bytecode, yk_t_bytecode);
}

void yk_bytecode_emit(YkObject bytecode, YkOpcode op, uint16_t modifier, YkObject ptr) {
	YkObject bytecode_ptr = YK_PTR(bytecode);

	if (bytecode_ptr->bytecode.code_size >= bytecode_ptr->bytecode.code_capacity) {
		bytecode_ptr->bytecode.code_capacity += 8;
		bytecode_ptr->bytecode.code =
			realloc(bytecode_ptr->bytecode.code,
					sizeof(YkInstruction) *	bytecode_ptr->bytecode.code_capacity);
	}

	YkInstruction* last_i = bytecode_ptr->bytecode.code + bytecode_ptr->bytecode.code_size++;
	last_i->ptr = ptr;
	last_i->modifier = modifier;
	last_i->opcode = op;
}

static YkObject yk_nreverse(YkObject list) {
	YK_ASSERT(YK_LISTP(list));

	YkObject current = list,
		next, previous = YK_NIL,
		last = YK_NIL;

	while (current != YK_NIL) {
		next = YK_CDR(current);
		YK_CDR(current) = previous;
		previous = current;
		last = current;
		current = next;
	}

	return last;
}

#define IS_BLANK(x) ((x) == ' ' || (x) == '\n' || (x) == '\t')

static YkObject yk_read_list(const char* string, int string_size) {
	YkObject list = YK_NIL;
	YK_GC_PROTECT1(list);

	int i = string_size - 1;

	while (i >= 0) {
		for (; IS_BLANK(string[i]); i--) { /* Skip all blank chars */
			if (i < 0)
				break;
		}

		if (string[i] == ')') {
			int parens_count = 0;
			int j = i, count = 0;

			do {
				if (string[j] == ')')
					parens_count++;
				else if (string[j] == '(')
					parens_count--;

				YK_ASSERT(j >= 0);

				count++;
				YK_ASSERT(parens_count >= 0);
			} while(!(string[j--] == '(' && parens_count == 0));

			list = yk_cons(yk_read_list(string + j + 2, count - 2), list);
			i -= count;
		}
		else {
			int count = 0;
			int j = i;

			for (; !IS_BLANK(string[j]); j--) {
				if (j < 0)
					break;

				YK_ASSERT(string[j] != '(');
				count++;
			}

			if (count == 0)
				break;

			char* symbol_string = m_strndup(string + j + 1, count);

			if (strcmp(symbol_string, ".") == 0) { /* Dotted list */
				YK_ASSERT(YK_CDR(list) == YK_NIL);
				list = YK_CAR(list);
			}
			else {
				YkInt integer;
				double dfloating;

				int type = parse_number(symbol_string, &integer, &dfloating);

				float f = (float)dfloating;

				if (type == 0)
					list = yk_cons(YK_MAKE_INT(integer), list);
				else if (type == 1)
					list = yk_cons(YK_MAKE_FLOAT(f), list);
				else
					list = yk_cons(yk_make_symbol(symbol_string), list);
			}

			i -= count;
		}
	}

	YK_GC_UNPROTECT;
	return list;
}

YkObject yk_read(const char* string) {
	YkObject r = yk_read_list(string, strlen(string));
	return YK_CAR(r);
}

void yk_print(YkObject o) {
	switch (YK_TYPEOF(o)) {
	case yk_t_list:
		if (YK_NULL(o)) {
			printf("nil");
		} else {
			YkObject c;
			printf("(");

			for (c = o; YK_CONSP(c); c = YK_CDR(c)) {
				yk_print(YK_CAR(c));

				if (YK_CONSP(YK_CDR(c)))
					printf(" ");
			}

			if (!YK_NULL(c)) {
				printf(" . ");
				yk_print(c);
			}

			printf(")");
		}
		break;
	case yk_t_int:
		printf("%ld", yk_signed_fixnum_to_long(YK_INT(o)));
		break;
	case yk_t_float:
		printf("%f", YK_FLOAT(o));
		break;
	case yk_t_symbol:
		printf("%s", YK_PTR(o)->symbol.name);
		break;
	case yk_t_bytecode:
		printf("<bytecode %s at %p>",
			   YK_PTR(YK_PTR(o)->bytecode.name)->symbol.name,
			   YK_PTR(o));
		break;
	case yk_t_closure:
		printf("<closure at %p>", YK_PTR(o));
		break;
	case yk_t_c_proc:
		printf("<compiled-function %s at %p>",
			   YK_PTR(YK_PTR(o)->c_proc.name)->symbol.name,
			   YK_PTR(o));
		break;
	default:
		printf("Unknown type 0x%lx!\n", YK_TYPEOF(o));
	}
}

#define YK_RUN_DEBUG

YkObject yk_run(YkObject bytecode) {
	YK_ASSERT(YK_BYTECODEP(bytecode));

	yk_program_counter = YK_PTR(bytecode)->bytecode.code;
	yk_bytecode_register = bytecode;

start:
	switch (yk_program_counter->opcode) {
	case YK_OP_FETCH_LITERAL:
		yk_value_register = yk_program_counter->ptr;
		yk_program_counter++;
		break;
	case YK_OP_FETCH_GLOBAL:
	{
		YkObject val = YK_PTR(yk_program_counter->ptr)->symbol.value;
		YK_ASSERT(val != NULL);	/* Unbound variable */
		yk_value_register = val;
		yk_program_counter++;
	}
		break;
	case YK_OP_LEXICAL_VAR:
		yk_value_register = yk_lisp_stack_top[yk_program_counter->modifier];
		yk_program_counter++;
		break;
	case YK_OP_PUSH:
		if (yk_lisp_stack_top <= yk_lisp_stack)
			panic("Stack overflow!\n");

		YK_LISP_STACK_PUSH(yk_value_register);
		yk_program_counter++;
		break;
	case YK_OP_UNBIND:
		yk_lisp_stack_top += yk_program_counter->modifier;
		yk_program_counter++;
		break;
	case YK_OP_TAIL_CALL:
		if (YK_CLOSUREP(yk_value_register)) {
			panic("Not implemented!\n");
		}
		else if (YK_BYTECODEP(yk_value_register)) {
			YkInt last_frame_size = YK_INT(yk_lisp_stack_top[yk_program_counter->modifier]);

			YkObject* last_top = yk_lisp_stack_top;
			yk_lisp_stack_top += last_frame_size + yk_program_counter->modifier + 1;

			yk_lisp_stack_top -= yk_program_counter->modifier;
			memcpy(yk_lisp_stack_top, last_top, sizeof(YkObject) * yk_program_counter->modifier);

			YK_LISP_STACK_PUSH(YK_MAKE_INT(yk_program_counter->modifier));

			YkObject code = YK_PTR(yk_value_register);
			YkInt nargs = code->bytecode.nargs;
			if (nargs >= 0) {
				YK_ASSERT(yk_program_counter->modifier == nargs);
			}
			else {
				YK_ASSERT(yk_program_counter->modifier >= -(nargs + 1));
			}

			yk_bytecode_register = code;
			yk_program_counter = code->bytecode.code;
		}
		else if (YK_CPROCP(yk_value_register)) {
			YkObject proc = YK_PTR(yk_value_register);
			YkInt nargs = proc->c_proc.nargs;
			if (nargs >= 0) {
				YK_ASSERT(yk_program_counter->modifier == nargs);
			}
			else {
				YK_ASSERT(yk_program_counter->modifier >= -(nargs + 1));
			}

			yk_value_register = proc->c_proc.cfun(yk_program_counter->modifier);
			yk_lisp_stack_top += yk_program_counter->modifier;

			YkObject last_nargs; /* Only builtin functions return control in tail calls */
			YK_LISP_STACK_POP(last_nargs);
			yk_lisp_stack_top += YK_INT(last_nargs);
			YK_RET_POP(yk_program_counter);
			YK_RET_POP(yk_bytecode_register);
		}
		else {
			panic("Not a function!\n");
		}
		break;
	case YK_OP_CALL:
		if (YK_CLOSUREP(yk_value_register)) {
			panic("Not implemented!\n");
		}
		else if (YK_BYTECODEP(yk_value_register)) {
			YK_LISP_STACK_PUSH(YK_MAKE_INT(yk_program_counter->modifier));

			YK_RET_PUSH(yk_bytecode_register);
			YK_RET_PUSH(yk_program_counter + 1);

			YkObject code = YK_PTR(yk_value_register);
			YkInt nargs = code->bytecode.nargs;
			if (nargs >= 0) {
				YK_ASSERT(yk_program_counter->modifier == nargs);
			}
			else {
				YK_ASSERT(yk_program_counter->modifier >= -(nargs + 1));
			}

			yk_bytecode_register = code;
			yk_program_counter = code->bytecode.code;
		}
		else if (YK_CPROCP(yk_value_register)) {
			YkObject proc = YK_PTR(yk_value_register);
			YkInt nargs = proc->c_proc.nargs;
			if (nargs >= 0) {
				YK_ASSERT(yk_program_counter->modifier == nargs);
			}
			else {
				YK_ASSERT(yk_program_counter->modifier >= -(nargs + 1));
			}

			yk_value_register = proc->c_proc.cfun(yk_program_counter->modifier);
			yk_lisp_stack_top += yk_program_counter->modifier;
			yk_program_counter++;
		}
		else {
			panic("Not a function!\n");
		}
		break;
	case YK_OP_RET:
	{
		YkObject nargs;
		YK_LISP_STACK_POP(nargs);
		yk_lisp_stack_top += YK_INT(nargs);
		YK_RET_POP(yk_program_counter);
		YK_RET_POP(yk_bytecode_register);
	}
		break;
	case YK_OP_JMP:
		yk_program_counter =
			YK_PTR(yk_bytecode_register)->bytecode.code + yk_program_counter->modifier;
		break;
	case YK_OP_JNIL:
		if (yk_value_register == YK_NIL) {
			yk_program_counter =
				YK_PTR(yk_bytecode_register)->bytecode.code + yk_program_counter->modifier;
		}
		else {
			yk_program_counter++;
		}
		break;
	case YK_OP_BIND_DYNAMIC:
	{
		YkObject sym = yk_program_counter->ptr;
		yk_dynamic_bindings_stack_top--;
		yk_dynamic_bindings_stack_top->symbol = sym;
		yk_dynamic_bindings_stack_top->old_value = YK_PTR(sym)->symbol.value;

		YK_PTR(sym)->symbol.value = yk_value_register;
	}
		yk_program_counter++;
		break;
	case YK_OP_UNBIND_DYNAMIC:
		for (uint16_t i = 0; i < yk_program_counter->modifier; i++) {
			YK_PTR(yk_dynamic_bindings_stack_top[i].symbol)->symbol.value =
				yk_dynamic_bindings_stack_top[i].old_value;
		}
		yk_dynamic_bindings_stack_top += yk_program_counter->modifier;
		yk_program_counter++;
		break;
	case YK_OP_WITH_CONT:
		YK_PUSH(yk_continuations_stack_top,
				yk_make_continuation(yk_program_counter->modifier));
		yk_program_counter++;
		break;
	case YK_OP_EXIT_CONT:
	{
		YkObject exit = yk_continuations_stack_top[yk_program_counter->modifier];
		YK_ASSERT(!(YK_PTR(exit)->continuation.exited));

		for (uint i = 0; i < yk_program_counter->modifier; i--) {
			YkObject cont;
			YK_POP(yk_continuations_stack_top, cont);
			YK_PTR(cont)->continuation.exited = 1;
		}

		YkDynamicBinding* ptr = yk_dynamic_bindings_stack_top;
		YkDynamicBinding* next_ptr = YK_PTR(exit)->continuation.dynamic_bindings_stack_pointer;
		for (; ptr != next_ptr; ptr++) { /* todo */
			YK_PTR(ptr->symbol)->symbol.value = ptr->old_value;
		}

		yk_dynamic_bindings_stack_top = YK_PTR(exit)->continuation.dynamic_bindings_stack_pointer;
		yk_lisp_stack_top = YK_PTR(exit)->continuation.lisp_stack_pointer;
		yk_return_stack_top = YK_PTR(exit)->continuation.return_stack_pointer;
		yk_bytecode_register = YK_PTR(exit)->continuation.bytecode_register;
		yk_program_counter = YK_PTR(exit)->continuation.program_counter;

		YK_PTR(exit)->continuation.exited = 1;
		yk_continuations_stack_top++;
	}
		break;
	case YK_OP_EXIT:
	{
		YkObject exit;
		YK_POP(yk_continuations_stack_top, exit);
		YK_PTR(exit)->continuation.exited = 1;
		yk_program_counter++;
	}
		break;
	case YK_OP_END:
		goto end;
		break;
	}

#ifdef YK_RUN_DEBUG

	printf("\n ______STACK_____\n");
	if (yk_lisp_stack_top - yk_lisp_stack < YK_STACK_MAX_SIZE) {
		for (YkObject* ptr = yk_lisp_stack_top;
			 ptr < yk_lisp_stack + YK_STACK_MAX_SIZE; ptr++)
		{
			if (*ptr != NULL) {
				printf(" | ");
				yk_print(*ptr);
				printf("\t\t|\n");
			} else {
				printf(" | NULL\t\t|\n");
			}
		}
	}
	else {
		printf(" | EMPTY\t|\n");
	}
	printf(" ----------------\n");

	printf("VALUE: ");
	yk_print(yk_value_register);
	printf("\n");

#endif

	goto start;

end:
	return yk_value_register;
}

void yk_repl() {
	char buffer[2048];
	YkObject forms = YK_NIL, bytecode = YK_NIL;

	YK_GC_PROTECT1(forms);

	do {
		printf("\n> ");
		fgets(buffer, sizeof(buffer), stdin);

		forms = yk_read(buffer);

		if (YK_LISTP(forms)) {
			bytecode = yk_make_bytecode_begin(yk_make_symbol("toplevel"), 0);

			YkUint nargs = 0;
			YK_LIST_FOREACH(yk_nreverse(YK_CDR(forms)), f) {
				YkObject o = YK_CAR(f);
				yk_bytecode_emit(bytecode, YK_OP_FETCH_LITERAL, 0, o);
				yk_bytecode_emit(bytecode, YK_OP_PUSH, 0, YK_NIL);
				nargs++;
			}

			yk_bytecode_emit(bytecode, YK_OP_FETCH_GLOBAL, 0, YK_CAR(forms));
			yk_bytecode_emit(bytecode, YK_OP_CALL, nargs, YK_NIL);
			yk_bytecode_emit(bytecode, YK_OP_END, 0, YK_NIL);
		}

		yk_print(yk_run(bytecode));
		printf("\n");
	} while (strcmp(buffer, "bye\n") != 0);

	YK_GC_UNPROTECT;
}
