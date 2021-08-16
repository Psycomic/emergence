#include "yuki.h"

#include <stdio.h>
#include <string.h>

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

/* Stacks */
#define YK_GC_STACK_MAX_SIZE 1024
static YkObject *yk_gc_stack[YK_GC_STACK_MAX_SIZE];
static YkUint yk_gc_stack_size;

#define YK_LISP_STACK_MAX_SIZE 1024
static YkObject yk_lisp_stack[YK_LISP_STACK_MAX_SIZE];
static YkObject* yk_lisp_stack_top;

#define YK_LISP_STACK_MAX_SIZE 1024
static YkInstruction* yk_return_stack[YK_LISP_STACK_MAX_SIZE];
static YkInstruction** yk_return_stack_top;

/* Stack operations */
#define YK_GC_UNPROTECT yk_gc_stack_size = _yk_local_stack_ptr

#define YK_GC_PROTECT1(x) YkUint _yk_local_stack_ptr = yk_gc_stack_size; \
		yk_gc_stack[yk_gc_stack_size++] = &(x)

#define YK_GC_PROTECT2(x, y) YkUint _yk_local_stack_ptr = yk_gc_stack_size; \
	yk_gc_stack[yk_gc_stack_size++] = &(x);								\
	yk_gc_stack[yk_gc_stack_size++] = &(y);

static void yk_gc();

static void yk_allocator_init() {
	yk_workspace = malloc(sizeof(union YkUnion) * YK_WORKSPACE_SIZE);
    yk_workspace_size= YK_WORKSPACE_SIZE;
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
	for (size_t i = 0; i < YK_SYMBOL_TABLE_SIZE; i++) {
		yk_mark(yk_symbol_table[i]);
	}

	for (size_t i = 0; i < yk_gc_stack_size; i++) {
		yk_mark(*yk_gc_stack[i]);
	}

	yk_sweep();
}

static void yk_symbol_table_init() {
	yk_symbol_table = malloc(sizeof(YkObject) * YK_SYMBOL_TABLE_SIZE);

	for (uint i = 0; i < YK_SYMBOL_TABLE_SIZE; i++) {
		yk_symbol_table[i] = NULL;
	}
}

void yk_init() {
	yk_gc_stack_size = 0;
	yk_lisp_stack_top = yk_lisp_stack;
	yk_return_stack_top = yk_return_stack;

	yk_allocator_init();
	yk_symbol_table_init();
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

YkObject yk_cons(YkObject car, YkObject cdr) {
	YK_GC_PROTECT2(car, cdr);

	YkObject o = yk_alloc();
	o->cons.car = car;
	o->cons.cdr = cdr;

	YK_GC_UNPROTECT;
	return YK_TAG_LIST(o);
}

#define IS_BLANK(x) ((x) == ' ' || (x) == '\n' || (x) == '\t')

YkObject yk_read_list(const char* string) {
	YkObject list = YK_NIL;
	YK_GC_PROTECT1(list);

	size_t string_size = strlen(string);

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

			char* dupped = m_strndup(string + j + 2, count - 2);
			list = yk_cons(yk_read_list(dupped), list);
			free(dupped);
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
	YkObject r = yk_read_list(string);
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
		printf("%ld", YK_INT(o));
		break;
	case yk_t_float:
		printf("%f", YK_FLOAT(o));
		break;
	case yk_t_symbol:
		printf("%s", YK_PTR(o)->symbol.name);
		break;
	case yk_t_bytecode:
		printf("<bytecode at %p>", YK_PTR(o));
		break;
	case yk_t_closure:
		printf("<closure at %p>", YK_PTR(o));
		break;
	case yk_t_c_proc:
		printf("<compiled-function at %p>", YK_PTR(o));
		break;
	default:
		printf("Unknown type 0x%lx!\n", YK_TYPEOF(o));
	}
}

YkObject yk_run(YkObject bytecode) {
	YK_ASSERT(YK_BYTECODEP(bytecode));

	yk_program_counter = bytecode->bytecode.code;

start:
	switch (yk_program_counter->opcode) {
	case YK_OP_FETCH_LITERAL:
		yk_value_register = yk_program_counter->ptr;
		yk_program_counter++;
		break;
	case YK_OP_FETCH_GLOBAL:
		yk_value_register = yk_program_counter->ptr->symbol.value;
		yk_program_counter++;
		break;
	case YK_OP_LEXICAL_VAR:
		yk_value_register = *(yk_lisp_stack_top - 1 - yk_program_counter->modifier);
		yk_program_counter++;
		break;
	case YK_OP_PUSH:
		*(yk_lisp_stack_top++) = yk_value_register;
		yk_program_counter++;
		break;
	case YK_OP_UNBIND:
		yk_lisp_stack_top -= yk_program_counter->modifier;
		yk_program_counter++;
		break;
	case YK_OP_CALL:
		if (YK_CLOSUREP(yk_value_register)) {
			panic("Not implemented!\n");
		}
		else if (YK_BYTECODEP(yk_value_register)) {
			*(yk_lisp_stack_top++) = YK_MAKE_INT(yk_program_counter->modifier);
			*(yk_return_stack_top++) = yk_program_counter + 1;
			yk_program_counter = yk_value_register->bytecode.code;
		}
		else if (YK_CPROCP(yk_value_register)) {
			yk_value_register = yk_value_register->c_proc.cfun(yk_program_counter->modifier);
			yk_program_counter++;
		}
		break;
	case YK_OP_RET:
	{
		YkObject nargs = *(yk_lisp_stack_top--);
		yk_lisp_stack_top -= YK_INT(nargs);
		yk_program_counter = *(yk_return_stack_top--);
	}
		break;
	case YK_OP_END:
		goto end;
		break;
	}

	goto start;

end:
	return yk_value_register;
}

void yk_repl() {
	char buffer[2048];
	YkObject forms = YK_NIL;

	YK_GC_PROTECT1(forms);

	do {
		printf("\n> ");
		fgets(buffer, sizeof(buffer), stdin);

		forms = yk_read(buffer);

		yk_print(forms);
		printf("\n");
	} while (strcmp(buffer, "bye\n") != 0);

	YK_GC_UNPROTECT;
}
