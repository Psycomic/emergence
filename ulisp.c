#include "ulisp.h"

#include <assert.h>
#include <string.h>

#define IS_BLANK(x) ((x) == ' ' || (x) == '\n')
#define DEBUG(m,e) printf("%s:%d: %s:",__FILE__,__LINE__,m); ulisp_print(e, stdout); puts("");

LispObject *environnement, *nil, *tee, *quote, *iffe, *begin, *lambda, *define;

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
	*data = m_strdup(string);

	return new_object;
}

LispObject* ulisp_make_lambda(LispObject* args, LispObject* body) {
	LispObject* new_lambda = malloc(sizeof(LispObject) + sizeof(LispProc));
	new_lambda->type = LISP_PROC;

	LispProc* proc = (LispProc*)new_lambda->data;

	proc->arguments = args;
	proc->expression = body;

	return new_lambda;
}

BOOL ulisp_eq(LispObject* obj1, LispObject* obj2) {
	if (obj1->type != LISP_SYMBOL || obj2->type != LISP_SYMBOL)
		return GL_FALSE;

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

	printf("Not found: %s\n", *(char**)symbol->data);
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

LispObject* ulisp_prim_cons(LispObject* args) {
	LispObject *first = ulisp_car(args),
		*second = ulisp_car(ulisp_cdr(args));

	return ulisp_cons(first, second);
}

LispObject* ulisp_prim_eq(LispObject* args) {
	if (ulisp_eq(ulisp_car(args), ulisp_car(ulisp_cdr(args))))
		return tee;
	else
		return nil;
}

void ulisp_init(void) {
	nil = ulisp_make_symbol("nil");
	tee = ulisp_make_symbol("t");
	quote = ulisp_make_symbol("quote");
	iffe = ulisp_make_symbol("if");
	begin = ulisp_make_symbol("begin");
	lambda = ulisp_make_symbol("lambda");
	define = ulisp_make_symbol("define");

	environnement = ulisp_cons(ulisp_cons(nil, nil),
							   ulisp_cons(ulisp_cons(tee, tee),
										  nil));

	LispObject* car = ulisp_builtin_proc(ulisp_prim_car);
	LispObject* cdr = ulisp_builtin_proc(ulisp_prim_cdr);
	LispObject* cons = ulisp_builtin_proc(ulisp_prim_cons);
	LispObject* eq = ulisp_builtin_proc(ulisp_prim_eq);

	// ((car . <PROC>) . ((cdr . <PROC>) . NIL))
	environnement = ulisp_cons(ulisp_cons(ulisp_make_symbol("car"), car),
							   ulisp_cons(ulisp_cons(ulisp_make_symbol("cdr"), cdr),
										  ulisp_cons(ulisp_cons(ulisp_make_symbol("cons"), cons),
													 ulisp_cons(ulisp_cons(ulisp_make_symbol("eq?"), eq),
																environnement))));
}

LispObject* ulisp_apply(LispObject* proc, LispObject* env, LispObject* arguments) {
	assert(proc->type == LISP_PROC || proc->type == LISP_PROC_BUILTIN);
	assert(arguments->type == LISP_CONS || arguments == nil);

	if (proc->type == LISP_PROC_BUILTIN) {
		LispObject* (**function)(LispObject*) = (LispObject *(**)(LispObject *))&proc->data;
		return (*function)(arguments);
	}
	else if (proc->type == LISP_PROC) {
		LispProc* lisp_proc = (LispProc*)proc->data;
		LispObject* new_env = env;

		LispObject *applied_arg = arguments,
			*arg_name = lisp_proc->arguments;

		while (applied_arg != nil && arg_name != nil) {
			new_env = ulisp_cons(ulisp_cons(ulisp_car(arg_name), ulisp_car(applied_arg)), new_env);

			applied_arg = ulisp_cdr(applied_arg);
			arg_name = ulisp_cdr(arg_name);
		}

		return ulisp_eval(ulisp_cons(begin, lisp_proc->expression), new_env);
	}

	return NULL;
}

LispObject* ulisp_eval(LispObject* expression, LispObject* env) {
	if (expression->type == LISP_SYMBOL) {
		return ulisp_assoc(env, expression);
	}
	else {
		LispObject* applied_symbol = ulisp_car(expression);

		if (applied_symbol->type == LISP_SYMBOL) {
			if (ulisp_eq(applied_symbol, quote)) {
				return ulisp_car(ulisp_cdr(expression));
			}
			else if (ulisp_eq(applied_symbol, iffe)) {
				LispObject* arguments = ulisp_cdr(expression);

				if (!ulisp_eq(ulisp_eval(ulisp_car(arguments), env), nil))
					return ulisp_eval(ulisp_car(ulisp_cdr(arguments)), env);
				else if (!ulisp_eq(ulisp_cdr(ulisp_cdr(arguments)), nil))
					return ulisp_eval(ulisp_car(ulisp_cdr(ulisp_cdr(arguments))), env);
				else
					return nil;
			}
			else if (ulisp_eq(applied_symbol, begin))  {
				LispObject* last_obj = nil;

				for(LispObject* form = ulisp_cdr(expression); form != nil; form = ulisp_cdr(form))
					last_obj = ulisp_eval(ulisp_car(form), env);

				return last_obj;
			}
			else if (ulisp_eq(applied_symbol, lambda)) {
				return ulisp_make_lambda(ulisp_car(ulisp_cdr(expression)),
										 ulisp_cdr(ulisp_cdr(expression)));
			}
			else if (ulisp_eq(applied_symbol, define)) {
				LispObject* name = ulisp_car(ulisp_cdr(expression));
				LispObject* value = ulisp_eval(ulisp_car(ulisp_cdr(ulisp_cdr(expression))), env);

				LispObject* pair = ulisp_cons(name, value);

				environnement = ulisp_cons(pair, environnement);
				env = ulisp_cons(pair, env);

				return name;
			}
		}

		LispObject* proc = ulisp_eval(applied_symbol, env);
		LispObject* evaluated_args = nil;

		for(LispObject* argument = ulisp_cdr(expression); argument != nil; argument = ulisp_cdr(argument))
			evaluated_args = ulisp_cons(ulisp_eval(ulisp_car(argument), env), evaluated_args);

		return ulisp_apply(proc, env, ulisp_nreverse(evaluated_args));
	}
}

LispObject* ulisp_read_list_helper(const char* string) {
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

			list = ulisp_cons(ulisp_read_list_helper(m_strndup(string + i + 1, count - 2)), list);
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

			list = ulisp_cons(ulisp_make_symbol(m_strndup(string + i, count)), list);
			i += count;
		}
	}

	return ulisp_nreverse(list);
}

LispObject* ulisp_read_list(const char* string) {
	char* real_string = malloc(7 + strlen(string));
	strcpy(real_string, "begin ");
	strcpy(real_string + 6, string);

	LispObject* expression = ulisp_read_list_helper(real_string);
	free(real_string);

	return expression;
}

void ulisp_print(LispObject* obj, FILE* stream) {
	if (obj->type == LISP_SYMBOL) {
		fprintf(stream, "%s", *((char**)obj->data));
	}
	else if (obj->type == LISP_CONS) {
		printf("(");

		LispObject* c = obj;
		for(; c->type == LISP_CONS; c = ulisp_cdr(c)) {
			ulisp_print(ulisp_car(c), stream);
			printf(" ");
		}

		if (!ulisp_eq(c, nil)) {
			printf(" . ");
			ulisp_print(c, stream);
		}

		printf(")");
	}
	else if (obj->type == LISP_PROC_BUILTIN) {
		printf("<BUILTIN-PROC>");
	}
	else if (obj->type == LISP_PROC) {
		LispProc* proc = (LispProc*)obj->data;

		printf("<PROC args: ");
		ulisp_print(proc->arguments, stdout);
		printf(", body: ");
		ulisp_print(proc->expression, stdout);
		printf(">");
	}
}
