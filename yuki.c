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

static YkObject yk_alloc() {
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
}

static void yk_free(YkObject o) {
	YK_PTR(o)->cons.car = yk_free_list;
	yk_free_list = YK_PTR(o);

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

void yk_gc() {
	void* dummy = (void*)0x69;

	{
		void** stack_start = &dummy;
		size_t stack_size = stack_end - stack_start;

		for (size_t i = 0; i < stack_size; i++) {
			yk_mark(stack_start[i]);
		}
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

			sym = YK_TAG_SYMBOL(sym);
			s->symbol.next_sym = sym;

			return sym;
		}
	}
}

YkObject yk_cons(YkObject car, YkObject cdr) {
	YkObject o = yk_alloc();
	o->cons.car = car;
	o->cons.cdr = cdr;
	return YK_TAG_LIST(o);
}

#define IS_BLANK(x) ((x) == ' ' || (x) == '\n' || (x) == '\t')

YkObject yk_read_list(const char* string) {
	YkObject list = YK_NIL;
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

void yk_repl() {
	char buffer[2048];

	do {
		printf("\n> ");
		fgets(buffer, sizeof(buffer), stdin);

		YkObject forms = yk_read(buffer);

		yk_gc();

		yk_print(forms);
		printf("\n");
	} while (strcmp(buffer, "bye\n") != 0);
}
