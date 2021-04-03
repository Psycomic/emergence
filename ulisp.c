#include "ulisp.h"

#include <assert.h>
#include <string.h>

#define IS_BLANK(x) ((x) == ' ' || (x) == '\n' || (x) == '\t')
#define DEBUG(m,e) printf("%s:%d: %s:",__FILE__,__LINE__,m); ulisp_print(e, stdout); puts("");

LispObject *environnement, *nil, *tee, *quote, *iffe, *begin,
	*lambda, *mlambda, *define, *rest, *not_found;

LispObject* ulisp_cons(LispObject* first, LispObject* second) {
	LispObject* new_object = malloc(sizeof(LispObject) + 2 * sizeof(LispObject*));
	new_object->type = LISP_CONS | LISP_LIST;

	((ConsCell*)new_object->data)->car = first;
	((ConsCell*)new_object->data)->cdr = second;

	return new_object;
}

LispObject* ulisp_car(LispObject* cons_obj) {
	assert(cons_obj->type & LISP_CONS);

	return ((ConsCell*)cons_obj->data)->car;
}

LispObject* ulisp_cdr(LispObject* cons_obj) {
	assert(cons_obj->type & LISP_CONS);

	return ((ConsCell*)cons_obj->data)->cdr;
}

LispObject* ulisp_make_symbol(const char* string) {
	int string_length = strlen(string);

	LispObject* new_object = malloc(sizeof(LispObject) + sizeof(LispSymbol) + sizeof(char) * string_length + 1);

	new_object->type = LISP_SYMBOL;

	LispSymbol* symbol = (LispSymbol*)new_object->data;
	strcpy(symbol->str, string);

	symbol->hash = hash((uchar*)string);

	return new_object;
}

char* ulisp_symbol_string(LispObject* symbol) {
	assert(symbol->type & LISP_SYMBOL);

	return ((LispSymbol*)symbol->data)->str;
}

LispObject* ulisp_make_integer(long val) {
	LispObject* new_object = malloc(sizeof(LispObject) + sizeof(long));

	new_object->type = LISP_INTEGER | LISP_NUMBER;
	*(long*)new_object->data = val;

	return new_object;
}

LispObject* ulisp_make_float(double val) {
	LispObject* new_object = malloc(sizeof(LispObject) + sizeof(double));

	new_object->type = LISP_FLOAT | LISP_NUMBER;
	*(double*)new_object->data = val;

	return new_object;
}

LispObject* ulisp_make_lambda(LispObject* args, LispObject* body, GLboolean is_macro) {
	LispObject* new_lambda = malloc(sizeof(LispObject) + sizeof(LispProc));
	new_lambda->type = LISP_PROC;

	LispProc* proc = (LispProc*)new_lambda->data;

	proc->arguments = args;
	proc->expression = body;
	proc->is_macro = is_macro;

	return new_lambda;
}

BOOL ulisp_eq(LispObject* obj1, LispObject* obj2) {
	if (obj1 == obj2)
		return GL_TRUE;

	if (!(obj1->type & LISP_SYMBOL) ||
		!(obj2->type & LISP_SYMBOL))
		return GL_FALSE;

	uint hash1 = ((LispSymbol*)obj1->data)->hash,
		hash2 = ((LispSymbol*)obj2->data)->hash;

	if (hash1 == hash2)
		return GL_TRUE;
	else
		return GL_FALSE;
}

LispObject* ulisp_assoc(LispObject* plist, LispObject* symbol) {
	assert(plist->type & LISP_LIST);

	for (; plist != nil; plist = ulisp_cdr(plist)) {
		LispObject* temp = ulisp_car(plist);

		if (ulisp_eq(ulisp_car(temp), symbol))
			return ulisp_cdr(temp);
	}

	return not_found;
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

uint ulisp_length(LispObject* list) {
	uint length = 0;

	for(LispObject* i = list; i != nil; i = ulisp_cdr(i))
		length++;

	return length;
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

LispObject* ulisp_prim_plus(LispObject* args) {
	long sum = 0;
	double fsum = 0.0;
	GLboolean is_floating = GL_FALSE;

	for(LispObject* i = args; i != nil; i = ulisp_cdr(i)) {
		LispObject* number = ulisp_car(i);
		assert(number->type & LISP_NUMBER);

		if (number->type & LISP_FLOAT) {
			if (!is_floating) {
				is_floating = GL_TRUE;
				fsum += sum;
			}

			fsum += *(double*)number->data;
		}
		else {
			if (is_floating)
				fsum += *(long*)number->data;
			else
				sum += *(long*)number->data;
		}
	}

	if (is_floating)
		return ulisp_make_float(fsum);

	return ulisp_make_integer(sum);
}

LispObject* ulisp_prim_minus(LispObject* args) {
	long sum;
	double fsum;
	GLboolean is_floating;

	LispObject* first = ulisp_car(args);
	if (first->type & LISP_INTEGER) {
		sum = *(long*)first->data;
		fsum = *(long*)first->data;

		is_floating = GL_FALSE;
	}
	else if (first->type & LISP_FLOAT) {
		sum = *(double*)first->data;
		fsum = *(double*)first->data;

		is_floating = GL_TRUE;
	}

	for(LispObject* i = ulisp_cdr(args); i != nil; i = ulisp_cdr(i)) {
		LispObject* number = ulisp_car(i);
		assert(number->type & LISP_NUMBER);

		if (number->type & LISP_FLOAT) {
			if (!is_floating) {
				is_floating = GL_TRUE;
				fsum += sum;
			}

			fsum -= *(double*)number->data;
		}
		else {
			if (is_floating)
				fsum -= *(long*)number->data;
			else
				sum -= *(long*)number->data;
		}
	}

	if (is_floating)
		return ulisp_make_float(fsum);

	return ulisp_make_integer(sum);
}

LispObject* ulisp_prim_mul(LispObject* args) {
	long prod = 1;
	double fprod = 1.0;
	GLboolean is_floating = GL_FALSE;

	for(LispObject* i = args; i != nil; i = ulisp_cdr(i)) {
		LispObject* number = ulisp_car(i);
		assert(number->type & LISP_NUMBER);

		if (number->type & LISP_FLOAT) {
			if (!is_floating) {
				is_floating = GL_TRUE;
				fprod *= prod;
			}

			fprod *= *(double*)number->data;
		}
		else {
			if (is_floating)
				fprod *= *(long*)number->data;
			else
				prod *= *(long*)number->data;
		}
	}

	if (is_floating)
		return ulisp_make_float(fprod);

	return ulisp_make_integer(prod);
}

LispObject* ulisp_prim_num_eq(LispObject* args) {
	LispObject* a = ulisp_car(args);
	LispObject* b = ulisp_car(ulisp_cdr(args));

	assert(a->type & LISP_NUMBER && b->type & LISP_NUMBER);

	if (a->type & LISP_INTEGER) {
		if (*(long*)a->data == *(long*)b->data)
			return tee;
		else
			return nil;
	}
	else {
	if (*(double*)a->data == *(double*)b->data)
			return tee;
		else
			return nil;
	}
}

LispObject* ulisp_prim_num_inf(LispObject* args) {
	LispObject* a = ulisp_car(args);
	LispObject* b = ulisp_car(ulisp_cdr(args));

	assert(a->type & LISP_NUMBER && b->type & LISP_NUMBER);

	if (a->type & LISP_INTEGER) {
		if (*(long*)a->data < *(long*)b->data)
			return tee;
		else
			return nil;
	}
	else {
	if (*(double*)a->data < *(double*)b->data)
			return tee;
		else
			return nil;
	}
}

LispObject* ulisp_prim_num_sup(LispObject* args) {
	LispObject* a = ulisp_car(args);
	LispObject* b = ulisp_car(ulisp_cdr(args));

	assert(a->type & LISP_NUMBER && b->type & LISP_NUMBER);

	if (a->type & LISP_INTEGER) {
		if (*(long*)a->data > *(long*)b->data)
			return tee;
		else
			return nil;
	}
	else {
	if (*(double*)a->data > *(double*)b->data)
			return tee;
		else
			return nil;
	}
}

void env_push_fun(const char* name, void* function) {
	environnement = ulisp_cons(ulisp_cons(ulisp_make_symbol(name),
										  ulisp_builtin_proc(function)),
							   environnement);
}

void ulisp_init(void) {
	nil = ulisp_make_symbol("nil");
	nil->type |= LISP_LIST;

	tee = ulisp_make_symbol("t");
	quote = ulisp_make_symbol("quote");
	iffe = ulisp_make_symbol("if");
	begin = ulisp_make_symbol("begin");
	lambda = ulisp_make_symbol("lambda");
	mlambda = ulisp_make_symbol("mlambda");
	define = ulisp_make_symbol("define");
	rest = ulisp_make_symbol("&rest");
	not_found = ulisp_make_symbol("not-found");

	environnement = ulisp_cons(ulisp_cons(nil, nil),
							   ulisp_cons(ulisp_cons(tee, tee),
										  nil));

	env_push_fun("car", ulisp_prim_car);
	env_push_fun("cdr", ulisp_prim_cdr);
	env_push_fun("cons", ulisp_prim_cons);
	env_push_fun("eq?", ulisp_prim_eq);
	env_push_fun("+", ulisp_prim_plus);
	env_push_fun("-", ulisp_prim_minus);
	env_push_fun("*", ulisp_prim_mul);
	env_push_fun("=", ulisp_prim_num_eq);
	env_push_fun(">", ulisp_prim_num_sup);
	env_push_fun("<", ulisp_prim_num_inf);

	ulisp_eval(ulisp_read_list(read_file("./lisp/core.ul")), nil);
}

LispObject* ulisp_append(LispObject* a, LispObject* b) {
	assert(a->type & LISP_LIST && b->type & LISP_LIST);

	if (ulisp_eq(a, nil))
		return b;
	else if (ulisp_eq(b, nil))
		return a;
	else
		return ulisp_cons(ulisp_car(a),
						  ulisp_append(ulisp_cdr(a), b));
}

LispObject* ulisp_apply(LispObject* proc, LispObject* env, LispObject* arguments) {
	assert(proc->type & LISP_PROC || proc->type & LISP_PROC_BUILTIN);
	assert(arguments->type & LISP_LIST);

	if (proc->type & LISP_PROC_BUILTIN) {
		LispObject* (**function)(LispObject*) = (LispObject *(**)(LispObject *))&proc->data;
		return (*function)(arguments);
	}
	else if (proc->type & LISP_PROC) {
		LispProc* lisp_proc = (LispProc*)proc->data;
		LispObject* new_env = env;

		LispObject *applied_arg = arguments,
			*arg_name = lisp_proc->arguments;

		while (applied_arg != nil && arg_name != nil) {
			LispObject* arg = ulisp_car(arg_name);

			if (ulisp_eq(arg, rest)) {
				new_env = ulisp_cons(ulisp_cons(ulisp_car(ulisp_cdr(arg_name)),
												applied_arg),
									 new_env);

				break;
			}

			new_env = ulisp_cons(ulisp_cons(ulisp_car(arg_name),
											ulisp_car(applied_arg)), new_env);

			applied_arg = ulisp_cdr(applied_arg);
			arg_name = ulisp_cdr(arg_name);
		}

		return ulisp_eval(ulisp_cons(begin, lisp_proc->expression), new_env);
	}

	return NULL;
}

LispObject* ulisp_macroexpand(LispObject* expression, LispObject* env) {
	if (expression->type & LISP_CONS && ulisp_car(expression)->type & LISP_SYMBOL) {
		LispObject* proc = ulisp_assoc(ulisp_append(env, environnement), ulisp_car(expression));

		if (proc != nil && proc->type & LISP_PROC && ((LispProc*)proc->data)->is_macro)
			return ulisp_macroexpand(ulisp_apply(proc, env, ulisp_cdr(expression)), env);
		else
			return expression;
	}
	else {
		return expression;
	}
}

LispObject* ulisp_eval(LispObject* expression, LispObject* env) {
	if (expression->type & LISP_SYMBOL) {
		LispObject* exp = ulisp_assoc(ulisp_append(env, environnement), expression);

		if (ulisp_eq(exp, not_found)) {
			printf("Not found: %s\n", ulisp_symbol_string(expression));
			ulisp_print(env, stdout);
			puts("");
		}

		return exp;
	}
	else if (expression->type & LISP_NUMBER) {
		return expression;
	}
	else {
		expression = ulisp_macroexpand(expression, env);

		LispObject* applied_symbol = ulisp_car(expression);

		if (applied_symbol->type & LISP_SYMBOL) {
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
										 ulisp_cdr(ulisp_cdr(expression)),
										 GL_FALSE);
			}
			else if (ulisp_eq(applied_symbol, mlambda)) {
				return ulisp_make_lambda(ulisp_car(ulisp_cdr(expression)),
										 ulisp_cdr(ulisp_cdr(expression)),
										 GL_TRUE);
			}
			else if (ulisp_eq(applied_symbol, define)) {
				LispObject* name = ulisp_car(ulisp_cdr(expression));
				LispObject* value = ulisp_eval(ulisp_car(ulisp_cdr(ulisp_cdr(expression))), env);

				LispObject* pair = ulisp_cons(name, value);
				environnement = ulisp_cons(pair, environnement);

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

			char* symbol_string = m_strndup(string + i, count);

			long integer;
			double floating;

			int type = parse_number(symbol_string, &integer, &floating);

			if (type == 0)
				list = ulisp_cons(ulisp_make_integer(integer), list);
			else if (type == 1)
				list = ulisp_cons(ulisp_make_float(floating), list);
			else
				list = ulisp_cons(ulisp_make_symbol(symbol_string), list);

			free(symbol_string);

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
	if (obj->type & LISP_SYMBOL) {
		fprintf(stream, "%s", ulisp_symbol_string(obj));
	}
	else if (obj->type & LISP_CONS) {
		printf("(");

		LispObject* c = obj;
		for(; c->type & LISP_CONS; c = ulisp_cdr(c)) {
			ulisp_print(ulisp_car(c), stream);
			printf(" ");
		}

		if (!ulisp_eq(c, nil)) {
			printf(" . ");
			ulisp_print(c, stream);
		}

		printf(")");
	}
	else if (obj->type & LISP_PROC_BUILTIN) {
		printf("<BUILTIN-PROC at %p>", *(void**)obj->data);
	}
	else if (obj->type & LISP_PROC) {
		LispProc* proc = (LispProc*)obj->data;

		printf("<PROC args: ");
		ulisp_print(proc->arguments, stdout);
		printf(", body: ");
		ulisp_print(proc->expression, stdout);
		printf(">");
	}
	else if (obj->type & LISP_INTEGER) {
		printf("%ld", *(long*)obj->data);
	}
	else if (obj->type & LISP_FLOAT) {
		printf("%g", *(double*)obj->data);
	}
}
