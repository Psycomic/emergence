#include "ulisp.h"
#include "psyche.h"
#include "random.h"

#include <assert.h>
#include <string.h>
#include <stdarg.h>

#define IS_BLANK(x) ((x) == ' ' || (x) == '\n' || (x) == '\t')
#define DEBUG(m,e) printf("%s:%d: %s:",__FILE__,__LINE__,m); ulisp_print(e, ulisp_standard_output); puts("");
#define CAR(o) ((ConsCell*)(o)->data)->car
#define CDR(o) ((ConsCell*)(o)->data)->cdr
#define panic(str) do { printf(str"\n"); assert(0); } while (0)
#define AS(o, t) ((t *)o->data)
#define ASSERT(v) if (!(v)) { ulisp_throw(ulisp_make_symbol("assertion: "#v" in " __FILE__)); }
#define S_OFFSET(s, slot) (size_t)(&((s*)NULL)->slot)

LispObject *environnement, *read_environnement, /* Global environnements */
	*nil = NULL, *tee, *quote, *iffe, *begin, *named_lambda, *def, *rest, *def_macro, *call_cc,
	*quasiquote, *unquote, *set,/* keywords */
	*assertion_symbol, *top_level, *make_closure;

#define DEFAULT_WORKSPACESIZE (2 << 11)

static uint64_t workspace_size = DEFAULT_WORKSPACESIZE;
static uint64_t free_space = DEFAULT_WORKSPACESIZE;
static uint32_t cons_cell_size = sizeof(LispObject) + sizeof(ConsCell);
static LispObject* free_list;
static uchar* workspace;
static Stack gc_stack;

static LispObject* eval_stack;
static LispObject* envt_register;
static LispObject* current_continuation;
static LispObject* last_called_proc = NULL;
static LispTemplate* template_register;
static uchar* ulisp_rip;

static LispObject** exception_stack = NULL;

LispObject* ulisp_car(LispObject* pair);
LispObject* ulisp_cdr(LispObject* object);
void ulisp_apply();
void ulisp_throw(LispObject* exception);
LispObject* ulisp_nreverse(LispObject* obj);
LispObject* ulisp_make_symbol(const char* string);

typedef struct LispLabel {
	long id;
	size_t offset;
} LispLabel;

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
		LispClosure* proc = object->data;

		mark_object(proc->envt);

		for (uint i = 0; i < proc->template->literals_count; i++)
			mark_object(proc->template->literals[i]);
	}
	else if (object->type & LISP_CONTINUATION) {
		LispContinuation* cont = object->data;

		mark_object(cont->envt_register);
		mark_object(cont->eval_stack);

		if (cont->previous_cont)
			mark_object(cont->previous_cont);
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

void ulisp_gc() {
	printf("Garbage collection...\n");

	for (uint64_t i = 0; i < gc_stack.top; i++)
		mark_object(gc_stack.data[i]);

	mark_object(environnement);
	mark_object(read_environnement);
	mark_object(envt_register);
	mark_object(value_register);
	mark_object(eval_stack);

	if (current_continuation)
		mark_object(current_continuation);

	if (template_register) {
		for (uint i = 0; i < template_register->literals_count; i++)
			mark_object(template_register->literals[i]);
	}

	free_list_sweep();

	printf("Now having %lu free space!\n", free_space);
}

LispObject* ulisp_cons(LispObject* first, LispObject* second) {
	LispObject* new_object = free_list_alloc();

	new_object->type = LISP_CONS | LISP_LIST;

	CAR(new_object) = first;
	CDR(new_object) = second;

	return new_object;
}

LispObject* ulisp_car(LispObject* cons_obj) {
	ASSERT(cons_obj->type & LISP_CONS);

	return ((ConsCell*)cons_obj->data)->car;
}

LispObject* ulisp_cdr(LispObject* cons_obj) {
	ASSERT(cons_obj->type & LISP_CONS);

	return ((ConsCell*)cons_obj->data)->cdr;
}

LispObject* ulisp_list(LispObject* a, ...) {
	va_list args;
	LispObject* result = nil;

	va_start(args, a);
	LispObject* current_obj = a;

	while (current_obj != NULL) {
		result = ulisp_cons(current_obj, result);
		current_obj = va_arg(args, LispObject*);
	}

	va_end(args);

	return ulisp_nreverse(result);
}

LispObject* ulisp_make_symbol(const char* string) {
	int string_length = strlen(string);

	LispObject* new_object = malloc(sizeof(LispObject) + sizeof(LispSymbol) + sizeof(char) * string_length + 1);

	new_object->type = LISP_SYMBOL;

	LispSymbol* symbol = (LispSymbol*)new_object->data;
	strcpy(symbol->str, string);

	symbol->hash = hash((uchar*)string);

	if (nil && symbol->hash == ((LispSymbol*)nil->data)->hash)
		return nil;

	return new_object;
}

char* ulisp_symbol_string(LispObject* symbol) {
	ASSERT(symbol->type & LISP_SYMBOL);

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

LispObject* ulisp_make_continuation(LispObject* envt_register, LispObject* eval_stack, LispObject* current_cont, LispTemplate* current_template, uchar* rip) {
	LispObject* new_cont_obj = malloc(sizeof(LispObject) + sizeof(LispContinuation));
	new_cont_obj->type = LISP_CONTINUATION;

	LispContinuation* cont = (LispContinuation*)new_cont_obj->data;

	cont->envt_register = envt_register;
	cont->eval_stack = eval_stack;
	cont->previous_cont = current_cont;
	cont->current_template = current_template;
	cont->rip = rip;

	return new_cont_obj;
}

LispObject* ulisp_make_stream(FILE* f) {
	LispObject* new_stream_obj = malloc(sizeof(LispObject) + sizeof(LispStream));
	new_stream_obj->type = LISP_STREAM;

	LispStream* stream = new_stream_obj->data;
	stream->buffer = malloc(8);
	stream->size = 0;
	stream->capacity = 8;
	stream->f = f;

	return new_stream_obj;
}

void ulisp_stream_write(char* s, LispObject* stream_obj) {
	size_t size = strlen(s);
	GLboolean resized = GL_FALSE;
	LispStream* stream = stream_obj->data;

	if (stream->f)
		fwrite(s, size, 1, stream->f);

	while (stream->size + size > stream->capacity) {
		stream->capacity *= 2;
		resized = GL_TRUE;
	}

	if (resized) {
		stream->buffer = realloc(stream->buffer, stream->capacity);
	}

	memcpy(stream->buffer + stream->size, s, size);
	stream->size += size;
}

char* ulisp_stream_finish_output(LispObject* stream_obj) {
	LispStream* stream = stream_obj->data;

	stream->buffer[stream->size] = '\0';
	return stream->buffer;
}

void ulisp_stream_format(LispObject* stream, const char* format, ...) {
	va_list args;

	va_start(args, format);
	int size = vsnprintf(NULL, 0, format, args);
	va_end(args);

	char* s = malloc(size + 1);

	va_start(args, format);
	vsnprintf(s, size + 1, format, args);
	va_end(args);

	ulisp_stream_write(s, stream);
	free(s);
}

LispObject* ulisp_make_closure(LispTemplate* template, LispObject* envt) {
	LispObject* obj = malloc(sizeof(LispObject) + sizeof(LispClosure));
	obj->type = LISP_PROC;

	LispClosure* closure = obj->data;

	closure->template = template;
	closure->envt = envt;

	return obj;
}

LispObject* ulisp_prim_make_closure(LispObject* arguments) {
	LispTemplate* template = ulisp_car(arguments);

	return ulisp_make_closure(template, envt_register);
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

LispObject* ulisp_assoc(LispObject* plist, LispObject* symbol, GLboolean* out_found) {
	ASSERT(plist->type & LISP_LIST);
	ASSERT(symbol->type & LISP_SYMBOL);

	for (; plist != nil; plist = ulisp_cdr(plist)) {
		LispObject* temp = ulisp_car(plist);

		if (ulisp_eq(ulisp_car(temp), symbol)) {
			*out_found = GL_TRUE;
			return ulisp_cdr(temp);
		}
	}

	*out_found = GL_FALSE;
	return nil;
}

LispObject* ulisp_map(LispObject* (*fn)(LispObject*), LispObject* list) {
	LispObject* new_list = nil;

	for (LispObject* a = list; a != nil; a = ulisp_cdr(a)) {
		new_list = ulisp_cons(fn(ulisp_car(a)), new_list);
		stack_push(&gc_stack, new_list);

		if (free_space < 20)
			ulisp_gc();

		stack_pop(&gc_stack, 1);
	}

	return ulisp_nreverse(new_list);
}

LispObject* ulisp_nconc(LispObject* a, LispObject* b) {
	ASSERT(a->type & LISP_LIST);
	ASSERT(b->type & LISP_LIST);

	if (a == nil)
		return b;
	if (b == nil)
		return b;

	LispObject* a_it;
	for (a_it = a; CDR(a_it) != nil; a_it = CDR(a_it));

	CDR(a_it) = b;

	return a;
}

void ulisp_add_to_environnement(char* name, LispObject* closure) {
	environnement = ulisp_cons(ulisp_cons(ulisp_make_symbol(name),
										  closure),
							   environnement);
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

LispObject* ulisp_builtin_proc(void* function, LispObject* name) {
	LispObject* proc = malloc(sizeof(LispObject) + sizeof(LispBuiltinProc));
	proc->type = LISP_PROC_BUILTIN;
	LispBuiltinProc* builtin = proc->data;

	builtin->name = name;
	builtin->fn = function;

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
		ASSERT(number->type & LISP_NUMBER);

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

LispObject* ulisp_prim_symbol_p(LispObject* args) {
	return ulisp_car(args)->type & LISP_SYMBOL ? tee : nil;
}

LispObject* ulisp_prim_proc_p(LispObject* args) {
	LispObject* o = ulisp_car(args);

	return (o->type & LISP_PROC || o->type & LISP_PROC_BUILTIN) ? tee : nil;
}

LispObject* ulisp_prim_cont_p(LispObject* args) {
	return ulisp_car(args)->type & LISP_CONTINUATION ? tee : nil;
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
		ASSERT(number->type & LISP_NUMBER);

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
		ASSERT(number->type & LISP_NUMBER);

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

	ASSERT(first->type & LISP_NUMBER && second->type & LISP_NUMBER);

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

	ASSERT(a->type & LISP_NUMBER && b->type & LISP_NUMBER);

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

	ASSERT(a->type & LISP_NUMBER && b->type & LISP_NUMBER);

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

	ASSERT(a->type & LISP_NUMBER && b->type & LISP_NUMBER);

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

LispObject* ulisp_prim_insert_at_point(LispObject* args) {
	if (ps_current_input)
		ps_input_insert_at_point(ps_current_input, ulisp_debug_print(ulisp_car(args)));

	return nil;
}

LispObject* ulisp_prim_random(LispObject* args) {
	ASSERT(ulisp_length(args) == 1);
	LispObject* number = ulisp_car(args);
	ASSERT(number->type & LISP_NUMBER);

	if (number->type & LISP_FLOAT)
		return ulisp_make_float(random_float() * *(double*)number->data);
	else
		return ulisp_make_integer(random_randint() % *(long*)number->data);
}

LispObject* ulisp_prim_cons_p(LispObject* args) {
	ASSERT(ulisp_length(args) == 1);

	return ulisp_car(args)->type & LISP_CONS ? tee : nil;
}

void ulisp_invoke_debugger(LispObject* exception) {
	printf("Backtrace:\n");
	uint i = 0;

	if (last_called_proc) {
		LispObject* name;
		if (last_called_proc->type & LISP_PROC_BUILTIN)
			name = AS(last_called_proc, LispBuiltinProc)->name;
		else
			name = AS(last_called_proc, LispClosure)->template->name;

		printf("0 -> ");
		ulisp_print(ulisp_cons(name, eval_stack), ulisp_standard_output);
		printf("\n");

		i = 1;
	}

	LispObject* last_cont = NULL;

	for (LispObject* cont = current_continuation; cont != NULL; cont = ((LispContinuation*)cont->data)->previous_cont) {
		LispContinuation* continuation = cont->data;

		printf("%d -> ", i++);
		ulisp_print(ulisp_cons(continuation->current_template->name, ulisp_map(ulisp_cdr, continuation->envt_register)),
					ulisp_standard_output);
		printf("\n");

		last_cont = cont;
	}

	printf("\nUnhandled exception: ");
	ulisp_print(exception, ulisp_standard_output);
	printf("\n");

choice:
	printf("\nOptions: \n"
		   "[1] ABORT TO TOP LEVEL\n"
		   "[2] CHANGE VALUE\n"
		   "[3] DEBUGGER BREAKPOINT\n");

	printf("] ");

	uint choice;
	if (m_scanf("%u", &choice) < 0) {
		printf("Enter a number!\n");
		goto choice;
	}

	if (choice == 1) {
		if (last_cont) {
			eval_stack = ulisp_cons(exception, nil);
			value_register = last_cont;
			ulisp_apply();
		}
		else {
			value_register = exception;
		}

		longjmp(ulisp_top_level, 1);
	}
	else if (choice == 2) {
		printf("Enter new value: ");
		char buffer[128];
		fgets(buffer, sizeof(buffer) - 1, stdin);

		LispObject* new_value = ulisp_read(buffer);

		printf("Level: ");
		uint level;
		if (m_scanf("%u", &level) < 0) {
			printf("Enter a number!\n");
			goto choice;
		}

		uint i = 1;
		for (LispObject* cont = current_continuation; cont != NULL; cont = ((LispContinuation*)cont->data)->previous_cont) {
			if (i == level) {
				eval_stack = ulisp_cons(new_value, nil);
				value_register = cont;
				ulisp_apply();

				longjmp(ulisp_run_level_stack[ulisp_run_level_top - 1], 1);
			}

			i++;
		}

		printf("No such level!\n");
		goto choice;
	}
	else if (choice == 3) {
		assert(0);
	}
	else {
		printf("Enter the correct option!\n");
		goto choice;
	}
}

void ulisp_throw(LispObject* exception) {
	if (*exception_stack == nil) {
		ulisp_invoke_debugger(exception);
	}

	LispObject* cont = CAR(*exception_stack);
	*exception_stack = CDR(*exception_stack);

	value_register = cont;
	eval_stack = ulisp_cons(exception, nil);
	ulisp_apply();

	longjmp(ulisp_run_level_stack[ulisp_run_level_top - 1], 1);
}

LispObject* ulisp_prim_throw(LispObject* arguments) {
	ulisp_throw(ulisp_car(arguments));
	return value_register;
}

void env_push_fun(const char* name, void* function) {
	LispObject* function_symbol = ulisp_make_symbol(name);

	environnement = ulisp_cons(ulisp_cons(function_symbol,
										  ulisp_builtin_proc(function, function_symbol)),
							   environnement);
}

static LispObject *bytecode_push, *bytecode_push_cont, *bytecode_lookup_var,
	*bytecode_apply, *bytecode_restore_cont, *bytecode_fetch_literal, *bytecode_label,
	*bytecode_bind, *bytecode_branch_if, *bytecode_branch_else, *bytecode_branch,
	*bytecode_fetch_cc, *bytecode_list_bind, *bytecode_set;

LispObject* ulisp_standard_output;

void ulisp_init(void) {
	stack_init(&gc_stack);
	ulisp_run_level_top = 0;

	nil = ulisp_make_symbol("nil");
	nil->type |= LISP_LIST;

	value_register = nil;
	eval_stack = nil;
	template_register = NULL;
	envt_register = nil;
	ulisp_rip = NULL;
	current_continuation = NULL;
	read_environnement = nil;
	exception_stack = NULL;

	bytecode_push = ulisp_make_symbol("push");
	bytecode_push_cont = ulisp_make_symbol("push-cont");
	bytecode_lookup_var = ulisp_make_symbol("lookup-var");
	bytecode_apply = ulisp_make_symbol("apply");
	bytecode_restore_cont = ulisp_make_symbol("restore-cont");
	bytecode_fetch_literal =  ulisp_make_symbol("fetch-literal");
	bytecode_label = ulisp_make_symbol("label");
	bytecode_bind = ulisp_make_symbol("bind");
	bytecode_branch_if = ulisp_make_symbol("branch-if");
	bytecode_branch_else = ulisp_make_symbol("branch-else");
	bytecode_branch = ulisp_make_symbol("branch");
	bytecode_fetch_cc = ulisp_make_symbol("fetch-cc");
	bytecode_list_bind = ulisp_make_symbol("list-bind");
	bytecode_set = ulisp_make_symbol("set");

	tee = ulisp_make_symbol("t");
	quote = ulisp_make_symbol("quote");
	iffe = ulisp_make_symbol("if");
	begin = ulisp_make_symbol("begin");
	named_lambda = ulisp_make_symbol("n-lambda");
	def = ulisp_make_symbol("def");
	def_macro = ulisp_make_symbol("def-macro");
	rest = ulisp_make_symbol("&rest");
	call_cc = ulisp_make_symbol("call/cc");
	assertion_symbol = ulisp_make_symbol("assertion");
	quasiquote = ulisp_make_symbol("quasiquote");
	unquote = ulisp_make_symbol("unquote");
	top_level = ulisp_make_symbol("top_level");
	make_closure = ulisp_make_symbol("make-closure");
	set = ulisp_make_symbol("set!");

	free_list_init();

	LispObject* exception_stack_pair = ulisp_cons(ulisp_make_symbol("exception-stack"),
												  nil);

	environnement = nil;
	environnement = ulisp_cons(ulisp_cons(nil, nil),
							   ulisp_cons(ulisp_cons(tee, tee),
										  ulisp_cons(exception_stack_pair,
													 environnement)));

	exception_stack = &CDR(exception_stack_pair);

	env_push_fun("car", ulisp_prim_car);
	env_push_fun("cdr", ulisp_prim_cdr);
	env_push_fun("cons", ulisp_prim_cons);
	env_push_fun("eq?", ulisp_prim_eq);
	env_push_fun("cons?", ulisp_prim_cons_p);
	env_push_fun("symbol?", ulisp_prim_symbol_p);
	env_push_fun("procedure?", ulisp_prim_proc_p);
	env_push_fun("continuation?", ulisp_prim_cont_p);
	env_push_fun("+", ulisp_prim_plus);
	env_push_fun("-", ulisp_prim_minus);
	env_push_fun("*", ulisp_prim_mul);
	env_push_fun("/", ulisp_prim_div);
	env_push_fun("=", ulisp_prim_num_eq);
	env_push_fun(">", ulisp_prim_num_sup);
	env_push_fun("<", ulisp_prim_num_inf);
	env_push_fun("mod", ulisp_prim_num_mod);
	env_push_fun("insert-at-point", ulisp_prim_insert_at_point);
	env_push_fun("random", ulisp_prim_random);
	env_push_fun("make-closure", ulisp_prim_make_closure);
	env_push_fun("throw", ulisp_prim_throw);

	ulisp_standard_output = ulisp_make_stream(stdout);

	ULISP_TOPLEVEL {
		LispObject* expressions = ulisp_read(read_file("lisp/core.ul"));
		ulisp_eval(expressions);
	}
	ULISP_ABORT {
		printf("Error in initialization!");
		exit(0);
	}
}

void ulisp_eval_stack_push() {
	eval_stack = ulisp_cons(value_register, eval_stack);
}

void ulisp_save_current_continuation(uchar* rip) {
	LispObject* cont = ulisp_make_continuation(envt_register, eval_stack, current_continuation, template_register, rip);
	current_continuation = cont;

	eval_stack = nil;
}

void ulisp_restore_continuation() {
	LispContinuation* cont = current_continuation->data;
	envt_register = cont->envt_register;
	eval_stack = cont->eval_stack;
	template_register = cont->current_template;
	ulisp_rip = cont->rip;

	current_continuation = cont->previous_cont;
}

void ulisp_apply() {
	last_called_proc = value_register;

	if (value_register->type & LISP_PROC_BUILTIN) {
		LispObject* (*fn)(LispObject*) = ((LispBuiltinProc*)value_register->data)->fn;
		value_register = (fn)(eval_stack);
		ulisp_restore_continuation();
	}
	else if (value_register->type & LISP_PROC) {
		LispClosure* closure = value_register->data;

		envt_register = closure->envt;
		template_register = closure->template;
		ulisp_rip = closure->template->code;
	}
	else if (value_register->type & LISP_CONTINUATION) {
		LispContinuation* cont = value_register->data;

		value_register = ulisp_car(eval_stack);

		eval_stack = cont->eval_stack;
		envt_register = cont->envt_register;
		template_register = cont->current_template;
		ulisp_rip = cont->rip;
		current_continuation = cont->previous_cont;
	}
	else {
		ulisp_print(value_register, ulisp_standard_output);
		panic(" is not a function!");
	}

	last_called_proc = NULL;
}

LispObject* ulisp_pop() {
	LispObject* obj = ulisp_car(eval_stack);
	eval_stack = ulisp_cdr(eval_stack);

	return obj;
}

void ulisp_bind(LispObject* symbol) {
	envt_register = ulisp_cons(ulisp_cons(symbol, ulisp_pop()),
							   envt_register);
}

void ulisp_list_bind(LispObject* symbol) {
	envt_register = ulisp_cons(ulisp_cons(symbol, eval_stack),
							   envt_register);

	eval_stack = nil;
}

void ulisp_set(LispObject* symbol) {
	GLboolean found = GL_FALSE;

	for (LispObject* binding = envt_register; binding != nil; binding = CDR(binding)) {
		if (ulisp_eq(symbol, CAR(CAR(binding)))) {
			CDR(CAR(binding)) = value_register;
			found = GL_TRUE;
			break;
		}
	}

	if (!found) {
		for (LispObject* binding = environnement; binding != nil; binding = CDR(binding)) {
			if (ulisp_eq(symbol, CAR(CAR(binding)))) {
				CDR(CAR(binding)) = value_register;
				found = GL_TRUE;
				break;
			}
		}
	}
}

void ulisp_run(LispTemplate* template) {
	ulisp_rip = template->code;
	template_register = template;

	setjmp(ulisp_run_level_stack[ulisp_run_level_top++]);
start:
	if (free_space < 20)
		ulisp_gc();

	switch (ulisp_rip[0]) {
	case ULISP_BYTECODE_APPLY:
		ulisp_apply();
		break;
	case ULISP_BYTECODE_LOOKUP:
	{
		GLboolean found;
		LispObject* symbol = *(void**)(ulisp_rip + 1);
		value_register = ulisp_assoc(envt_register, symbol, &found);

		if (!found) {
			GLboolean global_found;
			value_register = ulisp_assoc(environnement, symbol, &global_found);
			if (!global_found) {
				printf("not found: ");
				ulisp_print(symbol, ulisp_standard_output);
				printf("\n");
				ASSERT(0);
			}
		}

		ulisp_rip += sizeof(void*) + 1;
	}
		break;
	case ULISP_BYTECODE_PUSH_CONT:
	{
		ulisp_save_current_continuation(template_register->code + *(size_t*)(ulisp_rip + 1));
		ulisp_rip += sizeof(void*) + 1;
	}
		break;
	case ULISP_BYTECODE_RESUME_CONT:
		ulisp_restore_continuation();
		break;
	case ULISP_BYTECODE_BRANCH_IF:
		if (value_register == nil)
			ulisp_rip += sizeof(size_t) + 1;
		else
			ulisp_rip = template_register->code + *(size_t*)(ulisp_rip + 1);
		break;
	case ULISP_BYTECODE_BRANCH_ELSE:
		if (value_register == nil)
			ulisp_rip = template_register->code + *(size_t*)(ulisp_rip + 1);
		else
			ulisp_rip += sizeof(size_t) + 1;
		break;
	case ULISP_BYTECODE_BRANCH:
		ulisp_rip = template_register->code + *(size_t*)(ulisp_rip + 1);
		break;
	case ULISP_BYTECODE_PUSH_EVAL:
		ulisp_eval_stack_push();
		ulisp_rip++;
		break;
	case ULISP_BYTECODE_FETCH_LITERAL:
		value_register = template_register->literals[*(uint*)(ulisp_rip + 1)];
		ulisp_rip += sizeof(uint) + 1;
		break;
	case ULISP_BYTECODE_FETCH_CC:
		value_register = current_continuation;
		ulisp_rip++;
		break;
	case ULISP_BYTECODE_BIND:
		ulisp_bind(*(void**)(ulisp_rip + 1));
		ulisp_rip += sizeof(void*) + 1;
		break;
	case ULISP_BYTECODE_LIST_BIND:
		ulisp_list_bind(*(void**)(ulisp_rip + 1));
		ulisp_rip += sizeof(void*) + 1;
		break;
	case ULISP_BYTECODE_SET:
		ulisp_set(*(void**)(ulisp_rip + 1));
		ulisp_rip += sizeof(void*) + 1;
		break;
	case ULISP_BYTECODE_END:
		goto end;
	default:
		panic("Invalid instruction");
	}

	goto start;

end:
	ulisp_run_level_top--;
	return;
}

void ulisp_scan_labels(LispObject* code, DynamicArray* target) {
	size_t offset = 0;

	for (LispObject* exp = code; exp != nil; exp = ulisp_cdr(exp)) {
		LispObject* funcall = ulisp_car(exp);
		LispObject* instruction = ulisp_car(funcall);

		if (ulisp_eq(instruction, bytecode_lookup_var)	||
			ulisp_eq(instruction, bytecode_bind)		||
			ulisp_eq(instruction, bytecode_list_bind)	||
			ulisp_eq(instruction, bytecode_set)			||
			ulisp_eq(instruction, bytecode_push_cont))
		{
			offset += 1 + sizeof(void*);
		}
		else if (ulisp_eq(instruction, bytecode_apply)			||
				 ulisp_eq(instruction, bytecode_restore_cont)	||
				 ulisp_eq(instruction, bytecode_push)			||
				 ulisp_eq(instruction, bytecode_fetch_cc))
		{
			offset += 1;
		}
		else if (ulisp_eq(instruction, bytecode_fetch_literal)) {
			offset += 1 + sizeof(uint);
		}
		else if (ulisp_eq(instruction, bytecode_branch_if)	||
				 ulisp_eq(instruction, bytecode_branch_else)||
				 ulisp_eq(instruction, bytecode_branch))
		{
			offset += 1 + sizeof(size_t);
		}
		else if (ulisp_eq(instruction, bytecode_label)) {
			LispLabel* label = dynamic_array_push_back(target, 1);
			LispObject* number_id = ulisp_car(ulisp_cdr(funcall));
			ASSERT(number_id->type & LISP_INTEGER);

			label->id = *(long*)number_id->data;
			label->offset = offset;
		}
		else {
			ulisp_print(instruction, ulisp_standard_output);
			panic(": Invalid instruction!");
		}
	}
}

size_t ulisp_label_find_rip(DynamicArray* labels, long target_label_num) {
	size_t cont_rip = 0;

	for (uint i = 0; i < labels->size; i++) {
		LispLabel* label = dynamic_array_at(labels, i);

		if (label->id == target_label_num) {
			cont_rip += label->offset;
			break;
		}
		else if (i == labels->size - 1) {
			panic("Label not found!");
		}
	}

	return cont_rip;
}

LispTemplate* ulisp_assembly_compile(LispObject* expressions) {
	DynamicArray bytecode, literals, labels;
	DYNAMIC_ARRAY_CREATE(&bytecode, uchar);
	DYNAMIC_ARRAY_CREATE(&literals, LispObject*);
	DYNAMIC_ARRAY_CREATE(&labels, LispLabel);

	ulisp_scan_labels(expressions, &labels);

	for (LispObject* exp = expressions; exp != nil; exp = ulisp_cdr(exp)) {
		LispObject* current_exp = CAR(exp);
		LispObject* applied_symbol = ulisp_car(current_exp);

		if (ulisp_eq(applied_symbol, bytecode_push)) {
			uchar* instructions = dynamic_array_push_back(&bytecode, 1);
			*instructions = ULISP_BYTECODE_PUSH_EVAL;
		}
		else if (ulisp_eq(applied_symbol, bytecode_push_cont)) {
			uchar* instructions = dynamic_array_push_back(&bytecode, 1 + sizeof(void*));
			instructions[0] = ULISP_BYTECODE_PUSH_CONT;

			long target_label_num = *(long*)ulisp_car(ulisp_cdr(current_exp))->data;
			*(size_t*)(instructions + 1) = ulisp_label_find_rip(&labels, target_label_num);
		}
		else if (ulisp_eq(applied_symbol, bytecode_branch_if)) {
			uchar* instructions = dynamic_array_push_back(&bytecode, 1 + sizeof(size_t));
			instructions[0] = ULISP_BYTECODE_BRANCH_IF;

			long target_label_num = *(long*)ulisp_car(ulisp_cdr(current_exp))->data;
			*(size_t*)(instructions + 1) = ulisp_label_find_rip(&labels, target_label_num);
		}
		else if (ulisp_eq(applied_symbol, bytecode_branch_else)) {
			uchar* instructions = dynamic_array_push_back(&bytecode, 1 + sizeof(size_t));
			instructions[0] = ULISP_BYTECODE_BRANCH_ELSE;

			long target_label_num = *(long*)ulisp_car(ulisp_cdr(current_exp))->data;
			*(size_t*)(instructions + 1) = ulisp_label_find_rip(&labels, target_label_num);
		}
		else if (ulisp_eq(applied_symbol, bytecode_branch)) {
			uchar* instructions = dynamic_array_push_back(&bytecode, 1 + sizeof(size_t));
			instructions[0] = ULISP_BYTECODE_BRANCH;

			long target_label_num = *(long*)ulisp_car(ulisp_cdr(current_exp))->data;
			*(size_t*)(instructions + 1) = ulisp_label_find_rip(&labels, target_label_num);
		}
		else if (ulisp_eq(applied_symbol, bytecode_lookup_var)) {
			uchar* instructions = dynamic_array_push_back(&bytecode, 1 + sizeof(void*));
			instructions[0] = ULISP_BYTECODE_LOOKUP;

			*(LispObject**)(instructions + 1) = ulisp_car(ulisp_cdr(current_exp));
		}
		else if (ulisp_eq(applied_symbol, bytecode_apply)) {
			uchar* instructions = dynamic_array_push_back(&bytecode, 1);
			*instructions = ULISP_BYTECODE_APPLY;
		}
		else if (ulisp_eq(applied_symbol, bytecode_restore_cont)) {
			uchar* instructions = dynamic_array_push_back(&bytecode, 1);
			*instructions = ULISP_BYTECODE_RESUME_CONT;
		}
		else if (ulisp_eq(applied_symbol, bytecode_fetch_literal)) {
			uchar* instructions = dynamic_array_push_back(&bytecode, 1 + sizeof(uint));
			instructions[0] = ULISP_BYTECODE_FETCH_LITERAL;
			*(uint*)(instructions + 1) = literals.size;

			LispObject** obj = dynamic_array_push_back(&literals, 1);
			*obj = ulisp_car(ulisp_cdr(current_exp));
		}
		else if (ulisp_eq(applied_symbol, bytecode_bind)) {
			uchar* instructions = dynamic_array_push_back(&bytecode, 1 + sizeof(void*));
			instructions[0] = ULISP_BYTECODE_BIND;
			*(void**)(instructions + 1) = ulisp_car(ulisp_cdr(current_exp));
		}
		else if (ulisp_eq(applied_symbol, bytecode_list_bind)) {
			uchar* instructions = dynamic_array_push_back(&bytecode, 1 + sizeof(void*));
			instructions[0] = ULISP_BYTECODE_LIST_BIND;
			*(void**)(instructions + 1) = ulisp_car(ulisp_cdr(current_exp));
		}
		else if (ulisp_eq(applied_symbol, bytecode_fetch_cc)) {
			uchar* instructions = dynamic_array_push_back(&bytecode, 1);
			instructions[0] = ULISP_BYTECODE_FETCH_CC;
		}
		else if (ulisp_eq(applied_symbol, bytecode_set)) {
			uchar* instructions = dynamic_array_push_back(&bytecode, 1 + sizeof(void*));
			instructions[0] = ULISP_BYTECODE_SET;

			*(void**)(instructions + 1) = ulisp_car(ulisp_cdr(current_exp));
		}
		else if (ulisp_eq(applied_symbol, bytecode_label)) {
			continue;
		}
		else {
			printf("operand not understood: ");
			ulisp_print(applied_symbol, ulisp_standard_output);
			panic("\nCompiler error.");
		}
	}

	uchar* end = dynamic_array_push_back(&bytecode, 1);
	*end = ULISP_BYTECODE_END;

	LispTemplate* compiled_code = malloc(sizeof(LispTemplate) + sizeof(LispObject*) * literals.size);
	compiled_code->code = bytecode.data;
	compiled_code->literals_count = 0;
	compiled_code->name = top_level;

	for (uint i = 0; i < literals.size; i++)
		compiled_code->literals[i] = *(LispObject**)dynamic_array_at(&literals, i);

	dynamic_array_destroy(&labels);
	dynamic_array_destroy(&literals);
	return compiled_code;
}

static long ulisp_label_count = 0;

LispObject* ulisp_eval(LispObject* expression);
LispObject* ulisp_compile(LispObject* expression);

LispTemplate* ulisp_compile_lambda(LispObject* expression) {
	LispObject* closure_instructions = ulisp_compile(
		ulisp_cons(begin, ulisp_cdr(ulisp_cdr((CDR(expression))))));

	LispObject* arguments = ulisp_car(ulisp_cdr(ulisp_cdr(expression)));
	LispObject* bind_instructions = nil;

	LispObject* arg;
	for (arg = arguments; arg->type & LISP_CONS; arg = ulisp_cdr(arg)) {
		bind_instructions = ulisp_cons(
			ulisp_cons(bytecode_bind,
					   ulisp_cons(ulisp_car(arg), nil)),
			bind_instructions);
	}

	if (arg != nil) {
		bind_instructions = ulisp_cons(
			ulisp_cons(bytecode_list_bind,
					   ulisp_cons(arg, nil)),
			bind_instructions);
	}

	closure_instructions = ulisp_nconc(ulisp_nreverse(bind_instructions),
									   closure_instructions);
	closure_instructions = ulisp_nconc(closure_instructions,
									   ulisp_cons(ulisp_cons(bytecode_restore_cont, nil), nil));

	LispTemplate* template = ulisp_assembly_compile(closure_instructions);
	template->name = ulisp_car(ulisp_cdr(expression));
	return template;
}

uchar end_byte = ULISP_BYTECODE_END;

LispObject* ulisp_macroexpand(LispObject* expression) {
	if (expression->type & LISP_CONS) {
		if (CAR(expression)->type & LISP_SYMBOL) {
			GLboolean is_found;
			LispObject* macro = ulisp_assoc(read_environnement, CAR(expression), &is_found);

			if (is_found) {
				LispClosure* closure = macro->data;
				ulisp_save_current_continuation(&end_byte);

				envt_register = closure->envt;
				eval_stack = CDR(expression);

				ulisp_run(closure->template);
				return value_register;
			}
		}
	}

	return expression;
}

LispObject* ulisp_compile(LispObject* expression) {
	uint stack_count = 1;
	expression = ulisp_macroexpand(expression);
	stack_push(&gc_stack, expression);

	if (free_space < 50)
		ulisp_gc();

	if (ulisp_eq(expression, tee) || expression == nil) {
		goto fetch_literal;
	}
	else if (expression->type & LISP_SYMBOL) {
		LispObject* lookup_instructions =
			ulisp_cons(
				ulisp_cons(bytecode_lookup_var, ulisp_cons(expression, nil)),
				nil);

		stack_pop(&gc_stack, stack_count);
		return lookup_instructions;
	}
	else if (expression->type & LISP_CONS) {
		LispObject* applied_symbol = CAR(expression);

		if (ulisp_eq(applied_symbol, quote)) {
			expression = ulisp_car(CDR(expression));
			goto fetch_literal;
		}
		else if (ulisp_eq(applied_symbol, iffe)) {
			long if_label_count = ulisp_label_count;
			ulisp_label_count += 2;

			LispObject* test_instructions = ulisp_compile(ulisp_car(CDR(expression)));
			stack_push(&gc_stack, test_instructions);

			LispObject* if_instructions = ulisp_compile(ulisp_car(ulisp_cdr(CDR(expression))));
			stack_push(&gc_stack, if_instructions);

			LispObject* else_instructions = nil;

			if (ulisp_cdr(ulisp_cdr(CDR(expression))) != nil)
				else_instructions = ulisp_compile(ulisp_car(ulisp_cdr(ulisp_cdr(CDR(expression)))));

			stack_push(&gc_stack, else_instructions);

			stack_count += 3;

			LispObject* branch_true_instructions = ulisp_cons(
				ulisp_cons(bytecode_branch_else,
						   ulisp_cons(ulisp_make_integer(if_label_count), nil)),
				nil);

			LispObject* branch_false_instructions = ulisp_cons(
				ulisp_cons(bytecode_branch,
						   ulisp_cons(ulisp_make_integer(if_label_count + 1), nil)),
				ulisp_cons(
					ulisp_cons(bytecode_label,
							   ulisp_cons(ulisp_make_integer(if_label_count), nil)),
					nil));

			LispObject* label_instructions = ulisp_cons(
				ulisp_cons(bytecode_label,
						   ulisp_cons(ulisp_make_integer(if_label_count + 1), nil)),
				nil);

			stack_pop(&gc_stack, stack_count);
			return ulisp_nconc(test_instructions,
							   ulisp_nconc(branch_true_instructions,
										   ulisp_nconc(if_instructions,
													   ulisp_nconc(branch_false_instructions,
																   ulisp_nconc(else_instructions,
																			   label_instructions)))));
		}
		else if (ulisp_eq(applied_symbol, begin)) {
			LispObject* a = ulisp_compile(ulisp_car(CDR(expression)));
			stack_push(&gc_stack, a);

			stack_count += 1;

			for (LispObject* it = ulisp_cdr(CDR(expression)); it != nil; it = ulisp_cdr(it)) {
				a = ulisp_nconc(a, ulisp_compile(ulisp_car(it)));
				stack_push(&gc_stack, a);
				stack_count += 1;
			}

			stack_pop(&gc_stack, stack_count);
			return a;
		}
		else if (ulisp_eq(applied_symbol, named_lambda)) {
			LispTemplate* compiled_lambda = ulisp_compile_lambda(expression);

			long cont_label_count = ulisp_label_count++;

			LispObject* instructions =
				ulisp_list(
					ulisp_list(bytecode_push_cont, ulisp_make_integer(cont_label_count), NULL),
					ulisp_list(bytecode_fetch_literal, compiled_lambda, NULL),
					ulisp_list(bytecode_push, NULL),
					ulisp_list(bytecode_lookup_var, make_closure, NULL),
					ulisp_list(bytecode_apply, NULL),
					ulisp_list(bytecode_label, ulisp_make_integer(cont_label_count), NULL),
					NULL);

			stack_pop(&gc_stack, stack_count);
			return instructions;
		}
		else if (ulisp_eq(applied_symbol, def)) {
			LispObject* symbol = ulisp_car(ulisp_cdr(expression));
			LispObject* object = ulisp_eval(ulisp_car(ulisp_cdr(ulisp_cdr(expression))));

			environnement = ulisp_cons(ulisp_cons(symbol, object),
									   environnement);

			stack_pop(&gc_stack, stack_count);
			return ulisp_cons(ulisp_cons(bytecode_fetch_literal,
										 ulisp_cons(symbol, nil)),
							  nil);
		}
		else if (ulisp_eq(applied_symbol, def_macro)) {
			LispObject* symbol = ulisp_car(ulisp_cdr(expression));
			LispObject* object = ulisp_eval(ulisp_car(ulisp_cdr(ulisp_cdr(expression))));

			read_environnement = ulisp_cons(ulisp_cons(symbol, object),
											read_environnement);

			stack_pop(&gc_stack, stack_count);
			return nil;
		}
		else if (ulisp_eq(applied_symbol, call_cc)) {
			long cont_label_count = ulisp_label_count++;

			LispObject* compiled_instructions = ulisp_compile(ulisp_car(CDR(expression)));

			LispObject* instructions = ulisp_cons(
				ulisp_cons(bytecode_push_cont,
						   ulisp_cons(ulisp_make_integer(cont_label_count), nil)),
				ulisp_cons(ulisp_cons(bytecode_fetch_cc, nil),
						   ulisp_cons(ulisp_cons(bytecode_push, nil),
									  ulisp_nconc(compiled_instructions,
												  ulisp_cons(ulisp_cons(bytecode_apply, nil),
															 ulisp_cons(ulisp_cons(bytecode_label,
																				   ulisp_cons(ulisp_make_integer(cont_label_count), nil)),
																		nil))))));

			stack_pop(&gc_stack, stack_count);
			return instructions;
		}
		else if (ulisp_eq(applied_symbol, set)) {
			LispObject* value_instructions = ulisp_compile(ulisp_car(ulisp_cdr(ulisp_cdr(expression))));

			stack_pop(&gc_stack, stack_count);
			return ulisp_nconc(value_instructions,
							   ulisp_cons(ulisp_list(bytecode_set, ulisp_car(ulisp_cdr(expression)), NULL), nil));
		}
		else {
			long cont_label_count;
			LispObject *arguments, *instructions;

			cont_label_count = ulisp_label_count++;

			arguments = ulisp_nreverse(expression);

			instructions = ulisp_cons(
				ulisp_cons(bytecode_push_cont,
						   ulisp_cons(ulisp_make_integer(cont_label_count), nil)),
				nil);

			stack_push(&gc_stack, arguments);
			stack_count += 1;

			for (LispObject* a = arguments; a != nil; a = ulisp_cdr(a)) {
				stack_push(&gc_stack, instructions);
				stack_count++;
				instructions = ulisp_nconc(instructions, ulisp_compile(ulisp_car(a)));

				if (ulisp_cdr(a) != nil) {
					instructions = ulisp_nconc(instructions, ulisp_cons(ulisp_cons(bytecode_push, nil), nil));
				}
			}

			LispObject* final_result =
				ulisp_nconc(instructions, ulisp_cons(
								ulisp_cons(bytecode_apply, nil),
								ulisp_cons(ulisp_cons(bytecode_label,
													   ulisp_cons(ulisp_make_integer(cont_label_count), nil)),
										   nil)));

			stack_pop(&gc_stack, stack_count);
			return final_result;
		}
	}
	else {
		LispObject* fetch_instructions;

	fetch_literal:
		fetch_instructions = ulisp_cons(
			ulisp_cons(bytecode_fetch_literal, ulisp_cons(expression, nil)),
			nil);

		stack_pop(&gc_stack, stack_count);
		return fetch_instructions;
	}
}

LispObject* ulisp_eval(LispObject* expression) {
	stack_push(&gc_stack, expression);

	if (free_space < 20)
		ulisp_gc();

	LispObject* compiled = ulisp_compile(expression);
	ulisp_run(ulisp_assembly_compile(compiled));
	stack_pop(&gc_stack, 1);

	printf("Stack size: %ld\n", gc_stack.top);

	return value_register;
}

LispObject* ulisp_read_list(const char* string) {
	LispObject* list = nil;
	size_t string_size = strlen(string);

	int i = string_size - 1;

	while (i >= 0) {
		stack_push(&gc_stack, list);

		if (free_space < 20)
			ulisp_gc();

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

				ASSERT(j >= 0);

				count++;
				ASSERT(parens_count >= 0);
			} while(!(string[j--] == '(' && parens_count == 0));

			list = ulisp_cons(ulisp_read_list(m_strndup(string + j + 2, count - 2)), list);
			i -= count;
		}
		else {
			int count = 0;
			int j = i;

			for (; !IS_BLANK(string[j]); j--) {
				ASSERT(string[j] != '(');

				if (string[j] == '\0')
					break;

				count++;
			}

			if (count == 0)
				break;

			char* symbol_string = m_strndup(string + j + 1, count);

			if (strcmp(symbol_string, ".") == 0) { /* Dotted list */
				ASSERT(ulisp_cdr(list) == nil);
				list = ulisp_car(list);
			}
			else {
				long integer;
				double floating;

				int type = parse_number(symbol_string, &integer, &floating);

				if (type == 0)
					list = ulisp_cons(ulisp_make_integer(integer), list);
				else if (type == 1)
					list = ulisp_cons(ulisp_make_float(floating), list);
				else
					list = ulisp_cons(ulisp_make_symbol(symbol_string), list);
			}

			free(symbol_string);

			i -= count;
		}

		stack_pop(&gc_stack, 1);
	}

	return list;
}

LispObject* ulisp_read(const char* string) {
	return ulisp_car(ulisp_read_list(string));
}

char* ulisp_debug_print(LispObject* obj) {
	LispObject* stream = ulisp_make_stream(NULL);
	ulisp_print(obj, stream);

	return ulisp_stream_finish_output(stream);
}

void ulisp_print(LispObject* obj, LispObject* stream) {
	if (obj->type & LISP_SYMBOL) {
		ulisp_stream_format(stream, "%s", ulisp_symbol_string(obj));
	}
	else if (obj->type & LISP_CONS) {
		ulisp_stream_format(stream, "(");

		LispObject* c = obj;
		for(; c->type & LISP_CONS; c = ulisp_cdr(c)) {
			ulisp_print(ulisp_car(c), stream);

			if (ulisp_cdr(c)->type & LISP_CONS)
				ulisp_stream_format(stream, " ");
		}

		if (!(c == nil)) {
			ulisp_stream_format(stream, " . ");
			ulisp_print(c, stream);
		}

		ulisp_stream_format(stream, ")");
	}
	else if (obj->type & LISP_PROC_BUILTIN) {
		LispBuiltinProc* proc = obj->data;

		ulisp_stream_format(stream, "<BUILTIN-PROC %s at %p>", ulisp_symbol_string(proc->name), proc);
	}
	else if (obj->type & LISP_PROC) {
		LispClosure* proc = (LispClosure*)obj->data;
		ulisp_stream_format(stream, "<PROC %s at %p>", ulisp_debug_print(proc->template->name), proc);
	}
	else if (obj->type & LISP_INTEGER) {
		ulisp_stream_format(stream, "%ld", *(long*)obj->data);
	}
	else if (obj->type & LISP_FLOAT) {
		ulisp_stream_format(stream, "%gf", *(double*)obj->data);
	}
	else {
		ulisp_stream_format(stream, "#<NOT PRINTABLE>");
	}
}
