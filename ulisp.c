#include "ulisp.h"
#include "psyche.h"
#include "random.h"

#include <assert.h>
#include <string.h>

#define IS_BLANK(x) ((x) == ' ' || (x) == '\n' || (x) == '\t')
#define DEBUG(m,e) printf("%s:%d: %s:",__FILE__,__LINE__,m); ulisp_print(e, stdout); puts("");
#define CAR(o) ((ConsCell*)(o)->data)->car
#define CDR(o) ((ConsCell*)(o)->data)->cdr
#define panic(str) do { printf(str"\n"); exit(-1); } while (0)

LispObject *environnement, *nil, *tee, *quote, *iffe, *begin,
	*lambda, *mlambda, *define, *rest, *not_found;

#define DEFAULT_WORKSPACESIZE (2 << 16)

static uint64_t workspace_size = DEFAULT_WORKSPACESIZE;
static uint64_t free_space = DEFAULT_WORKSPACESIZE;
static uint32_t cons_cell_size = sizeof(LispObject) + sizeof(ConsCell);
static LispObject* free_list;
static uchar* workspace;
static Stack gc_stack;

static LispObject* eval_stack;
static LispObject* envt_register;
LispObject* value_register;
static LispObject* current_continuation;
static LispTemplate* template_register;
static uchar* ulisp_rip;

LispObject* ulisp_car(LispObject* pair);
LispObject* ulisp_cdr(LispObject* object);
void ulisp_gc(LispObject* cons_cell);

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

void ulisp_gc(LispObject* cons_cell) {
	printf("Garbage collection...\n");

	for (uint64_t i = 0; i < gc_stack.top; i++)
		mark_object(gc_stack.data[i]);

	mark_object(cons_cell);
	mark_object(environnement);
	mark_object(envt_register);
	mark_object(eval_stack);
	mark_object(current_continuation);

	for (uint i = 0; i < template_register->literals_count; i++)
		mark_object(template_register->literals[i]);

	free_list_sweep();

	printf("Now having %lu free space!\n", free_space);
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

LispObject* ulisp_make_closure(LispTemplate* template, LispObject* envt) {
	LispObject* obj = malloc(sizeof(LispObject) + sizeof(LispClosure));
	obj->type = LISP_PROC;

	LispClosure* closure = obj->data;

	closure->template = template;
	closure->envt = envt;

	return obj;
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
	assert(plist->type & LISP_LIST);
	assert(symbol->type & LISP_SYMBOL);

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

LispObject* ulisp_prim_insert_at_point(LispObject* args) {
	if (ps_current_input)
		ps_input_insert_at_point(ps_current_input, ulisp_debug_print(ulisp_car(args)));

	return nil;
}

LispObject* ulisp_prim_random(LispObject* args) {
	assert(ulisp_length(args) == 1);
	LispObject* number = ulisp_car(args);
	assert(number->type & LISP_NUMBER);

	if (number->type & LISP_FLOAT)
		return ulisp_make_float(random_float() * *(double*)number->data);
	else
		return ulisp_make_integer(random_randint() % *(long*)number->data);
}

LispObject* ulisp_prim_cons_p(LispObject* args) {
	assert(ulisp_length(args) == 1);

	return ulisp_car(args)->type & LISP_CONS ? tee : nil;
}

void env_push_fun(const char* name, void* function) {
	environnement = ulisp_cons(ulisp_cons(ulisp_make_symbol(name),
										  ulisp_builtin_proc(function)),
							   environnement);
}

static LispObject *bytecode_push, *bytecode_push_cont, *bytecode_lookup_var,
	*bytecode_apply, *bytecode_restore_cont, *bytecode_fetch_literal, *bytecode_label,
	*bytecode_bind, *bytecode_branch_if, *bytecode_branch_else, *bytecode_branch;

void ulisp_init(void) {
	stack_init(&gc_stack);

	nil = ulisp_make_symbol("nil");
	nil->type |= LISP_LIST;

	value_register = nil;
	eval_stack = nil;
	template_register = NULL;
	envt_register = nil;
	ulisp_rip = NULL;

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
	env_push_fun("cons?", ulisp_prim_cons_p);
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

	/* ulisp_eval(ulisp_read_list(read_file("./lisp/core.ul")), nil); */
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
	if (value_register->type & LISP_PROC_BUILTIN) {
		LispObject* (**fn)(LispObject*) = value_register->data;
		value_register = (*fn)(eval_stack);
		ulisp_restore_continuation();
	}
	else if (value_register->type & LISP_PROC) {
		LispClosure* closure = value_register->data;

		envt_register = closure->envt;
		template_register = closure->template;
		ulisp_rip = closure->template->code;
	}
	else {
		ulisp_print(value_register, stdout);
		panic(" is not a function!");
	}
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

void ulisp_run(LispTemplate* template) {
	ulisp_rip = template->code;
	template_register = template;

start:
	switch (ulisp_rip[0]) {
	case ULISP_BYTECODE_APPLY:
		ulisp_apply();
		break;
	case ULISP_BYTECODE_LOOKUP:
	{
		GLboolean found;
		value_register = ulisp_assoc(envt_register, *(void**)(ulisp_rip + 1), &found);

		if (!found) {
			GLboolean global_found;
			value_register = ulisp_assoc(environnement, *(void**)(ulisp_rip + 1), &global_found);
			assert(global_found);
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
		if (ulisp_eq(value_register, nil))
			ulisp_rip += sizeof(size_t) + 1;
		else
			ulisp_rip = template_register->code + *(size_t*)(ulisp_rip + 1);
		break;
	case ULISP_BYTECODE_BRANCH_ELSE:
		if (ulisp_eq(value_register, nil))
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
	case ULISP_BYTECODE_BIND:
		ulisp_bind(*(void**)(ulisp_rip + 1));
		ulisp_rip += sizeof(void*) + 1;
		break;
	case ULISP_BYTECODE_END:
		goto end;
	default:
		panic("Invalid instruction");
	}

	goto start;
end:
	return;
}

void ulisp_scan_labels(LispObject* code, DynamicArray* target) {
	size_t offset = 0;

	for (LispObject* exp = code; exp != nil; exp = ulisp_cdr(exp)) {
		LispObject* funcall = ulisp_car(exp);
		LispObject* instruction = ulisp_car(funcall);

		if (ulisp_eq(instruction, bytecode_lookup_var) ||
			ulisp_eq(instruction, bytecode_bind) ||
			ulisp_eq(instruction, bytecode_push_cont))
		{
			offset += 1 + sizeof(void*);
		}
		else if (ulisp_eq(instruction, bytecode_apply) ||
				 ulisp_eq(instruction, bytecode_restore_cont) ||
				 ulisp_eq(instruction, bytecode_push))
		{
			offset += 1;
		}
		else if (ulisp_eq(instruction, bytecode_fetch_literal)) {
			offset += 1 + sizeof(uint);
		}
		else if (ulisp_eq(instruction, bytecode_branch_if) ||
				 ulisp_eq(instruction, bytecode_branch_else) ||
				 ulisp_eq(instruction, bytecode_branch))
		{
			offset += 1 + sizeof(size_t);
		}
		else if (ulisp_eq(instruction, bytecode_label)) {
			LispLabel* label = dynamic_array_push_back(target, 1);
			LispObject* number_id = ulisp_car(ulisp_cdr(funcall));
			assert(number_id->type & LISP_INTEGER);

			label->id = *(long*)number_id->data;
			label->offset = offset;
		}
		else {
			ulisp_print(instruction, stdout);
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
		else if (ulisp_eq(applied_symbol, bytecode_label)) {
			continue;
		}
		else {
			printf("operand not understood: ");
			ulisp_print(applied_symbol, stdout);
			panic("\nCompiler error.");
		}
	}

	uchar* end = dynamic_array_push_back(&bytecode, 1);
	*end = ULISP_BYTECODE_END;

	LispTemplate* compiled_code = malloc(sizeof(LispTemplate) + sizeof(LispObject*) * literals.size);
	compiled_code->code = bytecode.data;
	compiled_code->literals_count = 0;

	for (uint i = 0; i < literals.size; i++)
		compiled_code->literals[i] = *(LispObject**)dynamic_array_at(&literals, i);

	dynamic_array_destroy(&labels);
	dynamic_array_destroy(&literals);
	return compiled_code;
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

LispObject* ulisp_read(const char* string) {
	return ulisp_car(ulisp_read_list_helper(string));
}

char* ulisp_debug_print(LispObject* obj) {
	char* buffer = NULL;
	size_t bufferSize = 0;
	FILE* myStream = open_memstream(&buffer, &bufferSize);
	ulisp_print(obj, myStream);

	fclose(myStream);

	return buffer;
}

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
		LispClosure* proc = (LispClosure*)obj->data;
		fprintf(stream, "<PROC at %p>", proc);
	}
	else if (obj->type & LISP_INTEGER) {
		fprintf(stream, "%ld", *(long*)obj->data);
	}
	else if (obj->type & LISP_FLOAT) {
		fprintf(stream, "%gf", *(double*)obj->data);
	}
}
