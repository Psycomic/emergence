#include "ulisp.h"

#include <assert.h>
#include <string.h>

#define IS_BLANK(x) ((x) == ' ' || (x) == '\n' || (x) == '\t')
#define DEBUG(m,e) printf("%s:%d: %s:",__FILE__,__LINE__,m); ulisp_print(e, stdout); puts("");
#define CAR(o) ((ConsCell*)(o)->data)->car
#define CDR(o) ((ConsCell*)(o)->data)->cdr

LispObject *environnement, *nil, *tee, *quote, *iffe, *begin,
	*lambda, *mlambda, *define, *rest, *not_found;

#define DEFAULT_WORKSPACESIZE (2 << 16)

static uint64_t workspace_size = DEFAULT_WORKSPACESIZE;
static uint64_t free_space = DEFAULT_WORKSPACESIZE;
static uint32_t cons_cell_size = sizeof(LispObject) + sizeof(ConsCell);
static LispObject* free_list;
static uchar* workspace;
static Stack gc_stack;

LispObject* ulisp_car(LispObject* pair);
LispObject* ulisp_cdr(LispObject* object);
void ulisp_gc(LispObject* cons_cell);

void free_list_init() {
	workspace = malloc(cons_cell_size * free_space);
	assert(workspace != NULL);

	free_list = (LispObject*)workspace;

	LispObject* free_list_ptr = free_list;

	for (uint64_t i = 0; i < free_space - 1; i++) {
		free_list_ptr->type = LISP_CONS;
		LispObject* next_free_list_ptr = (LispObject*)((uchar*)free_list_ptr + cons_cell_size);

		CAR(free_list_ptr) = next_free_list_ptr;
		CDR(free_list_ptr) = nil;

		free_list_ptr = next_free_list_ptr;
	}

	free_list_ptr->type = LISP_CONS;
	CAR(free_list_ptr) = nil;
	CDR(free_list_ptr) = nil;
}

LispObject* free_list_alloc() {
	assert(free_space > 0);

	LispObject* obj = free_list;
	free_list = ulisp_car(free_list);
	free_space--;

	return obj;
}

void mark_object(LispObject* object) {
mark:
	if (object->type & GC_MARKED) return;

	if (object->type & LISP_CONS) {
		object->type |= GC_MARKED;

		mark_object(ulisp_car(object));
		object = ulisp_cdr(object);
		goto mark;
	}
	else if (object->type & LISP_PROC) {
		LispProc* proc = object->data;

		mark_object(proc->arguments);
		mark_object(proc->expression);
	}
}

void free_list_free(LispObject* object) {
	CDR(object) = nil;
	CAR(object) = free_list;

	object->type = LISP_CONS;

	free_list = object;
	free_space++;
}

void free_list_sweep() {
	free_list = nil;
	free_space = 0;

	for (int i = workspace_size - 1; i >= 0; i--) {
		LispObject* obj = (uchar*)workspace + i * cons_cell_size;

		if (!(obj->type & GC_MARKED))
			free_list_free(obj);
		else
			obj->type &= ~GC_MARKED;
	}
}

void ulisp_gc(LispObject* cons_cell) {
	printf("Garbage collection...\n");

	for (uint64_t i = 0; i < gc_stack.top; i++)
		mark_object(gc_stack.data[i]);

	mark_object(cons_cell);
	mark_object(environnement);
	free_list_sweep();

	printf("Now having %lu free space!\n", free_space);

/* 	if (free_space < 20) { */
/* 		uchar* new_space = malloc(cons_cell_size * workspace_size); */
/* 		workspace  */
/* 	} */
}

LispObject* ulisp_cons(LispObject* first, LispObject* second) {
	LispObject* new_object = free_list_alloc();

	new_object->type = LISP_CONS | LISP_LIST;

	((ConsCell*)new_object->data)->car = first;
	((ConsCell*)new_object->data)->cdr = second;

	if (free_space <= 20)
		ulisp_gc(new_object);

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
	long sum = 0;
	double fsum = 0.f;
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

	if (ulisp_cdr(args) == nil) {
		if (is_floating)
			return ulisp_make_float(-fsum);
		else
			return ulisp_make_integer(-sum);
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

LispObject* ulisp_prim_div(LispObject* args) {
	LispObject *first = ulisp_car(args),
		*second = ulisp_car(ulisp_cdr(args));

	assert(first->type & LISP_NUMBER && second->type & LISP_NUMBER);

	if (first->type & LISP_FLOAT && second->type & LISP_FLOAT)
		return ulisp_make_float(*(double*)first->data / *(double*)second->data);
	if (first->type & LISP_FLOAT && second->type & LISP_INTEGER)
 		return ulisp_make_float(*(double*)first->data / *(long*)second->data);
	if (first->type & LISP_INTEGER && second->type & LISP_FLOAT)
 		return ulisp_make_float(*(long*)first->data / *(double*)second->data);
	if (first->type & LISP_INTEGER && second->type & LISP_INTEGER)
 		return ulisp_make_float(*(long*)first->data / *(long*)second->data);

	return NULL;
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

	if (a->type & LISP_INTEGER && b->type & LISP_INTEGER) {
		if (*(long*)a->data < *(long*)b->data)
			return tee;
		else
			return nil;
	}
	else if (a->type & LISP_FLOAT && b->type & LISP_FLOAT) {
		if (*(double*)a->data < *(double*)b->data)
			return tee;
		else
			return nil;
	}
	else if (a->type & LISP_INTEGER && b->type & LISP_FLOAT) {
		if (*(long*)a->data < *(double*)b->data)
			return tee;
		else
			return nil;
	}
	else if (a->type & LISP_FLOAT && b->type & LISP_INTEGER) {
		if (*(double*)a->data < *(long*)b->data)
			return tee;
		else
			return nil;
	}

	return NULL;
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

LispObject* ulisp_prim_num_mod(LispObject* args) {
	return ulisp_make_integer(*(long*)ulisp_car(args)->data %
							  *(long*)ulisp_car(ulisp_cdr(args))->data);
}

void env_push_fun(const char* name, void* function) {
	environnement = ulisp_cons(ulisp_cons(ulisp_make_symbol(name),
										  ulisp_builtin_proc(function)),
							   environnement);
}

void ulisp_init(void) {
	stack_init(&gc_stack);

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

	free_list_init();

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
	env_push_fun("/", ulisp_prim_div);
	env_push_fun("=", ulisp_prim_num_eq);
	env_push_fun(">", ulisp_prim_num_sup);
	env_push_fun("<", ulisp_prim_num_inf);
	env_push_fun("mod", ulisp_prim_num_mod);

	ulisp_eval(ulisp_read_list(read_file("./lisp/core.ul")), nil);
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

		uint32_t push_count = 0;
		while (applied_arg != nil && arg_name != nil) {
			LispObject* arg = ulisp_car(arg_name);

			if (ulisp_eq(arg, rest)) {
				new_env = ulisp_cons(
					ulisp_cons(ulisp_car(ulisp_cdr(arg_name)),
							   applied_arg),
					new_env);

				stack_push(&gc_stack, new_env);
				push_count++;
				break;
			}

			new_env = ulisp_cons(
				ulisp_cons(ulisp_car(arg_name),
						   ulisp_car(applied_arg)), new_env);

			stack_push(&gc_stack, new_env);
			push_count++;

			applied_arg = ulisp_cdr(applied_arg);
			arg_name = ulisp_cdr(arg_name);
		}

		LispObject* result = ulisp_eval(ulisp_cons(begin, lisp_proc->expression), new_env);
		stack_pop(&gc_stack, push_count);
		return result;
	}

	return NULL;
}

LispObject* ulisp_macroexpand(LispObject* expression, LispObject* env) {
	if (expression->type & LISP_CONS && ulisp_car(expression)->type & LISP_SYMBOL) {
		LispObject* proc = ulisp_assoc(env, ulisp_car(expression));
		if (ulisp_eq(proc, not_found))
			proc = ulisp_assoc(environnement, ulisp_car(expression));

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
	stack_push(&gc_stack, expression);
	stack_push(&gc_stack, env);
	LispObject* result;

	if (expression->type & LISP_SYMBOL) {
		LispObject* exp = ulisp_assoc(env, expression);

		if (ulisp_eq(exp, not_found))
			exp = ulisp_assoc(environnement, expression);

		if (ulisp_eq(exp, not_found)) {
			printf("Not found: %s\n", ulisp_symbol_string(expression));
			ulisp_print(env, stdout);
			puts("");
		}

		result = exp;
		goto end;
	}
	else if (expression->type & LISP_NUMBER) {
		result = expression;
		goto end;
	}
	else {
		expression = ulisp_macroexpand(expression, env);

		LispObject* applied_symbol = ulisp_car(expression);

		if (applied_symbol->type & LISP_SYMBOL) {
			if (ulisp_eq(applied_symbol, quote)) {
				result = ulisp_car(ulisp_cdr(expression));
				goto end;
			}
			else if (ulisp_eq(applied_symbol, iffe)) {
				LispObject* arguments = ulisp_cdr(expression);

				if (!ulisp_eq(ulisp_eval(ulisp_car(arguments), env), nil)) {
					result = ulisp_eval(ulisp_car(ulisp_cdr(arguments)), env);
					goto end;
				}
				else if (!ulisp_eq(ulisp_cdr(ulisp_cdr(arguments)), nil)) {
					result = ulisp_eval(ulisp_car(ulisp_cdr(ulisp_cdr(arguments))), env);
					goto end;
				}
				else {
					result = nil;
					goto end;
				}
			}
			else if (ulisp_eq(applied_symbol, begin))  {
				LispObject* last_obj = nil;

				for(LispObject* form = ulisp_cdr(expression); form != nil; form = ulisp_cdr(form))
					last_obj = ulisp_eval(ulisp_car(form), env);

				result = last_obj;
				goto end;
			}
			else if (ulisp_eq(applied_symbol, lambda)) {
				result = ulisp_make_lambda(ulisp_car(ulisp_cdr(expression)),
										   ulisp_cdr(ulisp_cdr(expression)),
										   GL_FALSE);
				goto end;
			}
			else if (ulisp_eq(applied_symbol, mlambda)) {
				result = ulisp_make_lambda(ulisp_car(ulisp_cdr(expression)),
										   ulisp_cdr(ulisp_cdr(expression)),
										   GL_TRUE);
				goto end;
			}
			else if (ulisp_eq(applied_symbol, define)) {
				LispObject* name = ulisp_car(ulisp_cdr(expression));
				LispObject* value = ulisp_eval(ulisp_car(ulisp_cdr(ulisp_cdr(expression))), env);

				LispObject* pair = ulisp_cons(name, value);
				environnement = ulisp_cons(pair, environnement);

				result = name;
				goto end;
			}
		}

		LispObject* proc = ulisp_eval(applied_symbol, env);
		LispObject* evaluated_args = nil;
		uint32_t args_count = 0;

		for(LispObject* argument = ulisp_cdr(expression); argument != nil; argument = ulisp_cdr(argument)) {
			stack_push(&gc_stack, evaluated_args);
			evaluated_args = ulisp_cons(ulisp_eval(ulisp_car(argument), env), evaluated_args);
			args_count++;
		}

		result = ulisp_apply(proc, env, ulisp_nreverse(evaluated_args));
		stack_pop(&gc_stack, args_count);
		goto end;
	}

end:
	stack_pop(&gc_stack, 2);
	return result;
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

#ifdef _DEBUG
char* ulisp_debug_print(LispObject* obj) {
	char* buffer = NULL;
	size_t bufferSize = 0;
	FILE* myStream = open_memstream(&buffer, &bufferSize);
	ulisp_print(obj, myStream);

	fclose(myStream);

	return buffer;
}
#endif

void ulisp_print(LispObject* obj, FILE* stream) {
	if (obj->type & LISP_SYMBOL) {
		fprintf(stream, "%s", ulisp_symbol_string(obj));
	}
	else if (obj->type & LISP_CONS) {
		fprintf(stream, "(");

		LispObject* c = obj;
		for(; c->type & LISP_CONS; c = ulisp_cdr(c)) {
			ulisp_print(ulisp_car(c), stream);
			fprintf(stream, " ");
		}

		if (!ulisp_eq(c, nil)) {
			fprintf(stream, " . ");
			ulisp_print(c, stream);
		}

		fprintf(stream, ")");
	}
	else if (obj->type & LISP_PROC_BUILTIN) {
		fprintf(stream, "<BUILTIN-PROC at %p>", *(void**)obj->data);
	}
	else if (obj->type & LISP_PROC) {
		LispProc* proc = (LispProc*)obj->data;

		fprintf(stream, "<PROC args: ");
		ulisp_print(proc->arguments, stream);
		fprintf(stream, ", body: ");
		ulisp_print(proc->expression, stream);
		fprintf(stream, ">");
	}
	else if (obj->type & LISP_INTEGER) {
		fprintf(stream, "%ld", *(long*)obj->data);
	}
	else if (obj->type & LISP_FLOAT) {
		fprintf(stream, "%gf", *(double*)obj->data);
	}
}
