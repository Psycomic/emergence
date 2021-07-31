#include "yuki.h"

#include <stdio.h>
#include <string.h>

#define YK_MARK(x) ((x) | 1)
#define YK_MARKED(x) ((x) & 1)

static union YkUnion* yk_free_list;
static YkUint yk_free_list_size;
static YkUint yk_free_space;

#define YK_SYMBOL_TABLE_SIZE 4096
static YkObject *yk_symbol_table;

static void yk_allocator_init() {
	yk_free_list_size = 2048;
	yk_free_list = malloc(sizeof(union YkUnion) * yk_free_list_size);

	if ((uint64_t)yk_free_list % 8 != 0) {
		uint align_pad = 8 - (uint64_t)yk_free_list % 8;
		yk_free_list = (union YkUnion*)((char*)yk_free_list + align_pad);
		yk_free_list_size--;
	}

	for (uint i = 0; i < yk_free_list_size; i++) {
		if (i != yk_free_list_size - 1)
			yk_free_list[i].cons.car = yk_free_list + i + 1;
		else
			yk_free_list[i].cons.car = YK_NIL;
	}

	yk_free_space = yk_free_list_size;
}

static YkObject yk_alloc() {
	YkObject first = yk_free_list;
	yk_free_list = first->cons.car;
	yk_free_space--;

	return first;
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

			list = yk_cons(yk_read_list(m_strndup(string + j + 2, count - 2)), list);
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

void yk_print(YkObject o) {
	if (YK_NULL(o)) {
		printf("nil");
	}
	else if (YK_CONSP(o)) {
		YkObject c;
		printf("(");

		for (c = o; YK_CONSP(c); c = YK_CDR(c)) {
			yk_print(YK_CAR(c));

			if (YK_CONSP(YK_CDR(c)))
				printf(" ");
		}

		if (!YK_NULL(c)) {
			printf(" . ");
			yk_print(YK_CDR(c));
		}

		printf(")");
	}
	else if (YK_INTP(o)) {
		printf("%ld", YK_INT(o));
	}
	else if (YK_FLOATP(o)) {
		printf("%f", YK_FLOAT(o));
	}
	else if (YK_SYMBOLP(o)) {
		printf("%s", YK_PTR(o)->symbol.name);
	}
	else {
		printf("Unknown type %ld!\n", YK_TYPEOF(o));
	}
}
