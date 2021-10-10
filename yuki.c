#include "yuki.h"

#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <math.h>
#include <stdbool.h>
#include <time.h>

#include "random.h"

#define YK_MARK_BIT ((YkUint)1)
#define YK_MARKED(x) (((YkUint)YK_CAR(x)) & YK_MARK_BIT)

#define YK_WORKSPACE_SIZE 2048

static union YkUnion* yk_workspace;
static union YkUnion* yk_free_list;
static YkUint yk_workspace_size;
static YkUint yk_free_space;

#define YK_ARRAY_ALLOCATOR_SIZE 4096
static char* yk_array_allocator;
static char* yk_array_allocator_top;

#define YK_SYMBOL_TABLE_SIZE 4096
static YkObject *yk_symbol_table;

/* Registers */
static YkObject yk_value_register;
static YkInstruction* yk_program_counter;
static YkObject yk_bytecode_register;

/* Symbols */
YkObject yk_tee, yk_nil, yk_debugger;

/* Stacks */
YkObject *yk_gc_stack[YK_GC_STACK_MAX_SIZE];
YkUint yk_gc_stack_size;

/* Other stuff */
static jmp_buf yk_jump_buf;

#define YK_STACK_MAX_SIZE 1024
static YkObject yk_lisp_stack[YK_STACK_MAX_SIZE];
static YkObject* yk_lisp_stack_top;

static YkReturnInfo yk_return_stack[YK_STACK_MAX_SIZE];
static YkReturnInfo* yk_return_stack_top;

static YkDynamicBinding yk_dynamic_bindings_stack[YK_STACK_MAX_SIZE];
static YkDynamicBinding* yk_dynamic_bindings_stack_top;

static YkObject yk_continuations_stack[YK_STACK_MAX_SIZE];
static YkObject* yk_continuations_stack_top;

#define YK_PUSH(stack, x) *(--(stack)) = x
#define YK_POP(stack, x) x = *((stack)++)

#define YK_LISP_STACK_PUSH(x) YK_PUSH(yk_lisp_stack_top, x)
#define YK_LISP_STACK_POP(x) YK_POP(yk_lisp_stack_top, x)

#define YK_RET_PUSH(x) *(--yk_return_stack_top) = (x)
#define YK_RET_POP(x) x = (*(yk_return_stack_top++))

static void yk_gc();
static YkObject yk_reverse(YkObject list);
static YkObject yk_nreverse(YkObject list);
YkUint yk_length(YkObject list);

static void yk_mark_block_data(void* data);
static void yk_array_allocator_sweep();

static YkObject yk_make_array(YkUint size, YkObject element);

static YkObject yk_arglist_cfun;

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
		YkObject value = YK_PTR(o)->symbol.value;
		YK_CAR(o) = YK_TAG(YK_CAR(o), YK_MARK_BIT);

		o = value;
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
	else if (YK_TYPEOF(o) == yk_t_array) {
		YK_CAR(o) = YK_TAG(YK_CAR(o), YK_MARK_BIT);
		yk_mark_block_data(o->array.data);
	}
	else {
		YK_CAR(o) = YK_TAG(YK_CAR(o), YK_MARK_BIT);
	}
}

static void yk_free(YkObject o) {
#ifdef YK_GC_STRESS
	m_bzero(o, sizeof(union YkUnion));
#endif

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

static YkObject yk_gc_protected_stack[1024];
static YkUint yk_gc_protected_stack_size;

static void yk_permanent_gc_protect(YkObject object) {
	yk_gc_protected_stack[yk_gc_protected_stack_size++] = object;
}

static void yk_gc() {
	yk_mark(yk_value_register);
	yk_mark(yk_bytecode_register);

	for (size_t i = 0; i < yk_gc_protected_stack_size; i++) {
		yk_mark(yk_gc_protected_stack[i]);
	}

	for (size_t i = 0; i < YK_SYMBOL_TABLE_SIZE; i++)
		yk_mark(yk_symbol_table[i]);

	printf("GC stack: %ld\n", yk_gc_stack_size);
	for (size_t i = 0; i < yk_gc_stack_size; i++)
		yk_mark(*yk_gc_stack[i]);

	for (long i = 0; i < YK_STACK_MAX_SIZE - (yk_lisp_stack_top - yk_lisp_stack); i++)
		yk_mark(yk_lisp_stack_top[i]);

	for (long i = 0; i < YK_STACK_MAX_SIZE -
			 (yk_continuations_stack_top - yk_continuations_stack); i++)
	{
		yk_mark(yk_continuations_stack_top[i]);
	}

	for (long i = 0; i < YK_STACK_MAX_SIZE -
			 (yk_dynamic_bindings_stack_top - yk_dynamic_bindings_stack); i++)
	{
		yk_mark(yk_dynamic_bindings_stack_top[i].symbol);
		yk_mark(yk_dynamic_bindings_stack_top[i].old_value);
	}

	for (long i = 0; i < YK_STACK_MAX_SIZE -
			 (yk_return_stack_top - yk_return_stack); i++)
	{
		yk_mark(yk_return_stack_top[i].bytecode_register);
	}

	yk_array_allocator_sweep();
	yk_sweep();
}

#define YK_BLOCK_USED(b) (b->size & 0x80000000)
#define YK_BLOCK_SIZE(b) (b->size & ~0x80000000)

static void yk_array_allocator_init() {
	yk_array_allocator = malloc(YK_ARRAY_ALLOCATOR_SIZE);
	yk_array_allocator_top = yk_array_allocator;
}

static void* yk_array_allocator_alloc(YkUint size) {
	char* block_ptr = yk_array_allocator;
	while(block_ptr < yk_array_allocator_top) {
		YkArrayAllocatorBlock* block = (YkArrayAllocatorBlock*)block_ptr;

		if (!YK_BLOCK_USED(block) && YK_BLOCK_SIZE(block) >= size) {
			block->size = size | 0x80000000;
			block->marked = false;
			return block->data;
		}

		block_ptr += YK_BLOCK_SIZE(block) + sizeof(YkArrayAllocatorBlock);
	}

	YkArrayAllocatorBlock* block = (YkArrayAllocatorBlock*)yk_array_allocator_top;
	block->size = size | 0x80000000;
	block->marked = false;
	yk_array_allocator_top += sizeof(YkArrayAllocatorBlock) + size;

	return block->data;
}

static void yk_mark_block_data(void* data) {
	YkArrayAllocatorBlock* block = (YkArrayAllocatorBlock*)
		((char*)data - sizeof(YkArrayAllocatorBlock));

	block->marked = true;
}

static void yk_array_allocator_sweep() {
	char* block_ptr = yk_array_allocator;
	YkArrayAllocatorBlock* last_block = NULL;
	uint freed = 0;

	while(block_ptr < yk_array_allocator_top) {
		YkArrayAllocatorBlock* block = (YkArrayAllocatorBlock*)block_ptr;

		if (!block->marked) {
			block->size &= ~0x80000000;

			if (last_block) {
				last_block->size += YK_BLOCK_SIZE(block) +
					sizeof(YkArrayAllocatorBlock);
			} else {
				last_block = block;
			}

			freed++;
		} else {
			last_block = NULL;
		}

		block->marked = false;
		block_ptr += YK_BLOCK_SIZE(block) + sizeof(YkArrayAllocatorBlock);
	}

	printf("Freed %d blocks of memory!\n", freed);
}

static void yk_symbol_table_init() {
	yk_symbol_table = malloc(sizeof(YkObject) * YK_SYMBOL_TABLE_SIZE);

	for (uint i = 0; i < YK_SYMBOL_TABLE_SIZE; i++) {
		yk_symbol_table[i] = NULL;
	}
}

static YkObject yk_make_global_function(YkObject sym, YkInt nargs, YkCfun fn) {
	YkObject proc = YK_NIL;
	YK_GC_PROTECT2(proc, sym);

	proc = yk_alloc();
	proc->c_proc.name = sym;
	proc->c_proc.nargs = nargs;
	proc->c_proc.cfun = fn;

	YK_GC_UNPROTECT;
	return YK_TAG(proc, yk_t_c_proc);
}

static void yk_make_builtin(char* name, YkInt nargs, YkCfun fn) {
	YkObject proc = YK_NIL, sym = YK_NIL;
	YK_GC_PROTECT2(proc, sym);

	sym = yk_make_symbol(name);

	YK_PTR(sym)->symbol.value = yk_make_global_function(sym, nargs, fn);
	YK_PTR(sym)->symbol.declared = 1;
	YK_PTR(sym)->symbol.type = yk_s_function;
	YK_PTR(sym)->symbol.function_nargs = nargs;

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

	if (YK_INTP(yk_lisp_stack_top[0])) {
		if (YK_INTP(yk_lisp_stack_top[1])) {
			return yk_lisp_stack_top[0] == yk_lisp_stack_top[1] ? yk_tee : YK_NIL;
		} else {
			return yk_fixnum_to_float(yk_lisp_stack_top[0]) ==
				YK_FLOAT(yk_lisp_stack_top[1]) ? yk_tee : YK_NIL;
		}
	} else {
		if (YK_INTP(yk_lisp_stack_top[1])) {
			return YK_FLOAT(yk_lisp_stack_top[0]) ==
				yk_fixnum_to_float(yk_lisp_stack_top[1]) ? yk_tee : YK_NIL;
		} else {
			return YK_FLOAT(yk_lisp_stack_top[0]) ==
				YK_FLOAT(yk_lisp_stack_top[1]) ? yk_tee : YK_NIL;
		}
	}
}

static YkObject yk_builtin_nsup(YkUint nargs) {
	YK_ASSERT(YK_INTP(yk_lisp_stack_top[0]) || YK_FLOATP(yk_lisp_stack_top[0]));
	YK_ASSERT(YK_INTP(yk_lisp_stack_top[1]) || YK_FLOATP(yk_lisp_stack_top[1]));

	if (YK_INTP(yk_lisp_stack_top[0])) {
		if (YK_INTP(yk_lisp_stack_top[1])) {
			return yk_lisp_stack_top[0] > yk_lisp_stack_top[1] ? yk_tee : YK_NIL;
		} else {
			return yk_fixnum_to_float(yk_lisp_stack_top[0]) >
				YK_FLOAT(yk_lisp_stack_top[1]) ? yk_tee : YK_NIL;
		}
	} else {
		if (YK_INTP(yk_lisp_stack_top[0])) {
			return YK_FLOAT(yk_lisp_stack_top[0]) >
				yk_fixnum_to_float(yk_lisp_stack_top[1]) ? yk_tee : YK_NIL;
		} else {
			return YK_FLOAT(yk_lisp_stack_top[0]) >
				YK_FLOAT(yk_lisp_stack_top[1]) ? yk_tee : YK_NIL;
		}
	}
}

static YkObject yk_builtin_ninf(YkUint nargs) {
	YK_ASSERT(YK_INTP(yk_lisp_stack_top[0]) || YK_FLOATP(yk_lisp_stack_top[0]));
	YK_ASSERT(YK_INTP(yk_lisp_stack_top[1]) || YK_FLOATP(yk_lisp_stack_top[1]));

	if (YK_INTP(yk_lisp_stack_top[0])) {
		if (YK_INTP(yk_lisp_stack_top[1])) {
			return yk_lisp_stack_top[0] < yk_lisp_stack_top[1] ? yk_tee : YK_NIL;
		} else {
			return yk_fixnum_to_float(yk_lisp_stack_top[0]) <
				YK_FLOAT(yk_lisp_stack_top[1]) ? yk_tee : YK_NIL;
		}
	} else {
		if (YK_INTP(yk_lisp_stack_top[0])) {
			return YK_FLOAT(yk_lisp_stack_top[0]) <
				yk_fixnum_to_float(yk_lisp_stack_top[1]) ? yk_tee : YK_NIL;
		} else {
			return YK_FLOAT(yk_lisp_stack_top[0]) <
				YK_FLOAT(yk_lisp_stack_top[1]) ? yk_tee : YK_NIL;
		}
	}
}

static YkObject yk_builtin_nsupeq(YkUint nargs) {
	YK_ASSERT(YK_INTP(yk_lisp_stack_top[0]) || YK_FLOATP(yk_lisp_stack_top[0]));
	YK_ASSERT(YK_INTP(yk_lisp_stack_top[1]) || YK_FLOATP(yk_lisp_stack_top[1]));

	if (YK_INTP(yk_lisp_stack_top[0])) {
		if (YK_INTP(yk_lisp_stack_top[1])) {
			return yk_lisp_stack_top[0] >= yk_lisp_stack_top[1] ? yk_tee : YK_NIL;
		} else {
			return yk_fixnum_to_float(yk_lisp_stack_top[0]) >=
				YK_FLOAT(yk_lisp_stack_top[1]) ? yk_tee : YK_NIL;
		}
	} else {
		if (YK_INTP(yk_lisp_stack_top[0])) {
			return YK_FLOAT(yk_lisp_stack_top[0]) >=
				yk_fixnum_to_float(yk_lisp_stack_top[1]) ? yk_tee : YK_NIL;
		} else {
			return YK_FLOAT(yk_lisp_stack_top[0]) >=
				YK_FLOAT(yk_lisp_stack_top[1]) ? yk_tee : YK_NIL;
		}
	}
}

static YkObject yk_builtin_ninfeq(YkUint nargs) {
	YK_ASSERT(YK_INTP(yk_lisp_stack_top[0]) || YK_FLOATP(yk_lisp_stack_top[0]));
	YK_ASSERT(YK_INTP(yk_lisp_stack_top[1]) || YK_FLOATP(yk_lisp_stack_top[1]));

	if (YK_INTP(yk_lisp_stack_top[0])) {
		if (YK_INTP(yk_lisp_stack_top[1])) {
			return yk_lisp_stack_top[0] <= yk_lisp_stack_top[1] ? yk_tee : YK_NIL;
		} else {
			return yk_fixnum_to_float(yk_lisp_stack_top[0]) <=
				YK_FLOAT(yk_lisp_stack_top[1]) ? yk_tee : YK_NIL;
		}
	} else {
		if (YK_INTP(yk_lisp_stack_top[0])) {
			return YK_FLOAT(yk_lisp_stack_top[0]) <=
				yk_fixnum_to_float(yk_lisp_stack_top[1]) ? yk_tee : YK_NIL;
		} else {
			return YK_FLOAT(yk_lisp_stack_top[0]) <=
				YK_FLOAT(yk_lisp_stack_top[1]) ? yk_tee : YK_NIL;
		}
	}
}

static YkObject yk_builtin_eq(YkUint nargs) {
	return yk_lisp_stack_top[0] == yk_lisp_stack_top[1] ? yk_tee : YK_NIL;
}

static YkObject yk_builtin_not(YkUint nargs) {
	return yk_lisp_stack_top[0] == YK_NIL ? yk_tee : YK_NIL;
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
	YK_ASSERT(YK_LISTP(yk_lisp_stack_top[0]));
	if (yk_lisp_stack_top[0] == YK_NIL) return YK_NIL;

	return YK_CAR(yk_lisp_stack_top[0]);
}

static YkObject yk_builtin_tail(YkUint nargs) {
	YK_ASSERT(YK_LISTP(yk_lisp_stack_top[0]));
	if (yk_lisp_stack_top[0] == YK_NIL) return YK_NIL;

	return YK_CDR(yk_lisp_stack_top[0]);
}

static YkObject yk_builtin_second(YkUint nargs) {
	YK_ASSERT(YK_LISTP(yk_lisp_stack_top[0]));
	if (yk_lisp_stack_top[0] == YK_NIL) return YK_NIL;
	YkObject cdr = YK_CDR(yk_lisp_stack_top[0]);
	if (cdr == YK_NIL) return YK_NIL;

	return YK_CAR(cdr);
}

static YkObject yk_builtin_third(YkUint nargs) {
	YK_ASSERT(YK_LISTP(yk_lisp_stack_top[0]));
	if (yk_lisp_stack_top[0] == YK_NIL) return YK_NIL;
	YkObject cdr = YK_CDR(yk_lisp_stack_top[0]);
	if (cdr == YK_NIL) return YK_NIL;
	cdr = YK_CDR(cdr);
	if (cdr == YK_NIL) return YK_NIL;

	return YK_CAR(cdr);
}

static YkObject yk_builtin_list(YkUint nargs) {
	YkObject list = YK_NIL;
	YK_GC_PROTECT1(list);

	for (int i = nargs - 1; i >= 0; i--) {
		list = yk_cons(yk_lisp_stack_top[i], list);
	}

	YK_GC_UNPROTECT;
	return list;
}

static YkObject yk_builtin_reverse(YkUint nargs) {
	return yk_reverse(yk_lisp_stack_top[0]);
}

static YkObject yk_builtin_nreverse(YkUint nargs) {
	return yk_nreverse(yk_lisp_stack_top[0]);
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

static YkObject yk_builtin_documentation(YkUint nargs) {
	YkObject bytecode = yk_lisp_stack_top[0];
	YK_ASSERT(YK_BYTECODEP(bytecode));

	return YK_PTR(bytecode)->bytecode.docstring;
}

static YkObject yk_builtin_gensym(YkUint nargs) {
	char symbol_string[9];
	symbol_string[0] = '%';

	for (uint i = 1; i < 8; i++)
		symbol_string[i] = 'A' + (random_randint() % ('Z' - 'A'));

	symbol_string[8] = '\0';

	return yk_make_symbol(m_strdup(symbol_string));
}

static YkObject yk_builtin_clock(YkUint nargs) {
	YkInt time = clock();
	return YK_MAKE_INT(time);
}

static YkObject yk_builtin_print(YkUint nargs) {
	yk_print(yk_lisp_stack_top[0]);
	printf("\n");
	return YK_NIL;
}

static YkObject yk_builtin_random(YkUint nargs) {
	return YK_MAKE_INT(random_randint() % YK_INT(yk_lisp_stack_top[0]));
}

static YkObject yk_builtin_set_global(YkUint nargs) {
	YkObject symbol = yk_lisp_stack_top[0];
	YkObject value = yk_lisp_stack_top[1];

	YK_ASSERT(YK_SYMBOLP(symbol) && symbol != YK_NIL);

	if (YK_PTR(symbol)->symbol.value != NULL)
		YK_ASSERT(YK_PTR(symbol)->symbol.type != yk_s_constant);

	YK_PTR(symbol)->symbol.value = value;

	return value;
}

static YkObject yk_builtin_set_macro(YkUint nargs) {
	YkObject symbol = yk_lisp_stack_top[0];
	YkObject value = yk_lisp_stack_top[1];

	YK_ASSERT(YK_SYMBOLP(symbol) && symbol != YK_NIL);

	YK_PTR(symbol)->symbol.type = yk_s_macro;
	YK_PTR(symbol)->symbol.value = value;

	return symbol;
}

static YkObject yk_builtin_register_global(YkUint nargs) {
	YkObject sym = yk_lisp_stack_top[0];
	YK_ASSERT(YK_SYMBOLP(sym) && sym != YK_NIL);

	YK_PTR(sym)->symbol.type = yk_s_normal;
	YK_PTR(sym)->symbol.declared = 1;
	return sym;
}

static YkObject yk_builtin_register_function(YkUint nargs) {
	YkObject sym = yk_lisp_stack_top[0];
	YkObject fn_nargs = yk_lisp_stack_top[1];

	YK_PTR(sym)->symbol.declared = 1;
	YK_PTR(sym)->symbol.type = yk_s_function;
	YK_PTR(sym)->symbol.function_nargs = YK_INT(fn_nargs);

	return sym;
}


static YkObject yk_builtin_length(YkUint nargs) {
	YK_ASSERT(YK_LISTP(yk_lisp_stack_top[0]));

	return YK_MAKE_INT(yk_length(yk_lisp_stack_top[0]));
}

static YkObject yk_builtin_arguments_length(YkUint nargs) {
	YkObject lambda_list = yk_lisp_stack_top[0];
	YkInt length = 0;

	YkObject a = lambda_list;
	for (; YK_CONSP(a); a = YK_CDR(a)) {
		length++;
	}

	if (a != YK_NIL && YK_SYMBOLP(a)) {
		length = -(length + 1);
	}

	return YK_MAKE_INT(length);
}

static YkObject yk_builtin_append(YkUint nargs) {
	YkObject result = YK_NIL;
	YK_GC_PROTECT1(result);

	for (uint i = 0; i < nargs; i++) {
		YkObject list = yk_lisp_stack_top[i];

		YK_LIST_FOREACH(list, l) {
			result = yk_cons(YK_CAR(l), result);
		}
	}

	YK_GC_UNPROTECT;
	return yk_nreverse(result);
}

static YkObject yk_builtin_array(YkUint nargs) {
	YkObject array = yk_make_array(nargs, YK_NIL);

	for (uint i = 0; i < nargs; i++) {
		array->array.data[i] = yk_lisp_stack_top[i];
	}

	return array;
}

static YkObject yk_builtin_make_array(YkUint nargs) {
	YkObject array = yk_make_array(YK_INT(yk_lisp_stack_top[0]),
								   yk_lisp_stack_top[1]);

	return array;
}

static YkObject yk_builtin_list_to_array(YkUint nargs) {
	YkObject list = yk_lisp_stack_top[0];
	YkObject array = yk_make_array(yk_length(list), YK_NIL);

	uint i = 0;
	YK_LIST_FOREACH(list, l) {
		array->array.data[i++] = YK_CAR(l);
	}

	return array;
}

static YkObject yk_builtin_array_to_list(YkUint nargs) {
	YkObject array = yk_lisp_stack_top[0];

	YkObject list = YK_NIL;
	YK_GC_PROTECT1(list);

	for (int i = array->array.size - 1; i >= 0; i--) {
		list = yk_cons(array->array.data[i], list);
	}

	YK_GC_UNPROTECT;
	return list;
}

static YkObject yk_string_ref(YkObject string, YkInt i) {
	uchar* string_ptr = (uchar*)YK_PTR(string)->string.data;

	while (--i >= 0) {
		YK_ASSERT(*string_ptr != '\0');

		if (*string_ptr <= 0x7f) {
			string_ptr++;
		} else if (*string_ptr >> 6 == 2) {
			YK_ASSERT("error in encoding" == 0);
		} else if (*string_ptr >> 3 == 0x1e) {
			string_ptr += 4;
		} else if (*string_ptr >> 4 == 0xe) {
			string_ptr += 3;
		} else if (*string_ptr >> 5 == 6) {
			string_ptr += 2;
		}
	}

	return YK_MAKE_INT(u_string_to_codepoint(string_ptr));
}

static YkObject yk_builtin_aref(YkUint nargs) {
	YkObject array = yk_lisp_stack_top[0];
	YkInt index = YK_INT(yk_lisp_stack_top[1]);

	if (YK_TYPEOF(array) == yk_t_array) {
		YK_ASSERT(index < (YkInt)array->array.size && index >= 0);

		return array->array.data[index];
	} else if (YK_TYPEOF(array) == yk_t_string) {
		return yk_string_ref(array, index);
	} else {
		YK_ASSERT(0);
	}

	return YK_NIL;
}

static YkObject yk_builtin_aset(YkUint nargs) {
	YkObject array = yk_lisp_stack_top[0];
	YkInt index = YK_INT(yk_lisp_stack_top[1]);
	YkObject value = yk_lisp_stack_top[2];

	YK_ASSERT(index < (YkInt)array->array.size && index >= 0);

	array->array.data[index] = value;

	return value;
}

static YkObject yk_arglist(YkUint nargs) {
	YkUint offset = YK_INT(yk_lisp_stack_top[0]);
	YkUint argcount = YK_INT(yk_lisp_stack_top[1]);

	YkObject list = YK_NIL;
	YK_GC_PROTECT1(list);

	for (int i = argcount - offset - 1; i >= 0; i--) {
		list = yk_cons(yk_lisp_stack_top[offset + nargs + i + 1], list);
	}

	YK_GC_UNPROTECT;
	return list;
}

static YkObject yk_default_debugger(YkUint nargs) {
	YkObject error = yk_lisp_stack_top[0];

	printf("Unhandled error ");
	yk_print(error);
	printf("\n");
/*
	for (uint i = 0; i < YK_STACK_MAX_SIZE - (yk_return_stack_top - yk_return_stack); i++) {
		YkObject* stack_pointer = yk_return_stack_top[i].stack_pointer;
		uint nargs = YK_INT(stack_pointer[0]);

		printf("(");
		if (i == 0) {
			yk_print(YK_PTR(yk_bytecode_register)->bytecode.name);
		}
		else {
			YkObject bytecode = yk_return_stack_top[i - 1].bytecode_register;
			yk_print(YK_PTR(bytecode)->bytecode.name);
		}

		printf(" ");
		for (uint j = 0; j < nargs; j++) {
			yk_print(stack_pointer[1 + j]);

			if (j != nargs - 1)
				printf(" ");
		}
		printf(")\n");
	}
*/

	printf("1] ABORT TO TOPLEVEL\n"
		"2] DEBBUGER BREAKPOINT\n");

make_choice:
	printf("> ");
	int choice;
	m_scanf("%d", &choice);

	if (choice == 1) {
		yk_value_register = error;
		longjmp(yk_jump_buf, 1);
	}
	if (choice == 2) {
		assert(0);
	}
	else {
		goto make_choice;
	}

	return YK_NIL;
}

YkUint yk_length(YkObject list) {
	YkUint i = 0;
	YK_LIST_FOREACH(list, l) {
		i++;
	}

	return i;
}

YkObject yk_apply(YkObject function, YkObject args) {
	YkInt argcount = 0;
	YkObject result;

	args = yk_reverse(args);
	YK_LIST_FOREACH(args, e) {
		YK_PUSH(yk_lisp_stack_top, YK_CAR(e));
		argcount++;
	}

	if (YK_BYTECODEP(function)) {
		YkInt nargs = YK_PTR(function)->bytecode.nargs;
		if (nargs >= 0) {
			YK_ASSERT(argcount == nargs);
		}
		else {
			YK_ASSERT(argcount >= -(nargs + 1));
		}

		YK_PUSH(yk_lisp_stack_top, YK_MAKE_INT(argcount));

		static YkInstruction yk_end = {YK_NIL, 0, YK_OP_END};

		YkReturnInfo info = {
			.bytecode_register = NULL,
			.program_counter = &yk_end,
			.stack_pointer = NULL
		};

		YK_PUSH(yk_return_stack_top, info);

		yk_run(function);
		result = yk_value_register;
	} else if (YK_CPROCP(function)) {
		YkInt nargs = YK_PTR(function)->c_proc.nargs;
		if (nargs >= 0) {
			YK_ASSERT(argcount == nargs);
		}
		else {
			YK_ASSERT(argcount >= -(nargs + 1));
		}

		result = YK_PTR(function)->c_proc.cfun(yk_program_counter->modifier);
	}

	return result;
}

static YkObject yk_keyword_quote, yk_keyword_let, yk_keyword_lambda, yk_keyword_setq,
	yk_keyword_comptime, yk_keyword_do, yk_keyword_if, yk_keyword_dynamic_let,
	yk_keyword_with_cont, yk_keyword_exit, yk_keyword_loop,
	yk_symbol_argcount;

void yk_init() {
	yk_gc_stack_size = 0;
	yk_gc_protected_stack_size = 0;

	yk_lisp_stack_top = yk_lisp_stack + YK_STACK_MAX_SIZE;
	yk_return_stack_top = yk_return_stack + YK_STACK_MAX_SIZE;
	yk_dynamic_bindings_stack_top = yk_dynamic_bindings_stack + YK_STACK_MAX_SIZE;
	yk_continuations_stack_top = yk_continuations_stack + YK_STACK_MAX_SIZE;
	yk_value_register = YK_NIL;
	yk_program_counter = NULL;

	yk_allocator_init();
	yk_array_allocator_init();
	yk_symbol_table_init();

	/* Special symbols */
	yk_tee = yk_make_symbol("t");
	YK_PTR(yk_tee)->symbol.value = yk_tee;
	YK_PTR(yk_tee)->symbol.type = yk_s_constant;

	yk_nil = yk_make_symbol("nil");
	YK_PTR(yk_nil)->symbol.value = YK_NIL;
	YK_PTR(yk_nil)->symbol.type = yk_s_constant;

	yk_keyword_quote = yk_make_symbol("quote");
	yk_keyword_let = yk_make_symbol("let");
	yk_keyword_setq = yk_make_symbol("set!");
	yk_keyword_lambda = yk_make_symbol("named-lambda");
	yk_keyword_comptime = yk_make_symbol("comptime");
	yk_keyword_do = yk_make_symbol("do");
	yk_keyword_if = yk_make_symbol("if");
	yk_keyword_dynamic_let = yk_make_symbol("dynamic-let");
	yk_keyword_with_cont = yk_make_symbol("with-cont");
	yk_keyword_exit = yk_make_symbol("exit");
	yk_keyword_loop = yk_make_symbol("loop");

	yk_symbol_argcount = yk_make_symbol("*argcount*");

	/* Functions */
	YkObject arglist_symbol = yk_make_symbol("arglist");

	yk_arglist_cfun = yk_make_global_function(arglist_symbol, 2, yk_arglist);

	yk_permanent_gc_protect(yk_arglist_cfun);
	yk_permanent_gc_protect(arglist_symbol);

	/* Builtin functions */
	yk_make_builtin("+", -1, yk_builtin_add);
	yk_make_builtin("*", -1, yk_builtin_mul);
	yk_make_builtin("/", -1, yk_builtin_div);
	yk_make_builtin("-", -1, yk_builtin_sub);
	yk_make_builtin("**", 2, yk_builtin_pow);

	yk_make_builtin("=", 2, yk_builtin_neq);
	yk_make_builtin(">", 2, yk_builtin_nsup);
	yk_make_builtin("<", 2, yk_builtin_ninf);
	yk_make_builtin(">=", 2, yk_builtin_nsupeq);
	yk_make_builtin("<=", 2, yk_builtin_ninfeq);

	yk_make_builtin("eq?", 2, yk_builtin_eq);

	yk_make_builtin(":", 2, yk_builtin_cons);
	yk_make_builtin("head", 1, yk_builtin_head);
	yk_make_builtin("tail", 1, yk_builtin_tail);
	yk_make_builtin("first", 1, yk_builtin_head);
	yk_make_builtin("second", 1, yk_builtin_second);
	yk_make_builtin("third", 1, yk_builtin_third);
	yk_make_builtin("list", -1, yk_builtin_list);
	yk_make_builtin("length", 1, yk_builtin_length);
	yk_make_builtin("lambda-list-length", 1, yk_builtin_arguments_length);
	yk_make_builtin("not", 1, yk_builtin_not);
	yk_make_builtin("append", -1, yk_builtin_append);
	yk_make_builtin("reverse", 1, yk_builtin_reverse);
	yk_make_builtin("reverse!", 1, yk_builtin_nreverse);

	yk_make_builtin("array", -1, yk_builtin_array);
	yk_make_builtin("make-array", 2, yk_builtin_make_array);
	yk_make_builtin("aref", 2, yk_builtin_aref);
	yk_make_builtin("aset!", 3, yk_builtin_aset);
	yk_make_builtin("list->array", 1, yk_builtin_list_to_array);
	yk_make_builtin("array->list", 1, yk_builtin_array_to_list);

	yk_make_builtin("int?", 1, yk_builtin_intp);
	yk_make_builtin("float?", 1, yk_builtin_floatp);
	yk_make_builtin("pair?", 1, yk_builtin_consp);
	yk_make_builtin("null?", 1, yk_builtin_nullp);
	yk_make_builtin("list?", 1, yk_builtin_listp);
	yk_make_builtin("symbol?", 1, yk_builtin_symbolp);
	yk_make_builtin("closure?", 1, yk_builtin_closurep);
	yk_make_builtin("bytecode?", 1, yk_builtin_bytecodep);
	yk_make_builtin("compiled-function?", 1, yk_builtin_compiled_functionp);

	yk_make_builtin("documentation", 1, yk_builtin_documentation);
	yk_make_builtin("bound?", 1, yk_builtin_boundp);
	yk_make_builtin("gensym", 0, yk_builtin_gensym);

	yk_make_builtin("set-global!", 2, yk_builtin_set_global);
	yk_make_builtin("set-macro!", 2, yk_builtin_set_macro);
	yk_make_builtin("register-global!", 1, yk_builtin_register_global);
	yk_make_builtin("register-function!", 2, yk_builtin_register_function);

	yk_make_builtin("clock", 0, yk_builtin_clock);
	yk_make_builtin("random", 1, yk_builtin_random);

	yk_make_builtin("print", 1, yk_builtin_print);

	yk_make_builtin("invoke-debugger", 1, yk_default_debugger);
}

void yk_assert(uint8_t cond, const char* expression, const char* file, uint32_t line) {
	if (!cond) {
		char error_name[100];
		snprintf(error_name, sizeof(error_name), "<assertion-error '%s' at %s:%d>", expression, file, line);

		YK_PUSH(yk_lisp_stack_top, yk_make_symbol(m_strdup(error_name)));
		yk_default_debugger(1);
	}
}

YkObject yk_make_symbol(char* name) {
	YK_ASSERT(*name != '\0');

	uint64_t string_hash = hash_string((uchar*)name);
	uint16_t index = string_hash % YK_SYMBOL_TABLE_SIZE;
	YkObject sym;

	if (yk_symbol_table[index] == NULL) {
		sym = yk_alloc();

		sym->symbol.name = name;
		sym->symbol.hash = string_hash;
		sym->symbol.value = NULL;
		sym->symbol.next_sym = NULL;
		sym->symbol.type = yk_s_normal;
		sym->symbol.declared = 0;

		sym = YK_TAG_SYMBOL(sym);
		yk_symbol_table[index] = sym;

		return sym;
	} else {
		YkObject s;
		for (s = yk_symbol_table[index];
			 YK_PTR(s)->symbol.hash != string_hash &&
				 YK_PTR(s)->symbol.next_sym != NULL;
			 s = s->symbol.next_sym);

		if (YK_PTR(s)->symbol.hash == string_hash) {
			if (s == yk_nil)
				return YK_NIL;
			else
				return s;
		} else {
			sym = yk_alloc();

			sym->symbol.name = name;
			sym->symbol.hash = string_hash;
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
	cont->continuation.t = yk_t_continuation;

	cont->continuation.lisp_stack_pointer = yk_lisp_stack_top;
	cont->continuation.return_stack_pointer = yk_return_stack_top;
	cont->continuation.dynamic_bindings_stack_pointer = yk_dynamic_bindings_stack_top;
	cont->continuation.bytecode_register = yk_bytecode_register;
	cont->continuation.program_counter = YK_PTR(yk_bytecode_register)->bytecode.code + offset;
	cont->continuation.exited = 0;

	return cont;
}

static YkObject yk_make_array(YkUint size, YkObject element) {
	YkObject array = yk_alloc();

	array->array.t = yk_t_array;
	array->array.size = size;
	array->array.capacity = size;

	array->array.data = yk_array_allocator_alloc(size * sizeof(YkObject));

	for (uint i = 0; i < size; i++) {
		array->array.data[i] = element;
	}

	return array;
}

static YkObject yk_make_string(const char* cstr, size_t cstr_size) {
	YkObject string = yk_alloc();
	YkUint size = cstr_size + 1;

	string->string.t = yk_t_string;
	string->string.size = size - 1;
	string->string.data = yk_array_allocator_alloc(size);
	memcpy(string->string.data, cstr, size - 1);
	string->string.data[size - 1] = '\0';

	return string;
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
	YK_ASSERT(YK_BYTECODEP(bytecode));
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

static YkObject yk_reverse(YkObject list) {
	YkObject new_list = YK_NIL;

	YK_GC_PROTECT1(new_list);

	YK_LIST_FOREACH(list, l) {
		new_list = yk_cons(YK_CAR(l), new_list);
	}

	YK_GC_UNPROTECT;
	return new_list;
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

		if (string[i] == '"') {
			printf("Creating string...\n");
			YK_ASSERT(i > 0);
			int j;
			for (j = i - 1; string[j] != '"'; j--);

			YkInt str_obj_size = i - j - 1;
			YkObject str_obj = yk_make_string(string + j + 1, str_obj_size);
			list = yk_cons(str_obj, list);
			i -= str_obj_size + 2;
		}
		else if (string[i] == ')') {
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

				if (type == 0) {
					list = yk_cons(YK_MAKE_INT(integer), list);
				} else if (type == 1) {
					list = yk_cons(YK_MAKE_FLOAT(f), list);
				} else {
					YK_ASSERT(*symbol_string != '%');
					list = yk_cons(yk_make_symbol(symbol_string), list);
				}
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
	case yk_t_array:
		printf("[");
		for (uint i = 0; i < YK_PTR(o)->array.size; i++) {
			yk_print(YK_PTR(o)->array.data[i]);

			if (i != YK_PTR(o)->array.size - 1)
				printf(" ");
		}
		printf("]");
		break;
	case yk_t_string:
		printf("\"");
		printf("%s\"", YK_PTR(o)->string.data);
		break;
	default:
		printf("Unknown type 0x%lx!\n", YK_TYPEOF(o));
	}
}

static inline void yk_exit_continuation(YkObject exit) {
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
}

static inline void yk_debug_info() {
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
}

#define YK_RUN_DEBUG 1

YkObject yk_run(YkObject bytecode) {
	YK_ASSERT(YK_BYTECODEP(bytecode));

	yk_program_counter = YK_PTR(bytecode)->bytecode.code;
	yk_bytecode_register = bytecode;

	YK_PUSH(yk_continuations_stack_top,
			yk_make_continuation(YK_PTR(bytecode)->bytecode.code_size - 1));

	if (setjmp(yk_jump_buf) != 0) {
		yk_exit_continuation(*(yk_continuations_stack + YK_STACK_MAX_SIZE - 1));
		goto end;
	}

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
			YkInt current_frame_size = yk_program_counter->modifier + YK_INT(yk_program_counter->ptr),
				last_frame_size = YK_INT(yk_lisp_stack_top[current_frame_size]);

			YkObject* last_top = yk_lisp_stack_top;
			yk_lisp_stack_top += last_frame_size + current_frame_size + 1;

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
			yk_lisp_stack_top += yk_program_counter->modifier + YK_INT(yk_program_counter->ptr);

			YkObject last_nargs; /* Only builtin functions return control in tail calls */
			YK_LISP_STACK_POP(last_nargs);
			yk_lisp_stack_top += YK_INT(last_nargs);

			YkReturnInfo info;
			YK_RET_POP(info);

			yk_program_counter = info.program_counter;
			yk_bytecode_register = info.bytecode_register;
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

			YkReturnInfo info;
			info.bytecode_register = yk_bytecode_register;
			info.program_counter = yk_program_counter + 1;
			info.stack_pointer = yk_lisp_stack_top;
			YK_RET_PUSH(info);

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
			YK_ASSERT(0);
		}
		break;
	case YK_OP_RET:
	{
		YkObject nargs;
		YK_LISP_STACK_POP(nargs);
		yk_lisp_stack_top += YK_INT(nargs);

		YkReturnInfo info;
		YK_RET_POP(info);

		yk_bytecode_register = info.bytecode_register;
		yk_program_counter = info.program_counter;
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
		yk_exit_continuation(exit);
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
	case YK_OP_LEXICAL_SET:
		yk_lisp_stack_top[yk_program_counter->modifier] = yk_value_register;
		yk_program_counter++;
		break;
	case YK_OP_GLOBAL_SET:
		YK_PTR(yk_program_counter->ptr)->symbol.value = yk_value_register;
		yk_program_counter++;
		break;
	case YK_OP_END:
		goto end;
		break;
	}

#if YK_RUN_DEBUG
	yk_debug_info();
#endif

	goto start;

end:
#if YK_RUN_DEBUG
	yk_debug_info();
#endif

	return yk_value_register;
}

char* yk_opcode_names[] = {
	[YK_OP_FETCH_LITERAL] = "fetch-literal",
	[YK_OP_FETCH_GLOBAL] = "fetch-global",
	[YK_OP_LEXICAL_VAR] = "lexical-var",
	[YK_OP_PUSH] = "push",
	[YK_OP_UNBIND] = "unbind",
	[YK_OP_CALL] = "call",
	[YK_OP_TAIL_CALL] = "tail-call",
	[YK_OP_RET] = "ret",
	[YK_OP_JMP] = "jmp",
	[YK_OP_JNIL] = "jnil",
	[YK_OP_BIND_DYNAMIC] = "bind-dynamic",
	[YK_OP_UNBIND_DYNAMIC] = "unbind-dynamic",
	[YK_OP_WITH_CONT] = "with-cont",
	[YK_OP_EXIT_CONT] = "exit-cont",
	[YK_OP_LEXICAL_SET] = "lexical-set",
	[YK_OP_GLOBAL_SET] = "global-set",
	[YK_OP_EXIT] = "exit",
	[YK_OP_END] = "end"
};

void yk_bytecode_disassemble(YkObject bytecode) {
	YK_ASSERT(YK_BYTECODEP(bytecode));

	printf("Bytecode ");
	yk_print(YK_PTR(bytecode)->bytecode.name);
	printf("\n");

	for (uint i = 0; i < YK_PTR(bytecode)->bytecode.code_size; i++) {
		YkInstruction instruction = YK_PTR(bytecode)->bytecode.code[i];
		printf("(%s ", yk_opcode_names[instruction.opcode]);

		if (instruction.opcode == YK_OP_TAIL_CALL) {
			printf(" ");
			yk_print(instruction.ptr);
			printf(" %u)\n", instruction.modifier);
		} else if (instruction.opcode == YK_OP_FETCH_LITERAL ||
				   instruction.opcode == YK_OP_FETCH_GLOBAL  ||
				   instruction.opcode == YK_OP_GLOBAL_SET    ||
				   instruction.opcode == YK_OP_BIND_DYNAMIC  ||
				   instruction.opcode == YK_OP_UNBIND_DYNAMIC)
		{
			yk_print(instruction.ptr);
			printf(")\n");
		} else {
			printf("%u)\n", instruction.modifier);
		}
	}
}

void yk_w_undeclared_var_init(YkWarning* warning, char* file, uint32_t line,
							  uint32_t character, YkObject symbol)
{
	warning->type = YK_W_UNDECLARED_VARIABLE;

	warning->file = file;
	warning->line = line;

	warning->character = character;
	warning->warning.undeclared_variable.symbol = symbol;
}

void yk_w_wrong_number_of_arguments_init(YkWarning* warning, char* file, uint32_t line,
										 uint32_t character, YkObject function_symbol,
										 YkInt expected_number, YkUint given_number)
{
	warning->type = YK_W_WRONG_NUMBER_OF_ARGUMENTS;
	warning->file = file;
	warning->line = line;
	warning->character = character;

	warning->warning.wrong_number_of_arguments.function_symbol = function_symbol;
	warning->warning.wrong_number_of_arguments.expected_number = expected_number;
	warning->warning.wrong_number_of_arguments.given_number = given_number;
}

void yk_w_assigning_to_function_init(YkWarning* warning, char* file, uint32_t line,
									 uint32_t character, YkObject function_symbol)
{
	warning->type = YK_W_WRONG_NUMBER_OF_ARGUMENTS;
	warning->file = file;
	warning->line = line;
	warning->character = character;

	warning->warning.assigning_to_function.function_symbol = function_symbol;
}

void yk_w_dynamic_bind_function_init(YkWarning* warning, char* file, uint32_t line,
									 uint32_t character, YkObject function_symbol)
{
	warning->type = YK_W_DYNAMIC_BIND_FUNCTION;
	warning->file = file;
	warning->line = line;
	warning->character = character;

	warning->warning.dynamic_bind_function.function_symbol = function_symbol;
}

void yk_w_print(YkWarning* w) {
	switch (w->type) {
	case YK_W_UNDECLARED_VARIABLE:
		printf("Undeclared variable: %s at %s:%d\n",
			   YK_PTR(w->warning.undeclared_variable.symbol)->symbol.name,
			   w->file, w->line);
		break;
	case YK_W_WRONG_NUMBER_OF_ARGUMENTS:
		printf("Wrong number of arguments for %s: expected %ld, got %ld at %s:%d\n",
			   YK_PTR(w->warning.wrong_number_of_arguments.function_symbol)->symbol.name,
			   w->warning.wrong_number_of_arguments.expected_number,
			   w->warning.wrong_number_of_arguments.given_number,
			   w->file, w->line);
		break;
	case YK_W_ASSIGNING_TO_FUNCTION:
		printf("Assignment to variable '%s' declared as a function at %s:%d\n",
			   YK_PTR(w->warning.assigning_to_function.function_symbol)->symbol.name,
			   w->file, w->line);
		break;
	case YK_W_DYNAMIC_BIND_FUNCTION:
		printf("Dynamic binding to the variable '%s' declared as a function at %s:%d\n",
			   YK_PTR(w->warning.assigning_to_function.function_symbol)->symbol.name,
			   w->file, w->line);
		break;
	}
}

void yk_w_remove_untrue(DynamicArray* warnings) {
	for (uint i = 0; i < warnings->size;) {
		YkWarning* w = dynamic_array_at(warnings, i);
		if (w->type == YK_W_UNDECLARED_VARIABLE &&
			YK_PTR(w->warning.undeclared_variable.symbol)->symbol.declared)
		{
			dynamic_array_remove(warnings, i);
		} else {
			i++;
		}
	}
}

YkInt yk_lexical_offset(YkObject symbol, YkObject lexical_stack) {
	YkInt j = 0;
	YkObject i;

	for (i = lexical_stack; i != YK_NIL; i = YK_CDR(i)) {
		YkObject sym = YK_CAR(i);

		if (sym == symbol) {
			break;
		}
		j++;
	}

	if (i != YK_NIL) {
		return j;
	} else {
		return -1;
	}
}

void yk_compile_loop(YkObject expression, YkObject bytecode, YkObject continuations_stack,
					 YkObject lexical_stack, YkUint stack_offset, bool is_tail, DynamicArray* warnings)
{
	YK_GC_PROTECT4(expression, bytecode, continuations_stack, lexical_stack);
	printf("Compiling ");
	yk_print(expression);
	printf(" with ");
	yk_print(lexical_stack);
	printf(" and %ld offset!\n", stack_offset);

	switch (YK_TYPEOF(expression)) {
	case yk_t_array:
	case yk_t_bytecode:
	case yk_t_c_proc:
	case yk_t_class:
	case yk_t_closure:
	case yk_t_float:
	case yk_t_instance:
	case yk_t_int:
	case yk_t_stream:
	case yk_t_string:
		yk_bytecode_emit(bytecode, YK_OP_FETCH_LITERAL, 0, expression);
		break;
	case yk_t_symbol:
	{
		int32_t j = yk_lexical_offset(expression, lexical_stack);

		if (j != -1) {
			yk_bytecode_emit(bytecode, YK_OP_LEXICAL_VAR, j + stack_offset, YK_NIL);
		}
		else {
			if (YK_PTR(expression)->symbol.type == yk_s_constant) {
				yk_bytecode_emit(bytecode, YK_OP_FETCH_LITERAL, 0, YK_PTR(expression)->symbol.value);
			} else {
				if (YK_PTR(expression)->symbol.value == NULL &&	!YK_PTR(expression)->symbol.declared) {
					YkWarning* warning = dynamic_array_push_back(warnings, 1);
					yk_w_undeclared_var_init(warning, "NONE", 0, 0, expression);
				}

				yk_bytecode_emit(bytecode, YK_OP_FETCH_GLOBAL, 0, expression);
			}
		}
	}
		break;
	case yk_t_list:
	{
		if (expression == YK_NIL) {
			yk_bytecode_emit(bytecode, YK_OP_FETCH_LITERAL, 0, YK_NIL);
			goto end;
		}

		YkObject first = YK_CAR(expression);

		if (first == yk_keyword_quote) {
			YK_ASSERT(yk_length(expression) == 2);
			yk_bytecode_emit(bytecode, YK_OP_FETCH_LITERAL, 0, YK_CAR(YK_CDR(expression)));
		} else if (first == yk_keyword_let) {
			YkObject bindings = YK_CAR(YK_CDR(expression));
			YkObject body = YK_CDR(YK_CDR(expression));

			YkUint offset = stack_offset;
			YkObject body_lexical_stack = lexical_stack;
			YK_GC_PROTECT1(body_lexical_stack);

			YK_LIST_FOREACH(bindings, l) {
				YkObject pair = YK_CAR(l);
				yk_compile_loop(YK_CAR(YK_CDR(pair)), bytecode, continuations_stack,
								lexical_stack, offset, false, warnings);
				yk_bytecode_emit(bytecode, YK_OP_PUSH, 0, YK_NIL);
				body_lexical_stack = yk_cons(YK_CAR(pair), body_lexical_stack);
				offset++;
			}

			YK_GC_UNPROTECT;

			YK_LIST_FOREACH(body, e) {
				bool compiled_is_tail = false;
				if (is_tail && !(YK_CONSP(YK_CDR(e))))
					compiled_is_tail = true;

				yk_compile_loop(YK_CAR(e), bytecode, continuations_stack, body_lexical_stack,
								stack_offset, compiled_is_tail, warnings);
			}

			yk_bytecode_emit(bytecode, YK_OP_UNBIND, offset, YK_NIL);
		} else if (first == yk_keyword_dynamic_let) {
			YkObject bindings = YK_CAR(YK_CDR(expression));
			YkObject body = YK_CDR(YK_CDR(expression));

			YkInt bindings_count = 0;

			YK_LIST_FOREACH(bindings, b) {
				YkObject pair = YK_CAR(b);
				yk_compile_loop(YK_CAR(YK_CDR(pair)), bytecode, continuations_stack,
								lexical_stack, stack_offset, false, warnings);
				yk_bytecode_emit(bytecode, YK_OP_BIND_DYNAMIC, 0, YK_CAR(pair));

				if (YK_PTR(YK_CAR(pair))->symbol.type == yk_s_function) {
					YkWarning* w = dynamic_array_push_back(warnings, 1);
					yk_w_dynamic_bind_function_init(w, "None", 0, 0, YK_CAR(pair));
				}

				bindings_count++;
			}

			YK_LIST_FOREACH(body, e) {
				yk_compile_loop(YK_CAR(e), bytecode, continuations_stack, lexical_stack,
								stack_offset, false, warnings);
			}

			yk_bytecode_emit(bytecode, YK_OP_UNBIND_DYNAMIC, bindings_count, YK_NIL);
		} else if (first == yk_keyword_setq) {
			YK_ASSERT(yk_length(expression) == 3);

			YkObject symbol = YK_CAR(YK_CDR(expression));
			YkObject value = YK_CAR(YK_CDR(YK_CDR(expression)));

			YkInt i = yk_lexical_offset(symbol, lexical_stack);

			yk_compile_loop(value, bytecode, continuations_stack,
								lexical_stack, stack_offset, false, warnings);

			if (i != -1) { 		/* Lexically scoped variable */
				YkUint offset = stack_offset + i;
				yk_bytecode_emit(bytecode, YK_OP_LEXICAL_SET, offset, YK_NIL);
			} else {			/* Global variable */
				YK_ASSERT(!(YK_PTR(symbol)->symbol.type == yk_s_macro ||
							YK_PTR(symbol)->symbol.type == yk_s_constant));

				if (!YK_PTR(symbol)->symbol.declared) {
					YkWarning* w = dynamic_array_push_back(warnings, 1);
					yk_w_undeclared_var_init(w, "None", 0, 0, symbol);
				} else if (YK_PTR(symbol)->symbol.type == yk_s_function) {
					YkWarning* w = dynamic_array_push_back(warnings, 1);
					yk_w_assigning_to_function_init(w, "None", 0, 0, symbol);
				}

				yk_bytecode_emit(bytecode, YK_OP_GLOBAL_SET, 0, symbol);
			}
		} else if (first == yk_keyword_comptime) {
			YkObject comptime_bytecode =
				yk_make_bytecode_begin(yk_make_symbol("compile-time-bytecode"), 0);

			YkObject comptime_exprs = YK_CDR(expression);

			YK_LIST_FOREACH(comptime_exprs, e) {
				yk_compile_loop(YK_CAR(e), comptime_bytecode,
								YK_NIL, YK_NIL, 0, false, warnings);
			}

			yk_bytecode_emit(comptime_bytecode, YK_OP_END, 0, YK_NIL);
			yk_bytecode_disassemble(comptime_bytecode);

			yk_run(comptime_bytecode);

			yk_bytecode_emit(bytecode, YK_OP_FETCH_LITERAL, 0, yk_value_register);
		} else if (first == yk_keyword_lambda) {
			YK_ASSERT(yk_length(expression) >= 4);
			YK_ASSERT(YK_SYMBOLP(YK_CAR(YK_CDR(expression))));

			YkObject name = YK_CAR(YK_CDR(expression));
			YkObject arglist = YK_CAR(YK_CDR(YK_CDR(expression)));
			YkObject body = YK_CDR(YK_CDR(YK_CDR(expression)));

			YkObject lambda_lexical_stack = YK_NIL,
				lambda_bytecode = YK_NIL;

			YK_GC_PROTECT2(lambda_lexical_stack, lambda_bytecode);

			YkObject l;
			YkInt argcount = 0;
			for (l = arglist; YK_CONSP(l); l = YK_CDR(l)) {
				YkObject arg = YK_CAR(l);
				YK_ASSERT(YK_SYMBOLP(arg));
				lambda_lexical_stack = yk_cons(arg, lambda_lexical_stack);
				argcount++;
			}

			if (l != YK_NIL && YK_SYMBOLP(l)) {
				argcount = -(argcount + 1);
			}

			lambda_lexical_stack = yk_cons(yk_symbol_argcount,
										   yk_nreverse(lambda_lexical_stack));
			lambda_bytecode = yk_make_bytecode_begin(name, argcount);
			if (body != YK_NIL && YK_TYPEOF(YK_CAR(body)) == yk_t_string) {
				YK_PTR(lambda_bytecode)->bytecode.docstring = YK_CAR(body);
				body = YK_CDR(body);
			}

			if (argcount < 0) {
				yk_bytecode_emit(lambda_bytecode, YK_OP_LEXICAL_VAR, 0, YK_NIL);
				yk_bytecode_emit(lambda_bytecode, YK_OP_PUSH, 0, YK_NIL);
				yk_bytecode_emit(lambda_bytecode, YK_OP_FETCH_LITERAL, 0, YK_MAKE_INT(-argcount - 1));
				yk_bytecode_emit(lambda_bytecode, YK_OP_PUSH, 0, YK_NIL);
				yk_bytecode_emit(lambda_bytecode, YK_OP_FETCH_LITERAL, 0, yk_arglist_cfun);
				yk_bytecode_emit(lambda_bytecode, YK_OP_CALL, 2, YK_NIL);
				yk_bytecode_emit(lambda_bytecode, YK_OP_PUSH, 0, YK_NIL);

				lambda_lexical_stack = yk_cons(l, lambda_lexical_stack);
			}

			YK_LIST_FOREACH(body, e) {
				yk_compile_loop(YK_CAR(e), lambda_bytecode,
								continuations_stack,
								lambda_lexical_stack,
								0,
								!YK_CONSP(YK_CDR(e)),
								warnings);
			}

			if (argcount < 0) {
				yk_bytecode_emit(lambda_bytecode, YK_OP_UNBIND, 1, YK_NIL);
			}

			yk_bytecode_emit(lambda_bytecode, YK_OP_RET, 0, YK_NIL);

			yk_bytecode_disassemble(lambda_bytecode);
			printf("===END===\n");

			yk_bytecode_emit(bytecode, YK_OP_FETCH_LITERAL, 0, lambda_bytecode);

			YK_GC_UNPROTECT;
	 	} else if (first == yk_keyword_do) {
			YK_LIST_FOREACH(YK_CDR(expression), l) {
				bool compiled_is_tail = false;
				if (is_tail && !YK_CONSP(YK_CDR(l)))
					compiled_is_tail = true;

				yk_compile_loop(YK_CAR(l), bytecode, continuations_stack, lexical_stack,
								stack_offset, compiled_is_tail, warnings);
			}
		} else if (first == yk_keyword_if) {
			yk_compile_loop(YK_CAR(YK_CDR(expression)), bytecode, continuations_stack,
							lexical_stack, stack_offset, false, warnings);
			YkUint branch_offset = YK_PTR(bytecode)->bytecode.code_size;
			yk_bytecode_emit(bytecode, YK_OP_JNIL, 69, YK_NIL);
			yk_compile_loop(YK_CAR(YK_CDR(YK_CDR(expression))), bytecode, continuations_stack,
							lexical_stack, stack_offset, is_tail, warnings);

			YkUint else_offset = YK_PTR(bytecode)->bytecode.code_size;
			YK_PTR(bytecode)->bytecode.code[branch_offset].modifier = else_offset + 1;
			yk_bytecode_emit(bytecode, YK_OP_JMP, 69, YK_NIL);

			yk_compile_loop(YK_CAR(YK_CDR(YK_CDR(YK_CDR(expression)))),
							bytecode, continuations_stack, lexical_stack,
							stack_offset, is_tail, warnings);

			YK_PTR(bytecode)->bytecode.code[else_offset].modifier =
				YK_PTR(bytecode)->bytecode.code_size;
		} else if (first == yk_keyword_with_cont) {
			YkObject cont_sym = YK_CAR(YK_CDR(expression));
			YkObject cont_body = YK_CDR(YK_CDR(expression));

			YkObject new_continuations_stack = yk_cons(cont_sym, continuations_stack);

			uint before_size = YK_PTR(bytecode)->bytecode.code_size;
			yk_bytecode_emit(bytecode, YK_OP_WITH_CONT, 0, YK_NIL);

			YK_LIST_FOREACH(cont_body, b) {
				yk_compile_loop(YK_CAR(b), bytecode, new_continuations_stack,
								lexical_stack, stack_offset, false, warnings);
			}

			uint after_size = YK_PTR(bytecode)->bytecode.code_size;
			YK_PTR(bytecode)->bytecode.code[before_size].modifier = after_size + 1;
			yk_bytecode_emit(bytecode, YK_OP_EXIT, 0, YK_NIL);
		} else if (first == yk_keyword_exit) {
			YK_ASSERT(yk_length(expression) == 3);

			YkObject symbol = YK_CAR(YK_CDR(expression));
			YkObject value_body = YK_CAR(YK_CDR(YK_CDR(expression)));

			YkInt cont_offset = yk_lexical_offset(symbol, continuations_stack);
			YK_ASSERT(cont_offset >= 0);

			yk_compile_loop(value_body, bytecode, continuations_stack,
							lexical_stack, stack_offset, false, warnings);

			yk_bytecode_emit(bytecode, YK_OP_EXIT_CONT, cont_offset, YK_NIL);
		} else if (first == yk_keyword_loop) {
			YkUint begin_size = YK_PTR(bytecode)->bytecode.code_size;
			YkObject body = YK_CDR(expression);

			YK_LIST_FOREACH(body, b) {
				yk_compile_loop(YK_CAR(b), bytecode, continuations_stack,
								lexical_stack, stack_offset, false, warnings);
			}

			yk_bytecode_emit(bytecode, YK_OP_JMP, begin_size, YK_NIL);
		} else {
			YkUint argcount = 0;
			YkUint offset = stack_offset;

			if (YK_SYMBOLP(YK_CAR(expression)) &&
				YK_PTR(YK_CAR(expression))->symbol.type == yk_s_macro)
			{
				YkObject macro_return =
					yk_apply(YK_PTR(YK_CAR(expression))->symbol.value,
						 YK_CDR(expression));

				yk_compile_loop(macro_return, bytecode, continuations_stack,
								lexical_stack, stack_offset, is_tail, warnings);
				goto end;
			}

			YkObject arguments = yk_reverse(YK_CDR(expression));
			YK_GC_PROTECT1(arguments);
			YK_LIST_FOREACH(arguments, e) {
				yk_compile_loop(YK_CAR(e), bytecode, continuations_stack,
								lexical_stack, offset, false, warnings);
				yk_bytecode_emit(bytecode, YK_OP_PUSH, 0, YK_NIL);
				argcount++;
				offset++;
			}

			YK_GC_UNPROTECT;

			if (YK_SYMBOLP(YK_CAR(expression))) {
				YkObject sym = YK_CAR(expression);
				if (YK_PTR(sym)->symbol.type == yk_s_function) {
					YkInt function_nargs = YK_PTR(sym)->symbol.function_nargs;

					if (function_nargs < 0) {
						if ((YkInt)argcount < -function_nargs) {
							YkWarning* warning = dynamic_array_push_back(warnings, 1);
							yk_w_wrong_number_of_arguments_init(warning, "NONE", 0, 0, sym, function_nargs, argcount);
						}
					} else if (function_nargs != (YkInt)argcount) {
						YkWarning* warning = dynamic_array_push_back(warnings, 1);
						yk_w_wrong_number_of_arguments_init(warning, "NONE", 0, 0, sym, function_nargs, argcount);
					}
				}
			}

			yk_compile_loop(YK_CAR(expression), bytecode, continuations_stack,
							lexical_stack, offset, false, warnings);

			if (is_tail) {
				printf("Compiling tail call with ");
				yk_print(lexical_stack);
				printf("!\n");

				YkUint frame_size = 0;
				bool found = false;
				YK_LIST_FOREACH(lexical_stack, s) {
					if (YK_CAR(s) == yk_symbol_argcount) {
						found = true;
						break;
					}

					frame_size++;
				}

				YK_ASSERT(found);

				yk_bytecode_emit(bytecode, YK_OP_TAIL_CALL,
								 argcount, YK_MAKE_INT(frame_size));
			} else {
				yk_bytecode_emit(bytecode, YK_OP_CALL, argcount, YK_NIL);
			}
		}
	}
		break;
	}

end:
	YK_GC_UNPROTECT;
}

void yk_compile(YkObject forms, YkObject bytecode) {
	YK_ASSERT(YK_BYTECODEP(bytecode));

	DynamicArray warnings;
	DYNAMIC_ARRAY_CREATE(&warnings, YkWarning);

	yk_compile_loop(forms, bytecode, YK_NIL, YK_NIL, 0, false, &warnings);
	yk_bytecode_emit(bytecode, YK_OP_END, 0, YK_NIL);

	yk_bytecode_disassemble(bytecode);

	yk_w_remove_untrue(&warnings);
	if (warnings.size != 0) {
		printf("======WARNINGS====\n");

		for (uint i = 0; i < warnings.size; i++) {
			yk_w_print(dynamic_array_at(&warnings, i));
		}
	}
}

void yk_repl() {
	char buffer[2048];
	YkObject forms = YK_NIL, bytecode = YK_NIL;

	YK_GC_PROTECT2(forms, bytecode);

	do {
		printf("\n> ");
		fgets(buffer, sizeof(buffer), stdin);

		forms = yk_read(buffer);

		bytecode = yk_make_bytecode_begin(yk_make_symbol("toplevel"), 0);
		yk_compile(forms, bytecode);

		yk_print(yk_run(bytecode));
		printf("\n");
	} while (strcmp(buffer, "bye\n") != 0);

	YK_GC_UNPROTECT;
}
