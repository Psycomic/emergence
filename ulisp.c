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
#define AS(o, t) ((t *)(o)->data)
#define ASSERT(v) if (!(v)) { ulisp_throw(ulisp_make_symbol("assertion: "#v" in " __FILE__)); }
#define S_OFFSET(s, slot) (size_t)(&((s*)NULL)->slot)

#define LIST_ITERATE(i, list) for (LispObject* i = list; i != nil; i = ulisp_cdr(i))

LispObject *environnement, *read_environnement, /* Global environnements */
	*nil = NULL, *tee, *quote, *iffe, *begin, *named_lambda, *def, *rest, *def_macro, *call_cc,
	*quasiquote, *unquote, *set, *standard_output, *let, *letrec, /* keywords */
	*assertion_symbol, *top_level, *make_closure;

#define DEFAULT_WORKSPACESIZE (2 << 20)

static uint64_t workspace_size = DEFAULT_WORKSPACESIZE;
static uint64_t free_space = DEFAULT_WORKSPACESIZE;
static uint32_t cons_cell_size = sizeof(LispObject) + sizeof(ConsCell);
static LispObject* free_list;
static uchar* workspace;
static Stack gc_stack;

DynamicArray allocated_pointers;

static LispObject* eval_stack;
static LispObject* envt_register;
static LispObject* current_continuation;
static LispObject* last_called_proc = NULL;
static LispObject* template_register;
static uchar* ulisp_rip;

static LispObject** exception_stack = NULL;

LispObject* ulisp_car(LispObject* pair);
LispObject* ulisp_cdr(LispObject* object);
void ulisp_apply();
void ulisp_throw(LispObject* exception);
uint ulisp_length(LispObject* list);
LispObject* ulisp_nreverse(LispObject* obj);
LispObject* ulisp_make_symbol(const char* string);

typedef struct LispLabel {
	long id;
	size_t offset;
} LispLabel;

void custom_allocator_init() {
	DYNAMIC_ARRAY_CREATE(&allocated_pointers, LispObject*);
}

void* custom_allocator_alloc(size_t size) {
	LispObject* p = malloc(size);

	LispObject** push_pointer = dynamic_array_push_back(&allocated_pointers, 1);
	*push_pointer = p;

	return p;
}

void custom_allocator_sweep() {
	DynamicArray new_allocated_pointers;
	DYNAMIC_ARRAY_CREATE(&new_allocated_pointers, LispObject*);

	uint freed_count = 0;

	for (uint i = 0; i < allocated_pointers.size; i++) {
		LispObject* obj = *(LispObject**)dynamic_array_at(&allocated_pointers, i);

		if (!(obj->type & GC_MARKED)) {
			free(obj);
			freed_count++;
		}
		else {
			obj->type &= ~GC_MARKED;
			*(LispObject**)dynamic_array_push_back(&new_allocated_pointers, 1) = obj;
		}
	}

	printf("Freed %u allocations!\n", freed_count);
	dynamic_array_destroy(&allocated_pointers);
	allocated_pointers = new_allocated_pointers;
}

void free_list_init() {
	workspace = malloc(cons_cell_size * free_space);
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

	if (!(object->type & LISP_SYMBOL))
		object->type |= GC_MARKED;

	if (object->type & LISP_CONS) {
		mark_object(ulisp_car(object));
		object = ulisp_cdr(object);
		goto mark;
	}
	else if (object->type & LISP_PROC) {
		LispClosure* proc = object->data;

		if (proc->envt)
			mark_object(proc->envt);

		mark_object(proc->template);
	}
	else if (object->type & LISP_CONTINUATION) {
		LispContinuation* cont = object->data;

		if (cont->envt_register)
			mark_object(cont->envt_register);

		mark_object(cont->eval_stack);
		mark_object(cont->current_template);

		if (cont->previous_cont)
			mark_object(cont->previous_cont);
	}
	else if (object->type & LISP_TEMPLATE) {
		LispTemplate* template = AS(object, LispTemplate);

		for (uint i = 0; i < template->literals_count; i++)
			mark_object(template->literals[i]);
	}
	else if (object->type & LISP_ARRAY) {
		LispArray* array = AS(object, LispArray);

		for (uint i = 0; i < array->size; i++) {
			mark_object(array->data[i]);
		}
	}
	else if (object->type & LISP_FRAME) {
		LispFrame* frame = AS(object, LispFrame);

		for (uint i = 0; i < frame->bindings_count; i++) {
			mark_object(frame->bindings[i]);
		}

		if (frame->scope)
			mark_object(frame->scope);
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

	if (envt_register)
		mark_object(envt_register);

	mark_object(value_register);
	mark_object(eval_stack);

	if (current_continuation)
		mark_object(current_continuation);

	mark_object(template_register);

	custom_allocator_sweep();
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

	symbol->hash = hash_string((uchar*)string);

	if (nil && symbol->hash == ((LispSymbol*)nil->data)->hash)
		return nil;

	return new_object;
}

char* ulisp_symbol_string(LispObject* symbol) {
	ASSERT(symbol->type & LISP_SYMBOL);

	return ((LispSymbol*)symbol->data)->str;
}

LispObject* ulisp_make_integer(long val) {
	LispObject* new_object = custom_allocator_alloc(sizeof(LispObject) + sizeof(long));

	new_object->type = LISP_INTEGER | LISP_NUMBER;
	*(long*)new_object->data = val;

	return new_object;
}

LispObject* ulisp_make_float(double val) {
	LispObject* new_object = custom_allocator_alloc(sizeof(LispObject) + sizeof(double));

	new_object->type = LISP_FLOAT | LISP_NUMBER;
	*(double*)new_object->data = val;

	return new_object;
}

LispObject* ulisp_make_continuation(LispObject* envt_register, LispObject* eval_stack, LispObject* current_cont, LispObject* current_template, uchar* rip) {
	LispObject* new_cont_obj = custom_allocator_alloc(sizeof(LispObject) + sizeof(LispContinuation));
	new_cont_obj->type = LISP_CONTINUATION;

	LispContinuation* cont = (LispContinuation*)new_cont_obj->data;

	cont->envt_register = envt_register;
	cont->eval_stack = eval_stack;
	cont->previous_cont = current_cont;
	cont->current_template = current_template;
	cont->rip = rip;

	return new_cont_obj;
}

LispObject* ulisp_make_file_stream(FILE* f) {
	LispObject* new_stream_obj = custom_allocator_alloc(sizeof(LispObject) + sizeof(LispFileStream));
	new_stream_obj->type = LISP_STREAM | LISP_FILE_STREAM;

	LispFileStream* stream = new_stream_obj->data;
	stream->f = f;

	return new_stream_obj;
}

LispObject* ulisp_make_string_stream() {
	LispObject* new_stream_obj = custom_allocator_alloc(sizeof(LispObject) + sizeof(LispStringStream));
	new_stream_obj->type = LISP_STREAM | LISP_STRING_STREAM;

	LispStringStream* stream = new_stream_obj->data;

	stream->buffer = malloc(8);
	stream->size = 0;
	stream->capacity = 8;

	return new_stream_obj;
}

LispObject* ulisp_make_array(LispObject* dimensions, LispObject* initial_element, LispObject* displaced_to) {
	ASSERT(dimensions->type & LISP_CONS || dimensions->type & LISP_INTEGER);
	ASSERT(displaced_to == nil || displaced_to->type & LISP_ARRAY);

	uint length;
	if (dimensions->type & LISP_INTEGER)
		length = 1;
	else
		length = ulisp_length(dimensions);

	LispObject* obj = custom_allocator_alloc(sizeof(LispObject) + sizeof(LispArray) +
											 sizeof(long) * length);
	obj->type = LISP_ARRAY;
	LispArray* array = obj->data;
	array->dimensions_count = length;

	size_t size = 1;
	if (dimensions->type & LISP_INTEGER) {
		size = *AS(dimensions, long);
		array->dimensions[0] = *AS(dimensions, long);
	}
	else {
		uint j = 0;
		LIST_ITERATE(i, dimensions) {
			size *= *AS(ulisp_car(i), long);
			array->dimensions[j++] = *AS(ulisp_car(i), long);
		}
	}

	array->size = size;
	array->capacity = size;

	if (displaced_to != nil) {
		array->data = AS(displaced_to, LispArray)->data;
	}
	else {
		array->data = malloc(sizeof(LispObject*) * size);

		for (uint i = 0; i < size; i++) {
			array->data[i] = initial_element;
		}
	}

	return obj;
}

LispObject* ulisp_array_ref(LispObject* array_obj, LispObject* indexes) {
	ASSERT(array_obj->type & LISP_ARRAY);
	ASSERT(indexes->type & LISP_CONS);

	LispArray* array = AS(array_obj, LispArray);

	uint final_index = 0;
	uint i = 0;
	uint multiplier = 1;

	LIST_ITERATE(index, indexes) {
		ASSERT(i < array->dimensions_count);
		ASSERT(ulisp_car(index)->type & LISP_INTEGER);

		long idx = *AS(ulisp_car(index), long);
		ASSERT(idx < array->dimensions[i]);

		final_index += idx * multiplier;
		multiplier *= array->dimensions[i];
		i++;
	}

	ASSERT(i == array->dimensions_count);

	return array->data[final_index];
}

LispObject* ulisp_array_set(LispObject* array_obj, LispObject* value, LispObject* indexes) {
	ASSERT(array_obj->type & LISP_ARRAY);
	ASSERT(indexes->type & LISP_CONS);

	LispArray* array = AS(array_obj, LispArray);

	uint final_index = 0;
	uint i = 0;
	uint multiplier = 1;

	LIST_ITERATE(index, indexes) {
		ASSERT(i < array->dimensions_count);

		long idx = *AS(ulisp_car(index), long);
		ASSERT(idx < array->dimensions[i]);

		final_index += idx * multiplier;
		multiplier *= array->dimensions[i];
		i++;
	}

	ASSERT(i == array->dimensions_count);
	array->data[final_index] = value;
	return value;
}

LispObject* ulisp_array_dimensions(LispObject* array_obj) {
	LispArray* array = AS(array_obj, LispArray);
	if (array->dimensions_count == 1)
		return ulisp_make_integer(array->dimensions[0]);

	LispObject* dims = nil;
	for (int i = array->dimensions_count - 1; i >= 0; i--) {
		dims = ulisp_cons(ulisp_make_integer(array->dimensions[i]),
						  dims);
	}

	return dims;
}

void ulisp_stream_write(char* s, LispObject* stream_obj) {
	ASSERT(stream_obj->type & LISP_STREAM);

	size_t size = strlen(s);

	if (stream_obj->type & LISP_STRING_STREAM) {
		GLboolean resized = GL_FALSE;
		LispStringStream* stream = stream_obj->data;

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
	else {
		LispFileStream* stream = stream_obj->data;
		fwrite(s, size, 1, stream->f);
	}
}

char* ulisp_stream_finish_output(LispObject* stream_obj) {
	assert(stream_obj->type & LISP_STREAM);

	if (stream_obj->type & LISP_STRING_STREAM) {
		LispStringStream* stream = stream_obj->data;

		stream->buffer[stream->size] = '\0';
		return stream->buffer;
	}
	else {
		LispFileStream* stream = stream_obj->data;

		fflush(stream->f);
		return NULL;
	}
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

LispObject* ulisp_make_closure(LispObject* template, LispObject* envt) {
	LispObject* obj = custom_allocator_alloc(sizeof(LispObject) + sizeof(LispClosure));
	obj->type = LISP_PROC;

	LispClosure* closure = obj->data;

	closure->template = template;
	closure->envt = envt;

	return obj;
}

LispObject* ulisp_prim_make_closure(LispObject* arguments) {
	LispObject* template = ulisp_car(arguments);
	return ulisp_make_closure(template, envt_register);
}

LispObject* ulisp_prim_make_array(LispObject* arguments) {
	return ulisp_make_array(ulisp_car(arguments), ulisp_car(ulisp_cdr(arguments)),
							ulisp_cdr(ulisp_cdr(arguments)) != nil ? ulisp_car(ulisp_cdr(ulisp_cdr(arguments))) : nil);
}

LispObject* ulisp_prim_array_ref(LispObject* arguments) {
	return ulisp_array_ref(ulisp_car(arguments), ulisp_cdr(arguments));
}

LispObject* ulisp_prim_array_set(LispObject* arguments) {
	return ulisp_array_set(ulisp_car(arguments), ulisp_car(ulisp_cdr(arguments)),
						   ulisp_cdr(ulisp_cdr(arguments)));
}

LispObject* ulisp_prim_array_total_size(LispObject* arguments) {
	return ulisp_make_integer(AS(ulisp_car(arguments), LispArray)->size);
}

LispObject* ulisp_prim_array_dimensions(LispObject* arguments) {
	return ulisp_array_dimensions(ulisp_car(arguments));
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

LispObject** ulisp_assoc(LispObject* plist, LispObject* symbol) {
	ASSERT(plist->type & LISP_LIST);
	ASSERT(symbol->type & LISP_SYMBOL);

	for (; plist != nil; plist = ulisp_cdr(plist)) {
		LispObject* temp = ulisp_car(plist);

		if (ulisp_eq(ulisp_car(temp), symbol)) {
			return &CDR(temp);
		}
	}

	return NULL;
}

LispObject* ulisp_map(LispObject* (*fn)(LispObject*), LispObject* list) {
	ASSERT(list->type & LISP_LIST);

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
		return a;

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
	LispObject* proc = custom_allocator_alloc(sizeof(LispObject) + sizeof(LispBuiltinProc));
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
	long b = *(long*)ulisp_car(ulisp_cdr(args))->data;
	long r = *(long*)ulisp_car(args)->data % b;

	return ulisp_make_integer(r < 0 ? r + b : r);
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
			name =  AS(AS(last_called_proc, LispClosure)->template, LispTemplate)->name;

		printf("0 -> ");
		ulisp_print(ulisp_cons(name, eval_stack), ulisp_standard_output);
		printf("\n");

		i = 1;
	}

	LispObject* last_cont = NULL;

	for (LispObject* cont = current_continuation; cont != NULL; cont = ((LispContinuation*)cont->data)->previous_cont) {
		LispContinuation* continuation = cont->data;

		printf("%d -> (", i++);
		LispObject* name = AS(continuation->current_template, LispTemplate)->name;
		ulisp_print(name, ulisp_standard_output);

		if (continuation->envt_register) {
			LispFrame* frame = AS(continuation->envt_register, LispFrame);

			for (uint i = 0; i < frame->bindings_count; i++) {
				printf(" ");
				ulisp_print(frame->bindings[i], ulisp_standard_output);
			}
		}

		printf(")\n");

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
	*bytecode_fetch_cc, *bytecode_set_local, *bytecode_set_global, *bytecode_binding_lookup,
	*bytecode_unbind, *bytecode_list_bind;

LispObject* ulisp_standard_output;

void ulisp_init(void) {
	stack_init(&gc_stack);
	custom_allocator_init();

	ulisp_run_level_top = 0;

	nil = ulisp_make_symbol("nil");
	nil->type |= LISP_LIST;

	value_register = nil;
	eval_stack = nil;
	template_register = NULL;
	envt_register = NULL;
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
	bytecode_set_local = ulisp_make_symbol("set-local");
	bytecode_set_global = ulisp_make_symbol("set-global");
	bytecode_binding_lookup = ulisp_make_symbol("bdg-get");
	bytecode_unbind = ulisp_make_symbol("unbind");
	bytecode_list_bind = ulisp_make_symbol("list-bind");

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
	standard_output = ulisp_make_symbol("*standard-output*");
	let = ulisp_make_symbol("let");
	letrec = ulisp_make_symbol("letrec");

	free_list_init();

	LispObject* exception_stack_pair = ulisp_cons(ulisp_make_symbol("exception-stack"),
												  nil);

	ulisp_standard_output = ulisp_make_file_stream(stdout);

	environnement = nil;
	environnement = ulisp_cons(ulisp_cons(nil, nil),
							   ulisp_cons(ulisp_cons(tee, tee),
										  ulisp_cons(exception_stack_pair,
													 ulisp_cons(ulisp_cons(standard_output, ulisp_standard_output),
																environnement))));

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
	env_push_fun("make-array", ulisp_prim_make_array);
	env_push_fun("array-ref", ulisp_prim_array_ref);
	env_push_fun("array-set!", ulisp_prim_array_set);
	env_push_fun("array-dimensions", ulisp_prim_array_dimensions);
	env_push_fun("array-total-size", ulisp_prim_array_total_size);

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
		ulisp_rip = AS(closure->template, LispTemplate)->code;
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

LispObject* ulisp_frame_create(LispObject* scope, uint size) {
	LispObject* frame_obj = malloc(sizeof(LispObject) + sizeof(LispFrame) + sizeof(LispObject*) * size);
	frame_obj->type = LISP_FRAME;

	LispFrame* frame = AS(frame_obj, LispFrame);

	frame->scope = scope;
	frame->bindings_count = size;

	return frame_obj;
}

void ulisp_frame_bind(uint count) {
	LispObject* new_frame_obj = ulisp_frame_create(envt_register, count);
	LispFrame* new_frame = AS(new_frame_obj, LispFrame);

	uint i = 0;

	LIST_ITERATE(value, eval_stack) {
		new_frame->bindings[i++] = CAR(value);
	}

	eval_stack = nil;
	envt_register = new_frame_obj;
}

void ulisp_frame_list_bind(uint count) {
	LispObject* new_frame_obj = ulisp_frame_create(envt_register, count);
	LispFrame* new_frame = AS(new_frame_obj, LispFrame);

	uint i = 0;

	LispObject* value = eval_stack;
	for (i = 0; i < count - 1; i++) {
		new_frame->bindings[i] = CAR(value);
		value = CDR(value);
	}

	new_frame->bindings[i] = value;

	eval_stack = nil;
	envt_register = new_frame_obj;
}

void ulisp_frame_set(uint index, uint scope) {
	LispFrame* frame = AS(envt_register, LispFrame);
	for (uint i = 0; i < scope; i++) {
		frame = AS(frame->scope, LispFrame);
	}

	frame->bindings[index] = value_register;
}

LispObject* ulisp_frame_get(uint index, uint scope) {
	LispFrame* frame = AS(envt_register, LispFrame);
	for (uint i = 0; i < scope; i++) {
		frame = AS(frame->scope, LispFrame);
	}

	return frame->bindings[index];
}

void ulisp_run(LispObject* template) {
	ulisp_rip = AS(template, LispTemplate)->code;
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
		value_register = ulisp_frame_get(
			*(uint*)(ulisp_rip + 1),
			*(uint*)(ulisp_rip + sizeof(uint) + 1));
		ulisp_rip += 1 + sizeof(uint) + sizeof(uint);
		break;
	case ULISP_BYTECODE_BINDING_LOOKUP:
		value_register = **(LispObject***)(ulisp_rip + 1);
		ulisp_rip += 1 + sizeof(long);
		break;
	case ULISP_BYTECODE_PUSH_CONT:
		ulisp_save_current_continuation(AS(template_register, LispTemplate)->code + *(size_t*)(ulisp_rip + 1));
		ulisp_rip += sizeof(void*) + 1;
		break;
	case ULISP_BYTECODE_RESUME_CONT:
		ulisp_restore_continuation();
		break;
	case ULISP_BYTECODE_BRANCH_IF:
		if (value_register == nil)
			ulisp_rip += sizeof(size_t) + 1;
		else
			ulisp_rip = AS(template_register, LispTemplate)->code + *(size_t*)(ulisp_rip + 1);
		break;
	case ULISP_BYTECODE_BRANCH_ELSE:
		if (value_register == nil)
			ulisp_rip = AS(template_register, LispTemplate)->code + *(size_t*)(ulisp_rip + 1);
		else
			ulisp_rip += sizeof(size_t) + 1;
		break;
	case ULISP_BYTECODE_BRANCH:
		ulisp_rip = AS(template_register, LispTemplate)->code + *(size_t*)(ulisp_rip + 1);
		break;
	case ULISP_BYTECODE_PUSH_EVAL:
		ulisp_eval_stack_push();
		ulisp_rip++;
		break;
	case ULISP_BYTECODE_FETCH_LITERAL:
		value_register = AS(template_register, LispTemplate)->literals[*(uint*)(ulisp_rip + 1)];
		ulisp_rip += sizeof(uint) + 1;
		break;
	case ULISP_BYTECODE_FETCH_CC:
		value_register = current_continuation;
		ulisp_rip++;
		break;
	case ULISP_BYTECODE_BIND:
		ulisp_frame_bind(*(uint*)(ulisp_rip + 1));
		ulisp_rip += sizeof(uint) + 1;
		break;
	case ULISP_BYTECODE_LIST_BIND:
		ulisp_frame_list_bind(*(uint*)(ulisp_rip + 1));
		ulisp_rip += sizeof(uint) + 1;
		break;
	case ULISP_BYTECODE_UNBIND:
		envt_register = AS(envt_register, LispFrame)->scope;
		ulisp_rip++;
		break;
	case ULISP_BYTECODE_SET_LOCAL:
		ulisp_frame_set(*(uint*)(ulisp_rip + 1), *(uint*)(ulisp_rip + 1 + sizeof(uint)));
		ulisp_rip += sizeof(uint) * 2 + 1;
		break;
	case ULISP_BYTECODE_SET_GLOBAL:
		**(LispObject***)(ulisp_rip + 1) = value_register;
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

		if (ulisp_eq(instruction, bytecode_lookup_var)		||
			ulisp_eq(instruction, bytecode_set_local)		||
			ulisp_eq(instruction, bytecode_set_global)		||
			ulisp_eq(instruction, bytecode_binding_lookup)	||
			ulisp_eq(instruction, bytecode_push_cont))
		{
			offset += 1 + sizeof(void*);
		}
		else if (ulisp_eq(instruction, bytecode_apply)			||
				 ulisp_eq(instruction, bytecode_restore_cont)	||
				 ulisp_eq(instruction, bytecode_push)			||
				 ulisp_eq(instruction, bytecode_unbind)			||
				 ulisp_eq(instruction, bytecode_fetch_cc))
		{
			offset += 1;
		}
		else if (ulisp_eq(instruction, bytecode_fetch_literal)	||
				 ulisp_eq(instruction, bytecode_bind)			||
				 ulisp_eq(instruction, bytecode_list_bind))
		{
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

uint ulisp_literals_add(DynamicArray* literals, LispObject* obj) {
	for (uint i = 0; i < literals->size; i++) {
		LispObject* l = *(LispObject**)dynamic_array_at(literals, i);
		if (l == obj)
			return i;
	}

	LispObject** new_lit = dynamic_array_push_back(literals, 1);
	*new_lit = obj;
	return literals->size - 1;
}

LispObject* ulisp_assembly_compile(LispObject* expressions) {
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
			uchar* instructions = dynamic_array_push_back(&bytecode, 1 + sizeof(uint) + sizeof(uint));
			instructions[0] = ULISP_BYTECODE_LOOKUP;

			*(uint*)(instructions + 1) = *AS(ulisp_car(ulisp_cdr(current_exp)), long);
			*(uint*)(instructions + 1 + sizeof(uint)) = *AS(ulisp_car(ulisp_cdr(ulisp_cdr(current_exp))), long);
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
			*(uint*)(instructions + 1) = ulisp_literals_add(&literals, ulisp_car(ulisp_cdr(current_exp)));
		}
		else if (ulisp_eq(applied_symbol, bytecode_bind)) {
			uchar* instructions = dynamic_array_push_back(&bytecode, 1 + sizeof(uint));
			instructions[0] = ULISP_BYTECODE_BIND;
			*(uint*)(instructions + 1) = *AS(ulisp_car(ulisp_cdr(current_exp)), long);
		}
		else if (ulisp_eq(applied_symbol, bytecode_list_bind)) {
			uchar* instructions = dynamic_array_push_back(&bytecode, 1 + sizeof(uint));
			instructions[0] = ULISP_BYTECODE_LIST_BIND;
			*(uint*)(instructions + 1) = *AS(ulisp_car(ulisp_cdr(current_exp)), long);
		}
		else if (ulisp_eq(applied_symbol, bytecode_unbind)) {
			uchar* instructions = dynamic_array_push_back(&bytecode, 1);
			instructions[0] = ULISP_BYTECODE_UNBIND;
		}
		else if (ulisp_eq(applied_symbol, bytecode_binding_lookup)) {
			uchar* instructions = dynamic_array_push_back(&bytecode, 1 + sizeof(long));
			instructions[0] = ULISP_BYTECODE_BINDING_LOOKUP;
			long* value = AS(ulisp_car(ulisp_cdr(current_exp)), long);

			*(long*)(instructions + 1) = *value;
		}
		else if (ulisp_eq(applied_symbol, bytecode_fetch_cc)) {
			uchar* instructions = dynamic_array_push_back(&bytecode, 1);
			instructions[0] = ULISP_BYTECODE_FETCH_CC;
		}
		else if (ulisp_eq(applied_symbol, bytecode_set_local)) {
			uchar* instructions = dynamic_array_push_back(&bytecode, 1 + sizeof(uint) * 2);
			instructions[0] = ULISP_BYTECODE_SET_LOCAL;

			*(uint*)(instructions + 1) = *AS(ulisp_car(ulisp_cdr(current_exp)), long);
			*(uint*)(instructions + 1 + sizeof(uint)) = *AS(ulisp_car(ulisp_cdr(ulisp_cdr(current_exp))), long);
		}
		else if (ulisp_eq(applied_symbol, bytecode_set_global)) {
			uchar* instructions = dynamic_array_push_back(&bytecode, 1 + sizeof(void*));
			instructions[0] = ULISP_BYTECODE_SET_GLOBAL;

			*(long*)(instructions + 1) = *AS(ulisp_car(ulisp_cdr(current_exp)), long);
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

	LispObject* template = custom_allocator_alloc(sizeof(LispObject) + sizeof(LispTemplate) +
												  sizeof(LispObject*) * literals.size);
	template->type = LISP_TEMPLATE;

	LispTemplate* compiled_code = AS(template, LispTemplate);

	compiled_code->code = bytecode.data;
	compiled_code->literals_count = literals.size;
	compiled_code->name = top_level;

	for (uint i = 0; i < literals.size; i++)
		compiled_code->literals[i] = *(LispObject**)dynamic_array_at(&literals, i);

	dynamic_array_destroy(&labels);
	dynamic_array_destroy(&literals);
	return template;
}

static long ulisp_label_count = 0;

LispObject* ulisp_eval(LispObject* expression);
LispObject* ulisp_compile(LispObject* expression, LispObject* comptime_environnement);

LispObject* ulisp_compile_lambda(LispObject* expression, LispObject* comptime_environnement) {
	LispObject* arguments = ulisp_car(ulisp_cdr(ulisp_cdr(expression)));
	uint arguments_count = 0;

	LispObject* bind_instructions = nil;
    LispObject* new_env = envt_register;

	LispObject* arg;
	for (arg = arguments; arg->type & LISP_CONS; arg = CDR(arg)) {
		arguments_count++;
	}

	LispObject* bind_symbol = bytecode_bind;
	if (arg->type & LISP_SYMBOL && arg != nil) {
		bind_symbol = bytecode_list_bind;
		arguments_count++;
	}

	if (arguments_count != 0) {
		bind_instructions = ulisp_cons(
			ulisp_list(bind_symbol, ulisp_make_integer(arguments_count), NULL),
			nil);

		new_env = ulisp_frame_create(comptime_environnement, arguments_count);

		LispFrame* new_env_frame = AS(new_env, LispFrame);

		uint i = 0;
		for (arg = arguments; arg->type & LISP_CONS; arg = CDR(arg)) {
			new_env_frame->bindings[i++] = ulisp_car(arg);
		}

		if (arg->type & LISP_SYMBOL && arg != nil) {
			new_env_frame->bindings[i] = arg;
		}
	}

	stack_push(&gc_stack, bind_instructions);

	LispObject* closure_instructions = ulisp_compile(
		ulisp_cons(begin, ulisp_cdr(ulisp_cdr(CDR(expression)))),
		new_env);

	stack_pop(&gc_stack, 1);

	closure_instructions = ulisp_nconc(bind_instructions, closure_instructions);
	closure_instructions = ulisp_nconc(closure_instructions, ulisp_cons(ulisp_cons(bytecode_restore_cont, nil), nil));

	LispObject* name = ulisp_car(ulisp_cdr(expression));

	printf("%s instructions: \n", ulisp_symbol_string(name));
	ulisp_print(closure_instructions, ulisp_standard_output);
	printf("\n");

	LispObject* template = ulisp_assembly_compile(closure_instructions);
	AS(template, LispTemplate)->name = name;
	return template;
}

static uchar end_byte = ULISP_BYTECODE_END;

LispObject* ulisp_macroexpand(LispObject* expression) {
	if (expression->type & LISP_CONS) {
		if (CAR(expression)->type & LISP_SYMBOL) {
			LispObject** macro_binding = ulisp_assoc(read_environnement, CAR(expression));

			if (macro_binding != NULL) {
				LispClosure* closure = AS(*macro_binding, LispClosure);
				ulisp_save_current_continuation(&end_byte);

				envt_register = closure->envt;
				eval_stack = CDR(expression);

				ulisp_run(closure->template);
				ulisp_print(value_register, ulisp_standard_output);
				printf("\n");

				return ulisp_macroexpand(value_register);
			}
		}
	}

	return expression;
}

int ulisp_comptime_local_lookup(LispObject* symbol, LispObject* comptime_environnement, uint* out_i, uint* out_j) {
	*out_j = 0;

	for (LispObject* frame = comptime_environnement; frame != NULL; frame = AS(frame, LispFrame)->scope) {
		LispFrame* f = AS(frame, LispFrame);
		for (*out_i = 0; *out_i < f->bindings_count; (*out_i)++) {
			if (ulisp_eq(f->bindings[*out_i], symbol)) {
				return 0;
			}
		}

		(*out_j)++;
	}

	return -1;
}

LispObject** ulisp_toplevel_binding_create(LispObject* symbol) {
	LispObject* pair = ulisp_cons(symbol, nil);
	environnement = ulisp_cons(pair, environnement);
	return &CDR(pair);
}

LispObject* ulisp_compile(LispObject* expression, LispObject* comptime_environnement) {
	uint stack_count = 1;
	expression = ulisp_macroexpand(expression);
	stack_push(&gc_stack, expression);

	if (free_space < 50)
		ulisp_gc();

	if (ulisp_eq(expression, tee) || expression == nil) {
		goto fetch_literal;
	}
	else if (expression->type & LISP_SYMBOL) {
		uint i, j;
		LispObject* lookup_instructions;

		if (ulisp_comptime_local_lookup(expression, comptime_environnement, &i, &j) == 0) {
			lookup_instructions =
				ulisp_list(
					ulisp_list(bytecode_lookup_var,
							   ulisp_make_integer(i), ulisp_make_integer(j), NULL),
					NULL);
		}
		else {
			LispObject** toplevel_binding = ulisp_assoc(environnement, expression);

			if (toplevel_binding == NULL)
				toplevel_binding = ulisp_toplevel_binding_create(expression);

			lookup_instructions =
				ulisp_list(
					ulisp_list(bytecode_binding_lookup, ulisp_make_integer((long)toplevel_binding), NULL),
					NULL);
		}

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

			LispObject* test_instructions = ulisp_compile(ulisp_car(CDR(expression)),
														  comptime_environnement);
			stack_push(&gc_stack, test_instructions);

			LispObject* if_instructions = ulisp_compile(ulisp_car(ulisp_cdr(CDR(expression))),
														comptime_environnement);
			stack_push(&gc_stack, if_instructions);

			LispObject* else_instructions = nil;

			if (ulisp_cdr(ulisp_cdr(CDR(expression))) != nil)
				else_instructions = ulisp_compile(ulisp_car(ulisp_cdr(ulisp_cdr(CDR(expression)))),
												  comptime_environnement);

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
		else if (ulisp_eq(applied_symbol, let)) {
			LispObject* instructions = nil;
			LispObject* bindings = ulisp_car(ulisp_cdr(expression));
			uint bindings_count = ulisp_length(bindings);

			LispObject* new_frame_obj = ulisp_frame_create(comptime_environnement, bindings_count);
			LispFrame* new_frame = AS(new_frame_obj, LispFrame);

			uint i = 0;
			LIST_ITERATE(b, ulisp_nreverse(bindings)) {
				LispObject* compiled = ulisp_compile(ulisp_car(ulisp_cdr(ulisp_car(b))),
													 comptime_environnement);
				stack_push(&gc_stack, compiled);
				stack_count++;

				instructions = ulisp_nconc(ulisp_nconc(compiled, ulisp_list(ulisp_list(bytecode_push, NULL), NULL)),
										   instructions);

				new_frame->bindings[i++] = ulisp_car(ulisp_car(b));

				stack_push(&gc_stack, instructions);
				stack_count++;
			}

			LispObject* compiled = ulisp_compile(ulisp_cons(begin, ulisp_cdr(ulisp_cdr(expression))), new_frame_obj);

			LispObject* bind_instructions = ulisp_nconc(
				instructions,
				ulisp_list(ulisp_list(bytecode_bind, ulisp_make_integer(bindings_count), NULL), NULL));

			instructions = ulisp_nconc(ulisp_nconc(bind_instructions, compiled),
									   ulisp_list(ulisp_cons(bytecode_unbind, nil), NULL));

			stack_pop(&gc_stack, stack_count);
			return instructions;
		}
		else if (ulisp_eq(applied_symbol, begin)) {
			LispObject* a = ulisp_compile(ulisp_car(CDR(expression)),
										  comptime_environnement);
			stack_push(&gc_stack, a);

			stack_count += 1;

			for (LispObject* it = ulisp_cdr(CDR(expression)); it != nil; it = ulisp_cdr(it)) {
				a = ulisp_nconc(a, ulisp_compile(ulisp_car(it), comptime_environnement));
				stack_push(&gc_stack, a);
				stack_count += 1;
			}

			stack_pop(&gc_stack, stack_count);
			return a;
		}
		else if (ulisp_eq(applied_symbol, named_lambda)) {
			LispObject* compiled_lambda = ulisp_compile_lambda(expression, comptime_environnement);

			long cont_label_count = ulisp_label_count++;
			LispObject** make_closure_binding = ulisp_assoc(environnement, make_closure);

			LispObject* instructions =
				ulisp_list(
					ulisp_list(bytecode_push_cont, ulisp_make_integer(cont_label_count), NULL),
					ulisp_list(bytecode_fetch_literal, compiled_lambda, NULL),
					ulisp_list(bytecode_push, NULL),
					ulisp_list(bytecode_binding_lookup, ulisp_make_integer((long)make_closure_binding), NULL),
					ulisp_list(bytecode_apply, NULL),
					ulisp_list(bytecode_label, ulisp_make_integer(cont_label_count), NULL),
					NULL);

			stack_pop(&gc_stack, stack_count);
			return instructions;
		}
		else if (ulisp_eq(applied_symbol, def)) {
			LispObject* symbol = ulisp_car(ulisp_cdr(expression));
			LispObject* object = ulisp_eval(ulisp_car(ulisp_cdr(ulisp_cdr(expression))));

			LIST_ITERATE(pair, environnement) {
				if (ulisp_eq(ulisp_car(CAR(pair)), symbol)) {
					CDR(CAR(pair)) = object;
					goto def_end;
				}
			}

			environnement = ulisp_cons(ulisp_cons(symbol, object),
									   environnement);

		def_end:
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

			LispObject* compiled_instructions = ulisp_compile(ulisp_car(CDR(expression)),
															  comptime_environnement);

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
			LispObject* value_instructions = ulisp_compile(ulisp_car(ulisp_cdr(ulisp_cdr(expression))),
														   comptime_environnement);
			LispObject* instructions;

			LispObject* symbol = ulisp_car(ulisp_cdr(expression));

			uint i, j;
			if (ulisp_comptime_local_lookup(symbol, comptime_environnement, &i, &j) == 0) {
				instructions = ulisp_nconc(value_instructions,
										   ulisp_list(
											   ulisp_list(bytecode_set_local,
														  ulisp_make_integer(i),
														  ulisp_make_integer(j),
														  NULL),
											   NULL));
			}
			else {
				LispObject** global_binding = ulisp_assoc(environnement, symbol);

				if (global_binding == NULL)
					global_binding = ulisp_toplevel_binding_create(symbol);

				instructions = ulisp_nconc(value_instructions,
										   ulisp_list(
											   ulisp_list(bytecode_set_global,
														  ulisp_make_integer((long)global_binding),
														  NULL),
											   NULL));
			}

			stack_pop(&gc_stack, stack_count);
			return instructions;
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
				instructions = ulisp_nconc(instructions,
										   ulisp_compile(ulisp_car(a), comptime_environnement));

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

	LispObject* compiled = ulisp_compile(expression, NULL);
	ulisp_print(compiled, ulisp_standard_output);
	printf("\n");

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
				if (j < 0)
					break;

				ASSERT(string[j] != '(');
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
	LispObject* stream = ulisp_make_string_stream();
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
		ulisp_stream_format(stream, "<PROC %s at %p>", ulisp_debug_print(AS(proc->template, LispTemplate)->name), obj);
	}
	else if (obj->type & LISP_INTEGER) {
		ulisp_stream_format(stream, "%ld", *(long*)obj->data);
	}
	else if (obj->type & LISP_FLOAT) {
		ulisp_stream_format(stream, "%gf", *(double*)obj->data);
	}
	else if (obj->type & LISP_CONTINUATION) {
		ulisp_stream_format(stream, "#<CONTINUATION at %p>", obj);
	}
	else if (obj->type & LISP_STREAM) {
		ulisp_stream_format(stream, "#<STREAM at %p>", obj);
	}
	else if (obj->type & LISP_TEMPLATE) {
		ulisp_stream_format(stream, "#<TEMPLATE at %p>", obj);
	}
	else if (obj->type & LISP_ARRAY) {
		LispArray* array = AS(obj, LispArray);

		if (array->dimensions_count == 1)
			ulisp_stream_format(stream, "#");
		else
			ulisp_stream_format(stream, "#%dA", array->dimensions_count);

		uint i = 0;
		uint last_dim = array->dimensions[array->dimensions_count - 1];

		for (uint i = 0; i < array->dimensions_count - 1; i++)
			ulisp_stream_write("(", stream);

		while (i < array->size) {
			if (i % last_dim == 0) {
				if (i != 0)
					ulisp_stream_write(")\n", stream);

				ulisp_stream_write("(", stream);
			} else {
				ulisp_stream_write(" ", stream);
			}

			ulisp_print(array->data[i], stream);
			i++;
		}

		for (uint i = 0; i < array->dimensions_count; i++)
			ulisp_stream_write(")", stream);
	}
	else {
		ulisp_stream_format(stream, "#<NOT PRINTABLE>");
	}
}
