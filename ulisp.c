#include "ulisp.h"

#include <assert.h>
#include <string.h>

#define IS_BLANK(x) ((x) == ' ' || (x) == '\n')

LispObject *environnement, *nil, *tee, *quote, *iffe, *begin;

LispObject* ulisp_cons(LispObject* first, LispObject* second) {
	LispObject* new_object = malloc(sizeof(LispObject) + 2 * sizeof(LispObject*));
	new_object->type = LISP_CONS;
	((ConsCell*)new_object->data)->car = first;
	((ConsCell*)new_object->data)->cdr = second;

	return new_object;
}

LispObject* ulisp_car(LispObject* cons_obj) {
	assert(cons_obj->type == LISP_CONS);

	return ((ConsCell*)cons_obj->data)->car;
}

LispObject* ulisp_cdr(LispObject* cons_obj) {
	assert(cons_obj->type == LISP_CONS);

	return ((ConsCell*)cons_obj->data)->cdr;
}

LispObject* ulisp_make_symbol(const char* string) {
	LispObject* new_object = malloc(sizeof(LispObject) + sizeof(char*));
	new_object->type = LISP_SYMBOL;

	char **data = (char**)&new_object->data;
	*data = strdup(string);

	return new_object;
}

BOOL ulisp_eq(LispObject* obj1, LispObject* obj2) {
	assert(obj1->type == LISP_SYMBOL);
	assert(obj2->type == LISP_SYMBOL);

	if (strcmp(*(char**)obj1->data, *(char**)obj2->data) == 0)
		return GL_TRUE;
	else
		return GL_FALSE;
}

LispObject* ulisp_assoc(LispObject* plist, LispObject* symbol) {
	assert(plist->type == LISP_CONS);
	assert(symbol->type == LISP_SYMBOL);

	for (; plist != nil; plist = ulisp_cdr(plist)) {
		LispObject* temp = ulisp_car(plist);

		if (ulisp_eq(ulisp_car(temp), symbol))
			return ulisp_cdr(temp);
	}

	return nil;
}

LispObject* ulisp_nreverse(LispObject* obj) {
	LispObject *current = obj,
		*next, *previous = nil,
		*last = nil;

	while (current != nil) {
		ConsCell* cell = (ConsCell*)current->data;
		next = cell->cdr;
		cell->cdr = previous;
		previous = current;
		last = current;
		current = next;
	}

	return last;
}

LispObject* ulisp_builtin_proc(void* function) {
	LispObject* proc = malloc(sizeof(LispObject) + sizeof(void*));
	proc->type = LISP_PROC_BUILTIN;
	*(void**)proc->data = function;

	return proc;
}

LispObject* ulisp_prim_car(LispObject* args) {
	LispObject* first = ulisp_car(args);

	return ulisp_car(first);
}

LispObject* ulisp_prim_cdr(LispObject* args) {
	LispObject* first = ulisp_car(args);

	return ulisp_cdr(first);
}

void ulisp_init(void) {
	nil = ulisp_make_symbol("nil");
	tee = ulisp_make_symbol("t");
	quote = ulisp_make_symbol("quote");
	iffe = ulisp_make_symbol("if");
	begin = ulisp_make_symbol("begin");

	environnement = ulisp_cons(ulisp_cons(nil, nil),
							   ulisp_cons(ulisp_cons(tee, tee),
										  nil));

	LispObject* car = ulisp_builtin_proc(ulisp_prim_car);
	LispObject* cdr = ulisp_builtin_proc(ulisp_prim_cdr);

	// ((car . <PROC>) . ((cdr . <PROC>) . NIL))
	environnement = ulisp_cons(ulisp_cons(ulisp_make_symbol("car"), car),
							   ulisp_cons(ulisp_cons(ulisp_make_symbol("cdr"), cdr),
										  environnement));
}

LispObject* ulisp_apply(LispObject* proc, LispObject* arguments) {
	assert(proc->type == LISP_PROC || proc->type == LISP_PROC_BUILTIN);
	assert(arguments->type == LISP_CONS);

	if (proc->type == LISP_PROC_BUILTIN) {
		LispObject* (**function)(LispObject*) = (LispObject *(**)(LispObject *))&proc->data;
		return (*function)(arguments);
	}
	else {
		assert(0);
	}
}

LispObject* ulisp_eval(LispObject* expression) {
	if (expression->type == LISP_SYMBOL) {
		return ulisp_assoc(environnement, expression);
	}
	else {
		LispObject* applied_symbol = ulisp_car(expression);
		if (ulisp_eq(applied_symbol, quote)) {
			return ulisp_car(ulisp_cdr(expression));
		}
		else if (ulisp_eq(applied_symbol, iffe)) {
			LispObject* arguments = ulisp_cdr(expression);

			if (ulisp_eval(ulisp_car(arguments)) != nil)
				return ulisp_eval(ulisp_car(ulisp_cdr(arguments)));
			else if (ulisp_cdr(ulisp_cdr(arguments)) != nil)
				return ulisp_eval(ulisp_car(ulisp_cdr(ulisp_cdr(arguments))));
			else
				return nil;
		}
		else if (ulisp_eq(applied_symbol, begin))  {
			ulisp_eval(ulisp_car(ulisp_cdr(expression)));
		}

		LispObject* proc = ulisp_eval(applied_symbol);

		for(LispObject* argument = ulisp_cdr(expression); argument != nil; argument = ulisp_cdr(argument))
			((ConsCell*)argument->data)->car = ulisp_eval(ulisp_car(argument));

		return ulisp_apply(proc, ulisp_cdr(expression));
	}
}

LispObject* ulisp_read_list(const char* string) {
	LispObject* list = nil;
	uint i = 0;

	while (string[i] != '\0') {
		for (; IS_BLANK(string[i]); i++) { /* Skip all blank chars */
			if (string[i] == '\0')
				break;
		}

		if (string[i] == '(') {
			int parens_count = 0;
			uint j = i, count = 0;

			do {
				if (string[j] == '(')
					parens_count++;
				else if (string[j] == ')')
					parens_count--;

				assert(string[j] != '\0');

				count++;
				assert(parens_count >= 0);
			} while(!(string[j++] == ')' && parens_count == 0));

			list = ulisp_cons(ulisp_read_list(strndup(string + i + 1, count - 2)), list);
			i += count;
		}
		else {
			uint count = 0;
			for (uint j = i; !IS_BLANK(string[j]); j++) {
				assert(string[j] != ')');

				if (string[j] == '\0')
					break;
				count++;
			}

			if (count == 0)
				break;

			list = ulisp_cons(ulisp_make_symbol(strndup(string + i, count)), list);
			i += count;
		}
	}

	return ulisp_nreverse(list);
}

void ulisp_print(LispObject* obj, FILE* stream) {
	if (obj->type == LISP_SYMBOL) {
		fprintf(stream, "%s", *((char**)obj->data));
	}
	else if (obj->type == LISP_CONS) {
		ConsCell* cell = (ConsCell*)obj->data;
		printf("(");
		ulisp_print(cell->car, stream);
		printf(" . ");
		ulisp_print(cell->cdr, stream);
		printf(")");
	}
	else if (obj->type == LISP_PROC_BUILTIN || obj->type == LISP_PROC) {
		printf("<PROC>");
	}
}
