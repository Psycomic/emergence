#include "yuki.h"
#include "psyche.h"

#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <math.h>
#include <stdbool.h>
#include <time.h>
#include <stdarg.h>
#include <signal.h>

#include "random.h"

#define YK_MARK_BIT ((YkUint)1)
#define YK_MARKED(x) (((YkUint)YK_CAR(x)) & YK_MARK_BIT)

#define YK_WORKSPACE_SIZE 4096

typedef struct YkCompilerVar {
	YkObject symbol;
	enum {
		YK_VAR_NORMAL,
		YK_VAR_BOXED,
		YK_VAR_ENVIRONNEMENT,
		YK_VAR_RETURN,
		YK_VAR_UNUSED
	} type;
	YkType value_type;
	struct YkCompilerVar* next;
} YkCompilerVar;

typedef struct YkClosedVar {
	YkCompilerVar* lexical_stack;
	struct YkClosedVar* next;
} YkClosedVar;

typedef struct {
	YkObject expr;

	YkCompilerVar* cont_stack;
	YkCompilerVar* lexical_stack;

	DynamicArray* warnings;

	YkClosedVar* var_upenvs;
	YkClosedVar* cont_upenvs;

	YkCompilerVar* closed_vars;
	YkCompilerVar* closed_conts;

	bool is_tail;
} YkCompilerState;

static union YkUnion* yk_workspace;
static union YkUnion* yk_free_list;
static YkUint yk_workspace_size;
static YkUint yk_free_space;

#define YK_ARRAY_ALLOCATOR_SIZE 0x20000
static char* yk_array_allocator;
static char* yk_array_allocator_top;

#define YK_SYMBOL_TABLE_SIZE 4096
static YkObject *yk_symbol_table;

/* Registers */
static YkObject yk_value_register;
static YkInstruction* yk_program_counter;
static YkObject yk_bytecode_register;

/* Symbols */
YkObject yk_tee, yk_nil, yk_debugger, yk_var_output;

/* Stacks */
YkObject *yk_gc_stack[YK_GC_STACK_MAX_SIZE];
YkUint yk_gc_stack_size;

/* Other stuff */
static jmp_buf yk_jump_buf;

#define YK_STACK_MAX_SIZE 1024
static YkObject yk_lisp_stack[YK_STACK_MAX_SIZE];
static YkObject* yk_lisp_stack_top;

static YkObject* yk_lisp_frame_ptr;

static YkDynamicBinding yk_dynamic_bindings_stack[YK_STACK_MAX_SIZE];
static YkDynamicBinding* yk_dynamic_bindings_stack_top;

static YkObject yk_continuations_stack[YK_STACK_MAX_SIZE];
static YkObject* yk_continuations_stack_top;

static YkObject yk_current_exit_cont = YK_NIL;

#define YK_PUSH(stack, x) *(--(stack)) = ((void*)x)
#define YK_POP(stack, type, x) x = *((type)((stack)++))

#define YK_LISP_STACK_PUSH(x) YK_PUSH(yk_lisp_stack_top, x)
#define YK_LISP_STACK_POP(x, type) YK_POP(yk_lisp_stack_top, type, x)

/* Error handling */
#define YK_ASSERT(cond) if (!(cond)) { yk_assert(#cond, __FILE__, __LINE__); }

static void yk_gc();
static YkObject yk_reverse(YkObject list);
static YkObject yk_nreverse(YkObject list);
static YkUint yk_length(YkObject list);
static bool yk_member(YkObject element, YkObject list);

static void yk_mark_block_data(void* data);
static void yk_array_allocator_sweep();
static void yk_array_allocator_print();

static YkObject yk_make_array(YkUint size, YkObject element);
static void yk_assert(const char* expression, const char* file, uint32_t line);
void yk_bytecode_disassemble(YkObject bytecode);
static YkObject yk_make_string(const char* cstr, size_t cstr_size);
inline static char* yk_symbol_cstr(YkObject sym);

static YkCompilerVar* yk_find_closed_vars(YkObject expr, YkClosedVar* upenvs, YkObject env);
static YkCompilerVar* yk_find_closed_conts(YkObject expr, YkClosedVar* upenvs, YkObject env);

static YkObject yk_arglist_cfun,
	yk_symbol_file_mode_input, yk_symbol_file_mode_output, yk_symbol_file_mode_append,
	yk_symbol_file_mode_binary_input, yk_symbol_file_mode_binary_output, yk_symbol_file_mode_binary_append,
	yk_symbol_eof, yk_symbol_environnement, yk_array_cfun;

#ifdef _DEBUG
YkObject yk_ptr(YkObject o) {
	return YK_PTR(o);
}
#endif

static void yk_allocator_init() {
	yk_workspace = malloc(sizeof(union YkUnion) * YK_WORKSPACE_SIZE);
    yk_workspace_size = YK_WORKSPACE_SIZE;
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

#define YK_GC_STRESS 0

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
	else if (YK_SYMBOLP(o)) {
		YK_CAR(o) = YK_TAG(YK_CAR(o), YK_MARK_BIT);

		yk_mark(YK_PTR(o)->symbol.value);
		o = YK_PTR(o)->symbol.name;
		goto mark;
	}
	else if (YK_CPROCP(o)) {
		YkObject docstring = YK_PTR(o)->c_proc.docstring;
		YK_CAR(o) = YK_TAG(YK_CAR(o), YK_MARK_BIT);

		o = docstring;
		goto mark;
	}
	else if (YK_BYTECODEP(o)) {
		YkObject bytecode = YK_PTR(o);
		yk_mark_block_data(bytecode->bytecode.code);

		YkObject docstring = bytecode->bytecode.docstring;
		YK_CAR(o) = YK_TAG(YK_CAR(o), YK_MARK_BIT);

		for (uint i = 0; i < bytecode->bytecode.code_size; i++) {
			yk_mark(bytecode->bytecode.code[i].ptr);
		}

		o = docstring;
		goto mark;
	}
	else if (YK_CLOSUREP(o)) {
		yk_mark(YK_PTR(o)->closure.lexical_env);
		YkObject bytecode = YK_PTR(o)->closure.bytecode;

		YK_CAR(o) = YK_TAG(YK_CAR(o), YK_MARK_BIT);

		o = bytecode;
		goto mark;
	}
	else if (YK_CONTINUATIONP(o)) {
		YkObject bytecode = YK_PTR(o)->continuation.bytecode_register;
		YK_CAR(o) = YK_TAG(YK_CAR(o), YK_MARK_BIT);

		o = bytecode;
		goto mark;
	}
	else if (YK_TYPEOF(o) == yk_t_array) {
		YkObject* data = YK_PTR(o)->array.data;
		yk_mark_block_data(data);

		if (data != NULL) {
			for (uint i = 0; i < YK_PTR(o)->array.size; i++) {
				yk_mark(data[i]);
			}
		}

		YK_CAR(o) = YK_TAG(YK_CAR(o), YK_MARK_BIT);
	}
	else if (YK_TYPEOF(o) == yk_t_string) {
		yk_mark_block_data(YK_PTR(o)->string.data);
		YK_CAR(o) = YK_TAG(YK_CAR(o), YK_MARK_BIT);
	}
	else if (YK_TYPEOF(o) == yk_t_file_stream) {
		YK_CAR(o) = YK_TAG(YK_CAR(o), YK_MARK_BIT);
	}
	else if (YK_TYPEOF(o) == yk_t_string_stream) {
		yk_mark_block_data(YK_PTR(o)->string_stream.buffer);
		YK_CAR(o) = YK_TAG(YK_CAR(o), YK_MARK_BIT);
	}
	else {
		assert(0);
	}
}

static void yk_free(YkObject o) {
#ifdef YK_GC_STRESS
	memset(o, 0x66, sizeof(union YkUnion));
#endif

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

static YkObject yk_gc_protected_stack[1024];
static YkUint yk_gc_protected_stack_size;

static void yk_permanent_gc_protect(YkObject object) {
	yk_gc_protected_stack[yk_gc_protected_stack_size++] = object;
}

static void yk_gc() {
	yk_mark(yk_value_register);
	yk_mark(yk_bytecode_register);
	yk_mark(yk_current_exit_cont);

	for (size_t i = 0; i < yk_gc_protected_stack_size; i++) {
		yk_mark(yk_gc_protected_stack[i]);
	}

	for (size_t i = 0; i < YK_SYMBOL_TABLE_SIZE; i++)
		yk_mark(yk_symbol_table[i]);

	printf("GC stack: %ld\n", yk_gc_stack_size);
	for (size_t i = 0; i < yk_gc_stack_size; i++)
		yk_mark(*yk_gc_stack[i]);

	for (long i = 0; i < YK_STACK_MAX_SIZE - (yk_lisp_stack_top - yk_lisp_stack); i++)
		yk_mark(yk_lisp_stack_top[i]);

	for (long i = 0; i < YK_STACK_MAX_SIZE -
			 (yk_continuations_stack_top - yk_continuations_stack); i++)
	{
		yk_mark(yk_continuations_stack_top[i]);
	}

	for (long i = 0; i < YK_STACK_MAX_SIZE -
			 (yk_dynamic_bindings_stack_top - yk_dynamic_bindings_stack); i++)
	{
		yk_mark(yk_dynamic_bindings_stack_top[i].symbol);
		yk_mark(yk_dynamic_bindings_stack_top[i].old_value);
	}

	yk_array_allocator_sweep();
	yk_sweep();
}

#define YK_BLOCK_MARKED_BIT 0x1
#define YK_BLOCK_USED_BIT   0x2

#define YK_BLOCK_USED(b)   ((b)->flags & YK_BLOCK_USED_BIT)
#define YK_BLOCK_MARKED(b) ((b)->flags & YK_BLOCK_MARKED_BIT)

static void yk_array_allocator_init() {
	yk_array_allocator = malloc(YK_ARRAY_ALLOCATOR_SIZE);
	yk_array_allocator_top = yk_array_allocator;
}

static void* yk_array_allocator_alloc(YkUint size) {
	char* block_ptr;
	bool has_gc = false;

start:
	block_ptr = yk_array_allocator;
	while (block_ptr < yk_array_allocator_top) {
		YkArrayAllocatorBlock* block = (YkArrayAllocatorBlock*)block_ptr;

		if (!YK_BLOCK_USED(block) && block->size >= size) {
			YkUint original_size = block->size;
			block->size = size;
			block->flags = YK_BLOCK_USED_BIT;

			YkUint new_block_size = original_size - size;
			if (new_block_size > sizeof(YkArrayAllocatorBlock)) {
				YkArrayAllocatorBlock* next_block =
					(YkArrayAllocatorBlock*)(block_ptr + sizeof(YkArrayAllocatorBlock) + size);

				next_block->size = new_block_size - sizeof(YkArrayAllocatorBlock);
				next_block->flags = 0x0;
			} else {
				block->size = original_size;
			}

			return block->data;
		}

		block_ptr += block->size + sizeof(YkArrayAllocatorBlock);
	}

	if ((int64_t)size >=
		(YK_ARRAY_ALLOCATOR_SIZE - (yk_array_allocator_top - yk_array_allocator)))
	{
		assert(!has_gc);
		has_gc = true;
		yk_gc();
		goto start;
	}

	YkArrayAllocatorBlock* block = (YkArrayAllocatorBlock*)yk_array_allocator_top;
	block->size = size;
	block->flags = YK_BLOCK_USED_BIT;
	yk_array_allocator_top += sizeof(YkArrayAllocatorBlock) + size;

	return block->data;
}

static void yk_mark_block_data(void* data) {
	if (data != NULL) {
		YkArrayAllocatorBlock* block = (YkArrayAllocatorBlock*)
			((char*)data - sizeof(YkArrayAllocatorBlock));

		block->flags |= YK_BLOCK_MARKED_BIT;
	}
}

static void yk_array_allocator_print() {
	char* block_ptr = yk_array_allocator;

	while(block_ptr < yk_array_allocator_top) {
		YkArrayAllocatorBlock* block = (YkArrayAllocatorBlock*)block_ptr;
		block_ptr += block->size + sizeof(YkArrayAllocatorBlock);

		printf("%p %c | size: %u, marked: %d\n", block->data,
			   YK_BLOCK_USED(block) ? 'X' : 'O',
			   block->size, YK_BLOCK_MARKED(block));
	}
}

static void yk_array_allocator_sweep() {
	char* block_ptr = yk_array_allocator;
	YkArrayAllocatorBlock *last_block = NULL;
	uint freed = 0;

	while (block_ptr < yk_array_allocator_top) {
		YkArrayAllocatorBlock* block = (YkArrayAllocatorBlock*)block_ptr;

		if (!YK_BLOCK_MARKED(block) || !YK_BLOCK_USED(block)) {
			block->flags &= ~YK_BLOCK_USED_BIT;
			memset(block->data, 0x66, block->size);

			if (last_block != NULL) {
				last_block->size += block->size + sizeof(YkArrayAllocatorBlock);
			} else {
				last_block = block;
			}

			freed++;
		} else {
			last_block = NULL;
		}

		block->flags &= ~YK_MARK_BIT;
		block_ptr += block->size + sizeof(YkArrayAllocatorBlock);
	}

	printf("Freed %d blocks of memory!\n", freed);
}

static void yk_symbol_table_init() {
	yk_symbol_table = malloc(sizeof(YkObject) * YK_SYMBOL_TABLE_SIZE);

	for (uint i = 0; i < YK_SYMBOL_TABLE_SIZE; i++) {
		yk_symbol_table[i] = NULL;
	}
}

static YkObject yk_make_global_function(YkObject sym, YkInt nargs, YkCfun fn) {
	YkObject proc = YK_NIL;
	YK_GC_PROTECT2(proc, sym);

	proc = yk_alloc();
	proc->c_proc.name = sym;
	proc->c_proc.nargs = nargs;
	proc->c_proc.cfun = fn;
#if YK_PROFILE
	proc->c_proc.calls = 0;
	proc->c_proc.time_called = 0;
#endif

	YK_GC_UNPROTECT;
	return YK_TAG(proc, yk_t_c_proc);
}

static void yk_make_builtin(char* name, YkInt nargs, YkCfun fn) {
	YkObject proc = YK_NIL, sym = YK_NIL;
	YK_GC_PROTECT2(proc, sym);

	sym = yk_make_symbol_cstr(name);

	YK_PTR(sym)->symbol.value = yk_make_global_function(sym, nargs, fn);
	YK_PTR(sym)->symbol.declared = 1;
	YK_PTR(sym)->symbol.type = yk_s_function;
	YK_PTR(sym)->symbol.function_nargs = nargs;

	YK_GC_UNPROTECT;
}

static YkInt yk_signed_fixnum_to_long(YkInt i) {
	if (i & (1L << 59))
		i |= 1L << 63 | 1L << 62 | 1L << 61 | 1L << 60;

	return i;
}

static float yk_fixnum_to_float(YkObject fix) {
	return (float)yk_signed_fixnum_to_long(YK_INT(fix));
}

char* yk_string_to_c_str(YkObject string) {
	return YK_PTR(string)->string.data;
}

static YkObject yk_make_file_stream(YkObject path, YkObject mode, FILE* optional_fptr) {
	YK_GC_PROTECT2(path, mode);

	YkObject stream = yk_alloc();
	stream->file_stream.t = yk_t_file_stream;

	char* s_mode = NULL;
	if (mode == yk_symbol_file_mode_input) {
		s_mode = "r";
		stream->file_stream.flags = YK_STREAM_READ_BIT;
	} else if (mode == yk_symbol_file_mode_binary_input) {
		s_mode = "r";
		stream->file_stream.flags = YK_STREAM_READ_BIT | YK_STREAM_BINARY_BIT;
	} else if (mode == yk_symbol_file_mode_output) {
		s_mode = "w";
		stream->file_stream.flags = YK_STREAM_WRITE_BIT;
	} else if (mode == yk_symbol_file_mode_binary_output) {
		s_mode = "w";
		stream->file_stream.flags = YK_STREAM_BINARY_BIT | YK_STREAM_WRITE_BIT;
	} else if (mode == yk_symbol_file_mode_append) {
		s_mode = "a";
		stream->file_stream.flags = YK_STREAM_WRITE_BIT;
	} else if (mode == yk_symbol_file_mode_binary_append) {
		s_mode = "a";
		stream->file_stream.flags = YK_STREAM_WRITE_BIT | YK_STREAM_BINARY_BIT;
	}

	YK_ASSERT(s_mode != NULL);

	if (optional_fptr == NULL) {
		FILE* file = fopen(yk_string_to_c_str(path), s_mode);
		YK_ASSERT(file != NULL);
		stream->file_stream.file_ptr = file;
	} else {
		stream->file_stream.file_ptr = optional_fptr;
	}

	YK_GC_UNPROTECT;
	return stream;
}

static YkObject yk_make_input_string_stream(char* data) {
	YkObject stream = yk_alloc();
	YK_GC_PROTECT1(stream);

	stream->string_stream.t = yk_t_string_stream;
	stream->string_stream.read_bytes = 0;
	stream->string_stream.flags = YK_STREAM_READ_BIT;

	size_t size = strlen(data);

	stream->string_stream.size = size;
	stream->string_stream.capacity = size;
	stream->string_stream.buffer = data;

	YK_GC_UNPROTECT;
	return stream;
}

YkObject yk_make_output_string_stream() {
	YkObject stream = yk_alloc();
	YK_GC_PROTECT1(stream);

	stream->string_stream.t = yk_t_string_stream;
	stream->string_stream.read_bytes = 0;
	stream->string_stream.flags = YK_STREAM_WRITE_BIT;

	stream->string_stream.size = 0;
	stream->string_stream.capacity = 16;
	stream->string_stream.buffer = yk_array_allocator_alloc(stream->string_stream.capacity);

	YK_GC_UNPROTECT;
	return stream;
}

static void yk_stream_write_byte(YkObject stream, YkInt byte) {
	YK_GC_PROTECT1(stream);
	YK_ASSERT(stream->t.t == yk_t_file_stream);
	YK_ASSERT(stream->file_stream.flags & YK_STREAM_WRITE_BIT &&
			  stream->file_stream.flags & YK_STREAM_BINARY_BIT);
	YK_ASSERT(!(stream->file_stream.flags & YK_STREAM_FINISHED_BIT));
	YK_ASSERT(byte >= 0 && byte <= 255);

	if (putc(byte, stream->file_stream.file_ptr) == EOF)
		YK_ASSERT(0);

	YK_GC_UNPROTECT;
}

static YkInt yk_stream_read_byte(YkObject stream) {
	YK_ASSERT(stream->t.t == yk_t_file_stream);
	YK_ASSERT(!(stream->file_stream.flags & YK_STREAM_FINISHED_BIT));
	YK_ASSERT(stream->file_stream.flags & YK_STREAM_READ_BIT &&
			  stream->file_stream.flags & YK_STREAM_BINARY_BIT);

	int c = getc(stream->file_stream.file_ptr);
	if (c < 0)
		return -1;

	return c;
}

static void yk_stream_write_char(YkObject stream, YkInt byte) {
	if (stream->t.t == yk_t_file_stream) {
		YK_ASSERT(!(stream->file_stream.flags & YK_STREAM_FINISHED_BIT));
		YK_ASSERT(stream->file_stream.flags & YK_STREAM_WRITE_BIT &&
				  !(stream->file_stream.flags & YK_STREAM_BINARY_BIT));

		char string[5] = { 0, 0, 0, 0, 0 };
		u_codepoint_to_string(string, byte);

		char* c = string;
		do {
			if (putc(*c, stream->file_stream.file_ptr) == EOF)
				YK_ASSERT(0);
		} while (*++c != '\0');
	} else if (stream->t.t == yk_t_string_stream) {
		YK_ASSERT(!(stream->string_stream.flags & YK_STREAM_FINISHED_BIT));
		YK_ASSERT(stream->string_stream.flags & YK_STREAM_WRITE_BIT &&
				  !(stream->string_stream.flags & YK_STREAM_BINARY_BIT));

		char string[5] = { 0, 0, 0, 0, 0 };
		u_codepoint_to_string(string, byte);

		size_t s_size = strlen(string),
			old_size = stream->string_stream.size;

		stream->string_stream.size += s_size;

		if (stream->string_stream.size >= stream->string_stream.capacity) {
			stream->string_stream.capacity = stream->string_stream.size * 2;

			char* new_buffer = yk_array_allocator_alloc(stream->string_stream.capacity);
			memcpy(new_buffer, stream->string_stream.buffer, old_size);
			stream->string_stream.buffer = new_buffer;
		}

		memcpy(stream->string_stream.buffer + old_size, string, s_size);
		stream->string_stream.buffer[old_size + s_size] = 0;
	}
}

static YkInt yk_stream_read_char(YkObject stream) {
	if (stream->t.t == yk_t_file_stream) {
		YK_ASSERT(!(stream->file_stream.flags & YK_STREAM_FINISHED_BIT));
		YK_ASSERT(stream->file_stream.flags & YK_STREAM_READ_BIT &&
				  !(stream->file_stream.flags & YK_STREAM_BINARY_BIT));

		int c = getc(stream->file_stream.file_ptr);
		if (c < 0)
			return -1;

		uint8_t byte = (uint8_t)c;
		uint8_t utf8_string[4];

		if (byte <= 0x7f) {
			utf8_string[0] = byte;
		} else if (byte >> 3 == 0x1e) {
			utf8_string[0] = byte;
			utf8_string[1] = (uint8_t)getc(stream->file_stream.file_ptr);
			utf8_string[2] = (uint8_t)getc(stream->file_stream.file_ptr);
			utf8_string[3] = (uint8_t)getc(stream->file_stream.file_ptr);
		} else if (byte >> 4 == 0xe) {
			utf8_string[0] = byte;
			utf8_string[1] = (uint8_t)getc(stream->file_stream.file_ptr);
			utf8_string[2] = (uint8_t)getc(stream->file_stream.file_ptr);
		} else if (byte >> 5 == 6) {
			utf8_string[0] = byte;
			utf8_string[1] = (uint8_t)getc(stream->file_stream.file_ptr);
		}

		return u_string_to_codepoint(utf8_string);
	} else {
		YK_ASSERT(!(stream->string_stream.flags & YK_STREAM_FINISHED_BIT));
		YK_ASSERT(stream->string_stream.flags & YK_STREAM_READ_BIT &&
				  !(stream->string_stream.flags & YK_STREAM_BINARY_BIT));

		YkStringStream* ss = &stream->string_stream;
		if (ss->read_bytes >= ss->size)
			return -1;

		char a = ss->buffer[ss->read_bytes++];
		return a;
	}
}

static void yk_stream_format(YkObject stream, const char* format, ...) {
	YK_ASSERT(YK_STREAMP(stream));

	va_list arguments;

	if (stream->t.t == yk_t_file_stream) {
		YK_ASSERT(!(stream->file_stream.flags & YK_STREAM_BINARY_BIT));
		YK_ASSERT(stream->file_stream.flags & YK_STREAM_WRITE_BIT);

		va_start(arguments, format);
		vfprintf(stream->file_stream.file_ptr, format, arguments);
		va_end(arguments);
	} else if (stream->t.t == yk_t_string_stream) {
		YK_ASSERT(!(stream->string_stream.flags & YK_STREAM_BINARY_BIT));
		YK_ASSERT(stream->string_stream.flags & YK_STREAM_WRITE_BIT);

		size_t size, old_size = stream->string_stream.size;

		va_start(arguments, format);
		size = vsnprintf(NULL, 0, format, arguments);
		va_end(arguments);

		stream->string_stream.size += size;

		if (stream->string_stream.size >= stream->string_stream.capacity) {
			stream->string_stream.capacity = stream->string_stream.size * 2;

			char* new_buffer = yk_array_allocator_alloc(stream->string_stream.capacity);
			memcpy(new_buffer, stream->string_stream.buffer, old_size);
			stream->string_stream.buffer = new_buffer;
		}

		va_start(arguments, format);
		vsprintf(stream->string_stream.buffer + old_size, format, arguments);
		va_end(arguments);

		stream->string_stream.buffer[old_size + size] = 0;
	}
}

static void yk_stream_close(YkObject stream) {
	YK_ASSERT(YK_STREAMP(stream));

	if (stream->t.t == yk_t_file_stream) {
		YK_ASSERT(!(stream->file_stream.flags & YK_STREAM_FINISHED_BIT));

		stream->file_stream.flags |= YK_STREAM_FINISHED_BIT;
		fclose(stream->file_stream.file_ptr);
	} else if (stream->t.t == yk_t_string_stream) {
		YK_ASSERT(!(stream->string_stream.flags & YK_STREAM_FINISHED_BIT));
		stream->string_stream.flags |= YK_STREAM_FINISHED_BIT;

		stream->string_stream.size = 0;
		stream->string_stream.capacity = 0;
		stream->string_stream.buffer = NULL;
	}
}

YkObject yk_stream_string(YkObject stream) {
	YkObject string = yk_alloc();

	string->string.t = yk_t_string;
	string->string.size = stream->string_stream.size;
	string->string.data = stream->string_stream.buffer;

	return string;
}

static YkObject yk_append(YkObject a, YkObject b) {
	YkObject result = YK_NIL;
	YK_GC_PROTECT1(result);

	YK_LIST_FOREACH(a, l) {
		result = yk_cons(YK_CAR(l), result);
	}

	YK_LIST_FOREACH(b, l) {
		result = yk_cons(YK_CAR(l), result);
	}

	YK_GC_UNPROTECT;
	return yk_nreverse(result);
}


static YkObject yk_builtin_neq(YkUint nargs) {
	YK_ASSERT(YK_INTP(yk_lisp_stack_top[0]) || YK_FLOATP(yk_lisp_stack_top[0]));
	YK_ASSERT(YK_INTP(yk_lisp_stack_top[1]) || YK_FLOATP(yk_lisp_stack_top[1]));

	if (YK_INTP(yk_lisp_stack_top[0])) {
		if (YK_INTP(yk_lisp_stack_top[1])) {
			return yk_lisp_stack_top[0] == yk_lisp_stack_top[1] ? yk_tee : YK_NIL;
		} else {
			return yk_fixnum_to_float(yk_lisp_stack_top[0]) ==
				YK_FLOAT(yk_lisp_stack_top[1]) ? yk_tee : YK_NIL;
		}
	} else {
		if (YK_INTP(yk_lisp_stack_top[1])) {
			return YK_FLOAT(yk_lisp_stack_top[0]) ==
				yk_fixnum_to_float(yk_lisp_stack_top[1]) ? yk_tee : YK_NIL;
		} else {
			return YK_FLOAT(yk_lisp_stack_top[0]) ==
				YK_FLOAT(yk_lisp_stack_top[1]) ? yk_tee : YK_NIL;
		}
	}
}

static YkObject yk_builtin_nsup(YkUint nargs) {
	YK_ASSERT(YK_INTP(yk_lisp_stack_top[0]) || YK_FLOATP(yk_lisp_stack_top[0]));
	YK_ASSERT(YK_INTP(yk_lisp_stack_top[1]) || YK_FLOATP(yk_lisp_stack_top[1]));

	if (YK_INTP(yk_lisp_stack_top[0])) {
		if (YK_INTP(yk_lisp_stack_top[1])) {
			return yk_lisp_stack_top[0] > yk_lisp_stack_top[1] ? yk_tee : YK_NIL;
		} else {
			return yk_fixnum_to_float(yk_lisp_stack_top[0]) >
				YK_FLOAT(yk_lisp_stack_top[1]) ? yk_tee : YK_NIL;
		}
	} else {
		if (YK_INTP(yk_lisp_stack_top[0])) {
			return YK_FLOAT(yk_lisp_stack_top[0]) >
				yk_fixnum_to_float(yk_lisp_stack_top[1]) ? yk_tee : YK_NIL;
		} else {
			return YK_FLOAT(yk_lisp_stack_top[0]) >
				YK_FLOAT(yk_lisp_stack_top[1]) ? yk_tee : YK_NIL;
		}
	}
}

static YkObject yk_builtin_ninf(YkUint nargs) {
	YK_ASSERT(YK_INTP(yk_lisp_stack_top[0]) || YK_FLOATP(yk_lisp_stack_top[0]));
	YK_ASSERT(YK_INTP(yk_lisp_stack_top[1]) || YK_FLOATP(yk_lisp_stack_top[1]));

	if (YK_INTP(yk_lisp_stack_top[0])) {
		if (YK_INTP(yk_lisp_stack_top[1])) {
			return yk_lisp_stack_top[0] < yk_lisp_stack_top[1] ? yk_tee : YK_NIL;
		} else {
			return yk_fixnum_to_float(yk_lisp_stack_top[0]) <
				YK_FLOAT(yk_lisp_stack_top[1]) ? yk_tee : YK_NIL;
		}
	} else {
		if (YK_INTP(yk_lisp_stack_top[0])) {
			return YK_FLOAT(yk_lisp_stack_top[0]) <
				yk_fixnum_to_float(yk_lisp_stack_top[1]) ? yk_tee : YK_NIL;
		} else {
			return YK_FLOAT(yk_lisp_stack_top[0]) <
				YK_FLOAT(yk_lisp_stack_top[1]) ? yk_tee : YK_NIL;
		}
	}
}

static YkObject yk_builtin_nsupeq(YkUint nargs) {
	YK_ASSERT(YK_INTP(yk_lisp_stack_top[0]) || YK_FLOATP(yk_lisp_stack_top[0]));
	YK_ASSERT(YK_INTP(yk_lisp_stack_top[1]) || YK_FLOATP(yk_lisp_stack_top[1]));

	if (YK_INTP(yk_lisp_stack_top[0])) {
		if (YK_INTP(yk_lisp_stack_top[1])) {
			return yk_lisp_stack_top[0] >= yk_lisp_stack_top[1] ? yk_tee : YK_NIL;
		} else {
			return yk_fixnum_to_float(yk_lisp_stack_top[0]) >=
				YK_FLOAT(yk_lisp_stack_top[1]) ? yk_tee : YK_NIL;
		}
	} else {
		if (YK_INTP(yk_lisp_stack_top[0])) {
			return YK_FLOAT(yk_lisp_stack_top[0]) >=
				yk_fixnum_to_float(yk_lisp_stack_top[1]) ? yk_tee : YK_NIL;
		} else {
			return YK_FLOAT(yk_lisp_stack_top[0]) >=
				YK_FLOAT(yk_lisp_stack_top[1]) ? yk_tee : YK_NIL;
		}
	}
}

static YkObject yk_builtin_ninfeq(YkUint nargs) {
	YK_ASSERT(YK_INTP(yk_lisp_stack_top[0]) || YK_FLOATP(yk_lisp_stack_top[0]));
	YK_ASSERT(YK_INTP(yk_lisp_stack_top[1]) || YK_FLOATP(yk_lisp_stack_top[1]));

	if (YK_INTP(yk_lisp_stack_top[0])) {
		if (YK_INTP(yk_lisp_stack_top[1])) {
			return yk_lisp_stack_top[0] <= yk_lisp_stack_top[1] ? yk_tee : YK_NIL;
		} else {
			return yk_fixnum_to_float(yk_lisp_stack_top[0]) <=
				YK_FLOAT(yk_lisp_stack_top[1]) ? yk_tee : YK_NIL;
		}
	} else {
		if (YK_INTP(yk_lisp_stack_top[0])) {
			return YK_FLOAT(yk_lisp_stack_top[0]) <=
				yk_fixnum_to_float(yk_lisp_stack_top[1]) ? yk_tee : YK_NIL;
		} else {
			return YK_FLOAT(yk_lisp_stack_top[0]) <=
				YK_FLOAT(yk_lisp_stack_top[1]) ? yk_tee : YK_NIL;
		}
	}
}

static YkObject yk_builtin_eq(YkUint nargs) {
	return yk_lisp_stack_top[0] == yk_lisp_stack_top[1] ? yk_tee : YK_NIL;
}

static YkObject yk_builtin_not(YkUint nargs) {
	return yk_lisp_stack_top[0] == YK_NIL ? yk_tee : YK_NIL;
}

static YkObject yk_builtin_add(YkUint nargs) {
	YkInt i_result = 0;
	float f_result = 0.f;

	char isfloat = 0;

	for (uint i = 0; i < nargs; i++) {
		if (isfloat) {
			if (YK_INTP(yk_lisp_stack_top[i])) {
				f_result += yk_fixnum_to_float(yk_lisp_stack_top[i]);
			} else if (YK_FLOATP(yk_lisp_stack_top[i])) {
				f_result += YK_FLOAT(yk_lisp_stack_top[i]);
			} else {
				YK_ASSERT(0);
			}
		} else {
			if (YK_INTP(yk_lisp_stack_top[i])) {
				i_result += YK_INT(yk_lisp_stack_top[i]);
			} else if (YK_FLOATP(yk_lisp_stack_top[i])) {
				f_result = (float)yk_signed_fixnum_to_long(i_result);
				isfloat = 1;
				f_result += YK_FLOAT(yk_lisp_stack_top[i]);
			} else {
				YK_ASSERT(0);
			}
		}
	}

	if (isfloat)
		return YK_MAKE_FLOAT(f_result);
	else
		return YK_MAKE_INT(i_result);
}

static YkObject yk_builtin_sub(YkUint nargs) {
	if (nargs == 1) {
		YK_ASSERT(YK_INTP(yk_lisp_stack_top[0]) || YK_FLOATP(yk_lisp_stack_top[0]));

		if (YK_INTP(yk_lisp_stack_top[0]))
			return YK_MAKE_INT(-YK_INT(yk_lisp_stack_top[0]));
		else
			return YK_MAKE_FLOAT(-YK_FLOAT(yk_lisp_stack_top[0]));
	}

	YkInt i_result;
	float f_result;
	char isfloat;

	if (YK_INTP(yk_lisp_stack_top[0])) {
		i_result = YK_INT(yk_lisp_stack_top[0]);
		isfloat = 0;
	} else {
		f_result = YK_FLOAT(yk_lisp_stack_top[0]);
		isfloat = 1;
	}

	for (uint i = 1; i < nargs; i++) {
		if (isfloat) {
			if (YK_INTP(yk_lisp_stack_top[i])) {
				f_result -= yk_signed_fixnum_to_long(YK_INT(yk_lisp_stack_top[i]));
			} else if (YK_FLOATP(yk_lisp_stack_top[i])) {
				f_result -= YK_FLOAT(yk_lisp_stack_top[i]);
			} else {
				YK_ASSERT(0);
			}
		} else {
			if (YK_INTP(yk_lisp_stack_top[i])) {
				i_result -= YK_INT(yk_lisp_stack_top[i]);
			} else if (YK_FLOATP(yk_lisp_stack_top[i])) {
				f_result = (float)yk_signed_fixnum_to_long(i_result);
				isfloat = 1;
				f_result -= YK_FLOAT(yk_lisp_stack_top[i]);
			} else {
				YK_ASSERT(0);
			}
		}
	}

	if (isfloat)
		return YK_MAKE_FLOAT(f_result);
	else
		return YK_MAKE_INT(i_result);
}

static YkObject yk_builtin_mul(YkUint nargs) {
	YkInt i_result = 1;
	float f_result = 1.f;

	char isfloat = 0;

	for (uint i = 0; i < nargs; i++) {
		if (isfloat) {
			if (YK_INTP(yk_lisp_stack_top[i])) {
				f_result *= yk_fixnum_to_float(yk_lisp_stack_top[i]);
			} else if (YK_FLOATP(yk_lisp_stack_top[i])) {
				f_result *= YK_FLOAT(yk_lisp_stack_top[i]);
			} else {
				YK_ASSERT(0);
			}
		} else {
			if (YK_INTP(yk_lisp_stack_top[i])) {
				i_result *= YK_INT(yk_lisp_stack_top[i]);
			} else if (YK_FLOATP(yk_lisp_stack_top[i])) {
				f_result = (float)yk_signed_fixnum_to_long(i_result);
				isfloat = 1;
				f_result *= YK_FLOAT(yk_lisp_stack_top[i]);
			} else {
				YK_ASSERT(0);
			}
		}
	}

	if (isfloat)
		return YK_MAKE_FLOAT(f_result);
	else
		return YK_MAKE_INT(i_result);
}

static YkObject yk_builtin_div(YkUint nargs) {
	if (nargs == 1) {
		YK_ASSERT(YK_INTP(yk_lisp_stack_top[0]) || YK_FLOATP(yk_lisp_stack_top[0]));

		if (YK_INTP(yk_lisp_stack_top[0]))
			return YK_MAKE_FLOAT(1.f / yk_fixnum_to_float(yk_lisp_stack_top[0]));
		else
			return YK_MAKE_FLOAT(1.f / YK_FLOAT(yk_lisp_stack_top[0]));
	}

	YkInt i_result;
	float f_result;
	char isfloat;

	if (YK_INTP(yk_lisp_stack_top[0])) {
		i_result = YK_INT(yk_lisp_stack_top[0]);
		isfloat = 0;
	} else {
		f_result = YK_FLOAT(yk_lisp_stack_top[0]);
		isfloat = 1;
	}

	for (uint i = 1; i < nargs; i++) {
		if (isfloat) {
			if (YK_INTP(yk_lisp_stack_top[i])) {
				f_result /= yk_fixnum_to_float(yk_lisp_stack_top[i]);
			} else if (YK_FLOATP(yk_lisp_stack_top[i])) {
				f_result /= YK_FLOAT(yk_lisp_stack_top[i]);
			} else {
				YK_ASSERT(0);
			}
		} else {
			if (YK_INTP(yk_lisp_stack_top[i])) {
				i_result /= YK_INT(yk_lisp_stack_top[i]);
			} else if (YK_FLOATP(yk_lisp_stack_top[i])) {
				f_result = (float)yk_signed_fixnum_to_long(i_result);
				isfloat = 1;
				f_result /= YK_FLOAT(yk_lisp_stack_top[i]);
			} else {
				YK_ASSERT(0);
			}
		}
	}

	if (isfloat)
		return YK_MAKE_FLOAT(f_result);
	else
		return YK_MAKE_INT(i_result);
}

static YkObject yk_builtin_pow(YkUint nargs) {
	YK_ASSERT(YK_INTP(yk_lisp_stack_top[0]) || YK_FLOATP(yk_lisp_stack_top[0]));
	YK_ASSERT(YK_INTP(yk_lisp_stack_top[1]) || YK_FLOATP(yk_lisp_stack_top[1]));

	if (YK_INTP(yk_lisp_stack_top[0])) {
		if (YK_INTP(yk_lisp_stack_top[1])) {
			return YK_MAKE_INT(powl(YK_INT(yk_lisp_stack_top[0]),
									YK_INT(yk_lisp_stack_top[1])));
		} else {
			return YK_MAKE_FLOAT(powf(yk_fixnum_to_float(yk_lisp_stack_top[0]),
									  YK_FLOAT(yk_lisp_stack_top[1])));
		}
	} else {
		if (YK_INTP(yk_lisp_stack_top[1])) {
			return YK_MAKE_FLOAT(powf(YK_FLOAT(yk_lisp_stack_top[0]),
									  yk_fixnum_to_float(yk_lisp_stack_top[1])));
		} else {
			return YK_MAKE_FLOAT(powf(YK_FLOAT(yk_lisp_stack_top[0]),
									  YK_FLOAT(yk_lisp_stack_top[1])));
		}
	}
}

static YkObject yk_builtin_cons(YkUint nargs) {
	return yk_cons(yk_lisp_stack_top[0], yk_lisp_stack_top[1]);
}

static YkObject yk_builtin_head(YkUint nargs) {
	YK_ASSERT(YK_LISTP(yk_lisp_stack_top[0]));
	if (yk_lisp_stack_top[0] == YK_NIL) return YK_NIL;

	return YK_CAR(yk_lisp_stack_top[0]);
}

static YkObject yk_builtin_tail(YkUint nargs) {
	YK_ASSERT(YK_LISTP(yk_lisp_stack_top[0]));
	if (yk_lisp_stack_top[0] == YK_NIL) return YK_NIL;

	return YK_CDR(yk_lisp_stack_top[0]);
}

static YkObject yk_builtin_second(YkUint nargs) {
	YK_ASSERT(YK_LISTP(yk_lisp_stack_top[0]));
	if (yk_lisp_stack_top[0] == YK_NIL) return YK_NIL;
	YkObject cdr = YK_CDR(yk_lisp_stack_top[0]);
	if (cdr == YK_NIL) return YK_NIL;

	return YK_CAR(cdr);
}

static YkObject yk_builtin_third(YkUint nargs) {
	YK_ASSERT(YK_LISTP(yk_lisp_stack_top[0]));
	if (yk_lisp_stack_top[0] == YK_NIL) return YK_NIL;
	YkObject cdr = YK_CDR(yk_lisp_stack_top[0]);
	if (cdr == YK_NIL) return YK_NIL;
	cdr = YK_CDR(cdr);
	if (cdr == YK_NIL) return YK_NIL;

	return YK_CAR(cdr);
}

static YkObject yk_builtin_list(YkUint nargs) {
	YkObject list = YK_NIL;
	YK_GC_PROTECT1(list);

	for (int i = nargs - 1; i >= 0; i--) {
		list = yk_cons(yk_lisp_stack_top[i], list);
	}

	YK_GC_UNPROTECT;
	return list;
}

static YkObject yk_builtin_reverse(YkUint nargs) {
	return yk_reverse(yk_lisp_stack_top[0]);
}

static YkObject yk_builtin_nreverse(YkUint nargs) {
	return yk_nreverse(yk_lisp_stack_top[0]);
}

static YkObject yk_builtin_intp(YkUint nargs) {
	return YK_INTP(yk_lisp_stack_top[0]) ? yk_tee : YK_NIL;
}

static YkObject yk_builtin_floatp(YkUint nargs) {
	return YK_FLOATP(yk_lisp_stack_top[0]) ? yk_tee : YK_NIL;
}

static YkObject yk_builtin_consp(YkUint nargs) {
	return YK_CONSP(yk_lisp_stack_top[0]) ? yk_tee : YK_NIL;
}

static YkObject yk_builtin_nullp(YkUint nargs) {
	return yk_lisp_stack_top[0] == YK_NIL ? yk_tee : YK_NIL;
}

static YkObject yk_builtin_listp(YkUint nargs) {
	return YK_LISTP(yk_lisp_stack_top[0]) ? yk_tee : YK_NIL;
}

static YkObject yk_builtin_symbolp(YkUint nargs) {
	return YK_SYMBOLP(yk_lisp_stack_top[0]) ? yk_tee : YK_NIL;
}

static YkObject yk_builtin_closurep(YkUint nargs) {
	return YK_CLOSUREP(yk_lisp_stack_top[0]) ? yk_tee : YK_NIL;
}

static YkObject yk_builtin_bytecodep(YkUint nargs) {
	return YK_BYTECODEP(yk_lisp_stack_top[0]) ? yk_tee : YK_NIL;
}

static YkObject yk_builtin_compiled_functionp(YkUint nargs) {
	return YK_CPROCP(yk_lisp_stack_top[0]) ? yk_tee : YK_NIL;
}

static YkObject yk_builtin_boundp(YkUint nargs) {
	YkObject sym = yk_lisp_stack_top[0];
	YK_ASSERT(YK_SYMBOLP(sym));

	if (YK_PTR(sym)->symbol.value == NULL)
		return YK_NIL;
	else
		return yk_tee;
}

static YkObject yk_builtin_documentation(YkUint nargs) {
	YkObject bytecode = yk_lisp_stack_top[0];
	YK_ASSERT(YK_BYTECODEP(bytecode));

	return YK_PTR(bytecode)->bytecode.docstring;
}

static YkObject yk_builtin_disassemble(YkUint nargs) {
	YkObject bytecode = yk_lisp_stack_top[0];

	yk_bytecode_disassemble(bytecode);
	return YK_NIL;
}

static YkObject yk_builtin_gensym(YkUint nargs) {
	char symbol_string[9];
	symbol_string[0] = '%';

	for (uint i = 1; i < 8; i++)
		symbol_string[i] = 'A' + (random_randint() % ('Z' - 'A'));

	symbol_string[8] = '\0';

	return yk_make_symbol_cstr(symbol_string);
}

static YkObject yk_builtin_clock(YkUint nargs) {
	clock_t time = clock();
	return YK_MAKE_FLOAT((float)time / CLOCKS_PER_SEC);
}

static YkObject yk_builtin_print(YkUint nargs) {
	yk_print(yk_lisp_stack_top[0]);
	printf("\n");
	return YK_NIL;
}

static YkObject yk_builtin_random(YkUint nargs) {
	return YK_MAKE_INT(random_randint() % YK_INT(yk_lisp_stack_top[0]));
}

static YkObject yk_builtin_set_global(YkUint nargs) {
	YkObject symbol = yk_lisp_stack_top[0];
	YkObject value = yk_lisp_stack_top[1];

	YK_ASSERT(YK_SYMBOLP(symbol) && symbol != YK_NIL);

	if (YK_PTR(symbol)->symbol.value != NULL)
		YK_ASSERT(YK_PTR(symbol)->symbol.type != yk_s_constant);

	YK_PTR(symbol)->symbol.value = value;

	return value;
}

static YkObject yk_builtin_set_macro(YkUint nargs) {
	YkObject symbol = yk_lisp_stack_top[0];
	YkObject value = yk_lisp_stack_top[1];

	YK_ASSERT(YK_SYMBOLP(symbol) && symbol != YK_NIL);

	YK_PTR(symbol)->symbol.type = yk_s_macro;
	YK_PTR(symbol)->symbol.value = value;

	return symbol;
}

static YkObject yk_builtin_register_global(YkUint nargs) {
	YkObject sym = yk_lisp_stack_top[0];
	YK_ASSERT(YK_SYMBOLP(sym) && sym != YK_NIL);

	YK_PTR(sym)->symbol.type = yk_s_normal;
	YK_PTR(sym)->symbol.declared = 1;
	return sym;
}

static YkObject yk_builtin_register_function(YkUint nargs) {
	YkObject sym = yk_lisp_stack_top[0];
	YkObject fn_nargs = yk_lisp_stack_top[1];

	YK_PTR(sym)->symbol.declared = 1;
	YK_PTR(sym)->symbol.type = yk_s_function;
	YK_PTR(sym)->symbol.function_nargs = YK_INT(fn_nargs);

	return sym;
}


static YkObject yk_builtin_length(YkUint nargs) {
	YK_ASSERT(YK_LISTP(yk_lisp_stack_top[0]));

	return YK_MAKE_INT(yk_length(yk_lisp_stack_top[0]));
}

static YkObject yk_builtin_arguments_length(YkUint nargs) {
	YkObject lambda_list = yk_lisp_stack_top[0];
	YkInt length = 0;

	YkObject a = lambda_list;
	for (; YK_CONSP(a); a = YK_CDR(a)) {
		length++;
	}

	if (a != YK_NIL && YK_SYMBOLP(a)) {
		length = -(length + 1);
	}

	return YK_MAKE_INT(length);
}

static YkObject yk_builtin_append(YkUint nargs) {
	YkObject result = YK_NIL;
	YK_GC_PROTECT1(result);

	for (uint i = 0; i < nargs; i++) {
		YkObject list = yk_lisp_stack_top[i];

		YK_LIST_FOREACH(list, l) {
			result = yk_cons(YK_CAR(l), result);
		}
	}

	YK_GC_UNPROTECT;
	return yk_nreverse(result);
}

static YkObject yk_builtin_make_closure(YkUint nargs) {
	YkObject bytecode = yk_lisp_stack_top[0],
		environnement = yk_lisp_stack_top[1];

	YkObject closure = yk_alloc();
	closure->closure.bytecode = bytecode;
	closure->closure.lexical_env = environnement;

	return YK_TAG(closure, yk_t_closure);
}

static YkObject yk_builtin_array(YkUint nargs) {
	YkObject array = yk_make_array(nargs, YK_NIL);

	for (uint i = 0; i < nargs; i++) {
		array->array.data[i] = yk_lisp_stack_top[i];
	}

	return array;
}

static YkObject yk_builtin_make_array(YkUint nargs) {
	YkObject array = yk_make_array(YK_INT(yk_lisp_stack_top[0]),
								   yk_lisp_stack_top[1]);

	return array;
}

static YkObject yk_builtin_list_to_array(YkUint nargs) {
	YkObject list = yk_lisp_stack_top[0];
	YkObject array = yk_make_array(yk_length(list), YK_NIL);

	uint i = 0;
	YK_LIST_FOREACH(list, l) {
		YK_PTR(array)->array.data[i++] = YK_CAR(l);
	}

	return array;
}

static YkObject yk_builtin_array_to_list(YkUint nargs) {
	YkObject array = yk_lisp_stack_top[0];

	YkObject list = YK_NIL;
	YK_GC_PROTECT1(list);

	for (int i = YK_PTR(array)->array.size - 1; i >= 0; i--) {
		list = yk_cons(YK_PTR(array)->array.data[i], list);
	}

	YK_GC_UNPROTECT;
	return list;
}

static YkObject yk_string_ref(YkObject string, YkInt i) {
	uchar* string_ptr = (uchar*)YK_PTR(string)->string.data;

	while (--i >= 0) {
		YK_ASSERT(*string_ptr != '\0');

		if (*string_ptr <= 0x7f) {
			string_ptr++;
		} else if (*string_ptr >> 6 == 2) {
			YK_ASSERT("error in encoding" == 0);
		} else if (*string_ptr >> 3 == 0x1e) {
			string_ptr += 4;
		} else if (*string_ptr >> 4 == 0xe) {
			string_ptr += 3;
		} else if (*string_ptr >> 5 == 6) {
			string_ptr += 2;
		}
	}

	return YK_MAKE_INT(u_string_to_codepoint(string_ptr));
}

static YkObject yk_builtin_aref(YkUint nargs) {
	YkObject array = yk_lisp_stack_top[0];
	YkInt index = YK_INT(yk_lisp_stack_top[1]);

	if (YK_TYPEOF(array) == yk_t_array) {
		YK_ASSERT(index < (YkInt)YK_PTR(array)->array.size && index >= 0);

		return YK_PTR(array)->array.data[index];
	} else if (YK_TYPEOF(array) == yk_t_string) {
		return yk_string_ref(array, index);
	} else {
		YK_ASSERT(0);
	}

	return YK_NIL;
}

static YkObject yk_builtin_mod(YkUint nargs) {
	YK_ASSERT(YK_INTP(yk_lisp_stack_top[0]) && YK_INTP(yk_lisp_stack_top[1]));
	YkInt a = YK_INT(yk_lisp_stack_top[0]),
		b = YK_INT(yk_lisp_stack_top[1]);

	YkInt x = a % b;
	if (x < 0)
		x = b - a;

	return YK_MAKE_INT(x);
}

static YkObject yk_builtin_aset(YkUint nargs) {
	YkObject array = yk_lisp_stack_top[0];
	YkInt index = YK_INT(yk_lisp_stack_top[1]);
	YkObject value = yk_lisp_stack_top[2];

	YK_ASSERT(YK_TYPEOF(array) == yk_t_array);

	YK_ASSERT(index < (YkInt)YK_PTR(array)->array.size && index >= 0);
	YK_PTR(array)->array.data[index] = value;

	return value;
}

static YkObject yk_builtin_make_file_stream(YkUint nargs) {
	return yk_make_file_stream(yk_lisp_stack_top[0], yk_lisp_stack_top[1], NULL);
}

static YkObject yk_builtin_make_input_string_stream(YkUint nargs) {
	YK_ASSERT(yk_lisp_stack_top[0]->t.t == yk_t_string);

	return yk_make_input_string_stream(yk_string_to_c_str(yk_lisp_stack_top[0]));
}

static YkObject yk_builtin_make_output_string_stream(YkUint nargs) {
	return yk_make_output_string_stream();
}

static YkObject yk_builtin_stream_string(YkUint nargs) {
	YK_ASSERT(yk_lisp_stack_top[0]->t.t == yk_t_string_stream);
	YK_ASSERT(!(yk_lisp_stack_top[0]->string_stream.flags & YK_STREAM_FINISHED_BIT));

	return yk_stream_string(yk_lisp_stack_top[0]);
}

static YkObject yk_builtin_stream_read_byte(YkUint nargs) {
	YkObject stream = yk_lisp_stack_top[0];
	YkInt byte = yk_stream_read_byte(stream);

	if (byte < 0)
		return yk_symbol_eof;

	return YK_MAKE_INT(byte);
}

static YkObject yk_builtin_stream_write_byte(YkUint nargs) {
	YkObject stream = yk_lisp_stack_top[0],
		byte = yk_lisp_stack_top[1];

	yk_stream_write_byte(stream, YK_INT(byte));
	return YK_NIL;
}

static YkObject yk_builtin_stream_read_char(YkUint nargs) {
	YkObject stream = yk_lisp_stack_top[0];
	YkInt character = yk_stream_read_char(stream);

	if (character < 0)
		return yk_symbol_eof;

	return YK_MAKE_INT(character);
}

static YkObject yk_builtin_stream_write_char(YkUint nargs) {
	YkObject stream = yk_lisp_stack_top[0],
		character = yk_lisp_stack_top[1];

	yk_stream_write_char(stream, YK_INT(character));
	return YK_NIL;
}

static YkObject yk_builtin_stream_close(YkUint nargs) {
	yk_stream_close(yk_lisp_stack_top[0]);
	return YK_NIL;
}

static YkObject yk_builtin_gc(YkUint nargs) {
	yk_gc();
	return YK_NIL;
}

static YkObject yk_arglist(YkUint nargs) {
	YkObject list = YK_NIL;
	YK_GC_PROTECT1(list);

	YkObject* last_frame_ptr = (YkObject*) *yk_lisp_frame_ptr,
		*last_stack_ptr = yk_lisp_stack_top + 4;

	YkUint argcount = last_frame_ptr - last_stack_ptr,
		offset = YK_INT(yk_lisp_stack_top[0]);

	for (int i = argcount - offset - 1; i >= 0; i--) {
		list = yk_cons(last_stack_ptr[offset + i], list);
	}

	YK_GC_UNPROTECT;
	return list;
}

static YkObject yk_builtin_breakpoint(YkUint nargs) {
	raise(SIGINT);
	return YK_NIL;
}

static YkObject yk_default_debugger(YkUint nargs) {
	YkObject error = yk_lisp_stack_top[0];

	printf("Unhandled error ");
	yk_print(error);
	printf("\n");

	YkObject *stack_ptr = yk_lisp_stack_top,
		*frame_ptr = yk_lisp_frame_ptr;

	while (stack_ptr < yk_lisp_stack + YK_STACK_MAX_SIZE) {
		for (; stack_ptr != frame_ptr; stack_ptr++) {
			printf("\t");
			yk_print(*stack_ptr);
			printf("\n");
		}

		if (frame_ptr < yk_lisp_stack + YK_STACK_MAX_SIZE) {
			YkObject bytecode = frame_ptr[2];
			printf("---%s----\n", yk_symbol_cstr(YK_PTR(bytecode)->bytecode.name));

			frame_ptr = (YkObject*) *frame_ptr;
			stack_ptr += 3;
		}
	}

	printf("1] ABORT TO TOPLEVEL\n"
		"2] DEBBUGER BREAKPOINT\n");

make_choice:
	printf("> ");
	int choice;
	m_scanf("%d", &choice);

	if (choice == 1) {
		yk_value_register = error;
		longjmp(yk_jump_buf, 1);
	}
	if (choice == 2) {
		assert(0);
	}
	else {
		goto make_choice;
	}

	return YK_NIL;
}

static YkUint yk_length(YkObject list) {
	YkUint i = 0;
	YK_LIST_FOREACH(list, l) {
		i++;
	}

	return i;
}

static bool yk_member(YkObject element, YkObject list) {
	YK_LIST_FOREACH(list, e) {
		if (YK_CAR(e) == element)
			return true;
	}

	return false;
}

YkObject yk_apply(YkObject function, YkObject args) {
	YkObject result = YK_NIL;

	YK_GC_PROTECT2(args, result);

	YkInt argcount = 0;

	static YkInstruction yk_end = {YK_NIL, 0, YK_OP_END};

	YK_PUSH(yk_lisp_stack_top, yk_bytecode_register);
	YK_PUSH(yk_lisp_stack_top, &yk_end);
	YK_PUSH(yk_lisp_stack_top, yk_lisp_frame_ptr);

	yk_lisp_frame_ptr = yk_lisp_stack_top;

	args = yk_reverse(args);
	YK_LIST_FOREACH(args, e) {
		YK_PUSH(yk_lisp_stack_top, YK_CAR(e));
		argcount++;
	}

	if (YK_BYTECODEP(function)) {
		YkInt nargs = YK_PTR(function)->bytecode.nargs;
		if (nargs >= 0) {
			YK_ASSERT(argcount == nargs);
		}
		else {
			YK_ASSERT(argcount >= -(nargs + 1));
		}

		yk_run(function);
		result = yk_value_register;
	} else if (YK_CPROCP(function)) {
		YkInt nargs = YK_PTR(function)->c_proc.nargs;
		if (nargs >= 0) {
			YK_ASSERT(argcount == nargs);
		}
		else {
			YK_ASSERT(argcount >= -(nargs + 1));
		}

		result = YK_PTR(function)->c_proc.cfun(yk_program_counter->modifier);
		yk_lisp_stack_top = yk_lisp_frame_ptr;

		YK_LISP_STACK_POP(yk_lisp_frame_ptr, YkObject**);
		YK_LISP_STACK_POP(yk_bytecode_register, YkObject*);
		YK_LISP_STACK_POP(yk_program_counter, YkInstruction**);
	} else {
		assert(0);
	}

	YK_GC_UNPROTECT;
	return result;
}

static YkObject yk_keyword_quote, yk_keyword_let, yk_keyword_lambda, yk_keyword_setq,
	yk_keyword_comptime, yk_keyword_do, yk_keyword_if, yk_keyword_dynamic_let,
	yk_keyword_with_cont, yk_keyword_exit, yk_keyword_loop,
	yk_stream_console_output, yk_stream_console_input,
	yk_make_closure_cfun;

void yk_init() {
	yk_gc_stack_size = 0;
	yk_gc_protected_stack_size = 0;

	yk_lisp_stack_top = yk_lisp_stack + YK_STACK_MAX_SIZE;
	yk_lisp_frame_ptr = yk_lisp_stack_top;
	yk_dynamic_bindings_stack_top = yk_dynamic_bindings_stack + YK_STACK_MAX_SIZE;
	yk_continuations_stack_top = yk_continuations_stack + YK_STACK_MAX_SIZE;
	yk_value_register = YK_NIL;
	yk_program_counter = NULL;

	yk_allocator_init();
	yk_array_allocator_init();
	yk_symbol_table_init();

	/* Special symbols */
	yk_tee = yk_make_symbol_cstr("t");
	YK_PTR(yk_tee)->symbol.value = yk_tee;
	YK_PTR(yk_tee)->symbol.type = yk_s_constant;

	yk_symbol_environnement = yk_make_symbol_cstr("*environnement*");

	yk_nil = yk_make_symbol_cstr("nil");
	YK_PTR(yk_nil)->symbol.value = YK_NIL;
	YK_PTR(yk_nil)->symbol.type = yk_s_constant;

	yk_keyword_quote = yk_make_symbol_cstr("quote");
	yk_keyword_let = yk_make_symbol_cstr("let");
	yk_keyword_setq = yk_make_symbol_cstr("set!");
	yk_keyword_lambda = yk_make_symbol_cstr("named-lambda");
	yk_keyword_comptime = yk_make_symbol_cstr("comptime");
	yk_keyword_do = yk_make_symbol_cstr("do");
	yk_keyword_if = yk_make_symbol_cstr("if");
	yk_keyword_dynamic_let = yk_make_symbol_cstr("dynamic-let");
	yk_keyword_with_cont = yk_make_symbol_cstr("with-cont");
	yk_keyword_exit = yk_make_symbol_cstr("exit");
	yk_keyword_loop = yk_make_symbol_cstr("loop");

	yk_symbol_file_mode_input = yk_make_symbol_cstr("input");
	yk_symbol_file_mode_output = yk_make_symbol_cstr("output");
	yk_symbol_file_mode_append = yk_make_symbol_cstr("append");
	yk_symbol_file_mode_binary_input = yk_make_symbol_cstr("binary-input");
	yk_symbol_file_mode_binary_output = yk_make_symbol_cstr("binary-output");
	yk_symbol_file_mode_binary_append = yk_make_symbol_cstr("binary-append");

	yk_symbol_eof = yk_make_symbol_cstr("eof");

	/* Streams */
	yk_stream_console_output = yk_make_file_stream(YK_NIL, yk_symbol_file_mode_output, stdout);
	yk_permanent_gc_protect(yk_stream_console_output);

	yk_stream_console_input = yk_make_file_stream(YK_NIL, yk_symbol_file_mode_input, stdin);
	yk_permanent_gc_protect(yk_stream_console_input);

	yk_var_output = yk_make_symbol_cstr("*output*");
	YK_PTR(yk_var_output)->symbol.declared = true;
	YK_PTR(yk_var_output)->symbol.value = yk_stream_console_output;

	/* Functions */
	YkObject arglist_symbol = yk_make_symbol_cstr("arglist");
	yk_arglist_cfun = yk_make_global_function(arglist_symbol, 1, yk_arglist);

	yk_permanent_gc_protect(yk_arglist_cfun);

	/* Builtin functions */
	yk_make_builtin("+", -1, yk_builtin_add);
	yk_make_builtin("*", -1, yk_builtin_mul);
	yk_make_builtin("/", -1, yk_builtin_div);
	yk_make_builtin("-", -1, yk_builtin_sub);
	yk_make_builtin("**", 2, yk_builtin_pow);
	yk_make_builtin("mod", 2, yk_builtin_mod);

	yk_make_builtin("=", 2, yk_builtin_neq);
	yk_make_builtin(">", 2, yk_builtin_nsup);
	yk_make_builtin("<", 2, yk_builtin_ninf);
	yk_make_builtin(">=", 2, yk_builtin_nsupeq);
	yk_make_builtin("<=", 2, yk_builtin_ninfeq);

	yk_make_builtin("eq?", 2, yk_builtin_eq);

	yk_make_builtin(":", 2, yk_builtin_cons);
	yk_make_builtin("head", 1, yk_builtin_head);
	yk_make_builtin("tail", 1, yk_builtin_tail);
	yk_make_builtin("first", 1, yk_builtin_head);
	yk_make_builtin("second", 1, yk_builtin_second);
	yk_make_builtin("third", 1, yk_builtin_third);
	yk_make_builtin("list", -1, yk_builtin_list);
	yk_make_builtin("length", 1, yk_builtin_length);
	yk_make_builtin("lambda-list-length", 1, yk_builtin_arguments_length);
	yk_make_builtin("not", 1, yk_builtin_not);
	yk_make_builtin("append", -1, yk_builtin_append);
	yk_make_builtin("reverse", 1, yk_builtin_reverse);
	yk_make_builtin("reverse!", 1, yk_builtin_nreverse);

	YkObject array_sym = yk_make_symbol_cstr("array");
	yk_array_cfun = yk_make_global_function(array_sym, -1, yk_builtin_array);
	yk_permanent_gc_protect(yk_array_cfun);

	YK_PTR(array_sym)->symbol.value = yk_array_cfun;
	YK_PTR(array_sym)->symbol.declared = 1;
	YK_PTR(array_sym)->symbol.type = yk_s_function;
	YK_PTR(array_sym)->symbol.function_nargs = -1;

	yk_make_closure_cfun = yk_make_global_function(yk_make_symbol_cstr("make-closure"),
												   2, yk_builtin_make_closure);
	yk_permanent_gc_protect(yk_make_closure_cfun);

	yk_make_builtin("make-array", 2, yk_builtin_make_array);
	yk_make_builtin("aref", 2, yk_builtin_aref);
	yk_make_builtin("aset!", 3, yk_builtin_aset);
	yk_make_builtin("list->array", 1, yk_builtin_list_to_array);
	yk_make_builtin("array->list", 1, yk_builtin_array_to_list);

	yk_make_builtin("make-file-stream", 2, yk_builtin_make_file_stream);
	yk_make_builtin("make-string-input-stream", 1, yk_builtin_make_input_string_stream);
	yk_make_builtin("make-string-output-stream", 0, yk_builtin_make_output_string_stream);

	yk_make_builtin("stream-string", 1, yk_builtin_stream_string);

	yk_make_builtin("read-byte", 1, yk_builtin_stream_read_byte);
	yk_make_builtin("write-byte!", 2, yk_builtin_stream_write_byte);
	yk_make_builtin("read-char", 1, yk_builtin_stream_read_char);
	yk_make_builtin("write-char!", 2, yk_builtin_stream_write_char);

	yk_make_builtin("stream-close", 1, yk_builtin_stream_close);

	yk_make_builtin("gc", 0, yk_builtin_gc);

	yk_make_builtin("int?", 1, yk_builtin_intp);
	yk_make_builtin("float?", 1, yk_builtin_floatp);
	yk_make_builtin("pair?", 1, yk_builtin_consp);
	yk_make_builtin("null?", 1, yk_builtin_nullp);
	yk_make_builtin("list?", 1, yk_builtin_listp);
	yk_make_builtin("symbol?", 1, yk_builtin_symbolp);
	yk_make_builtin("closure?", 1, yk_builtin_closurep);
	yk_make_builtin("bytecode?", 1, yk_builtin_bytecodep);
	yk_make_builtin("compiled-function?", 1, yk_builtin_compiled_functionp);

	yk_make_builtin("documentation", 1, yk_builtin_documentation);
	yk_make_builtin("disassemble", 1, yk_builtin_disassemble);
	yk_make_builtin("bound?", 1, yk_builtin_boundp);
	yk_make_builtin("gensym", 0, yk_builtin_gensym);

	yk_make_builtin("set-global!", 2, yk_builtin_set_global);
	yk_make_builtin("set-macro!", 2, yk_builtin_set_macro);
	yk_make_builtin("register-global!", 1, yk_builtin_register_global);
	yk_make_builtin("register-function!", 2, yk_builtin_register_function);

	yk_make_builtin("clock", 0, yk_builtin_clock);
	yk_make_builtin("random", 1, yk_builtin_random);

	yk_make_builtin("print", 1, yk_builtin_print);

	yk_make_builtin("breakpoint", 0, yk_builtin_breakpoint);
	yk_make_builtin("invoke-debugger", 1, yk_default_debugger);
}

static void yk_assert(const char* expression, const char* file, uint32_t line) {
	char error_name[100];
	snprintf(error_name, sizeof(error_name), "<assertion-error '%s' at %s:%d>", expression, file, line);

	YkObject sym = yk_make_symbol_cstr(error_name);
	YK_PUSH(yk_lisp_stack_top, sym);
	yk_default_debugger(1);
}

YkObject yk_make_symbol(const char* name, uint size) {
	YK_ASSERT(*name != '\0');
	YkObject sym = YK_NIL, string = YK_NIL;
	YK_GC_PROTECT2(sym, string);

	uint64_t string_hash = hash_string((uchar*)name, size);
	uint16_t index = string_hash % YK_SYMBOL_TABLE_SIZE;

	if (yk_symbol_table[index] == NULL) {
		string = yk_make_string(name, size);

		sym = yk_alloc();

		sym->symbol.name = string;
		sym->symbol.hash = string_hash;
		sym->symbol.value = NULL;
		sym->symbol.next_sym = NULL;
		sym->symbol.type = yk_s_normal;
		sym->symbol.function_nargs = 0;
		sym->symbol.declared = 0;

		sym = YK_TAG_SYMBOL(sym);
		yk_symbol_table[index] = sym;

		YK_GC_UNPROTECT;
		return sym;
	} else {
		YkObject s;
		for (s = yk_symbol_table[index];
			 YK_PTR(s)->symbol.hash != string_hash &&
				 YK_PTR(s)->symbol.next_sym != NULL;
			 s = YK_PTR(s)->symbol.next_sym);

		if (YK_PTR(s)->symbol.hash == string_hash) {
			if (s == yk_nil) {
				YK_GC_UNPROTECT;
				return YK_NIL;
			} else {
				YK_GC_UNPROTECT;
				return s;
			}
		} else {
			string = yk_make_string(name, size);

			sym = yk_alloc();

			sym->symbol.name = string;
			sym->symbol.hash = string_hash;
			sym->symbol.value = NULL;
			sym->symbol.next_sym = NULL;
			sym->symbol.type = yk_s_normal;
			sym->symbol.function_nargs = 0;
			sym->symbol.declared = 0;

			sym = YK_TAG_SYMBOL(sym);
			YK_PTR(s)->symbol.next_sym = sym;

			YK_GC_UNPROTECT;
			return sym;
		}
	}
}

YkObject yk_make_symbol_cstr(const char* cstr) {
	return yk_make_symbol(cstr, strlen(cstr));
}

inline static char* yk_symbol_cstr(YkObject sym) {
	return yk_string_to_c_str(YK_PTR(sym)->symbol.name);
}

static YkObject yk_make_continuation(uint16_t offset) {
	YkObject cont = yk_alloc();
	cont->continuation.t = yk_t_continuation;

	cont->continuation.lisp_stack_pointer = yk_lisp_stack_top;
	cont->continuation.lisp_frame_pointer = yk_lisp_frame_ptr;
	cont->continuation.dynamic_bindings_stack_pointer = yk_dynamic_bindings_stack_top;
	cont->continuation.bytecode_register = yk_bytecode_register;
	cont->continuation.program_counter = YK_PTR(yk_bytecode_register)->bytecode.code + offset;
	cont->continuation.exited = 0;

	return cont;
}

static YkObject yk_make_array(YkUint size, YkObject element) {
	YkObject array = YK_NIL;
	YK_GC_PROTECT2(element, array);
	array = yk_alloc();

	array->array.t = yk_t_array;
	array->array.size = size;
	array->array.capacity = size;
	array->array.dummy = YK_NIL;
	array->array.data = NULL;

	array->array.data = yk_array_allocator_alloc(size * sizeof(YkObject));

	for (uint i = 0; i < size; i++) {
		array->array.data[i] = element;
	}

	YK_GC_UNPROTECT;
	return array;
}

static YkObject yk_make_string(const char* cstr, size_t cstr_size) {
	YkObject string = yk_alloc();
	YkUint size = cstr_size + 1;

	string->string.t = yk_t_string;
	string->string.size = size - 1;
	string->string.data = yk_array_allocator_alloc(size);
	memcpy(string->string.data, cstr, size - 1);
	string->string.data[size - 1] = '\0';
	string->string.dummy = YK_NIL;

	return string;
}

YkObject yk_cons(YkObject car, YkObject cdr) {
	YK_GC_PROTECT2(car, cdr);

	YkObject o = yk_alloc();
	o->cons.car = car;
	o->cons.cdr = cdr;

	YK_GC_UNPROTECT;
	return YK_TAG_LIST(o);
}

#define BYTECODE_DEFAULT_SIZE 8

YkObject yk_make_bytecode_begin(YkObject name, YkInt nargs) {
	YkObject bytecode = YK_NIL;
	YK_GC_PROTECT2(bytecode, name);		/* This function can GC */

	bytecode = yk_alloc();
	bytecode->bytecode.name = name;
	bytecode->bytecode.docstring = YK_NIL;
	bytecode->bytecode.code = yk_array_allocator_alloc(8 * sizeof(YkInstruction));
	bytecode->bytecode.code_size = 0;
	bytecode->bytecode.code_capacity = 8;
	bytecode->bytecode.nargs = nargs;

	YK_GC_UNPROTECT;
	return YK_TAG(bytecode, yk_t_bytecode);
}

void yk_bytecode_emit(YkObject bytecode, YkOpcode op, uint16_t modifier, YkObject ptr) {
	YK_GC_PROTECT1(bytecode);

	YK_ASSERT(YK_BYTECODEP(bytecode));
	YkObject bytecode_ptr = YK_PTR(bytecode);

	if (bytecode_ptr->bytecode.code_size >= bytecode_ptr->bytecode.code_capacity) {
		bytecode_ptr->bytecode.code_capacity *= 2;
		YkInstruction* old_code = bytecode_ptr->bytecode.code;
		bytecode_ptr->bytecode.code = yk_array_allocator_alloc(sizeof(YkInstruction) * bytecode_ptr->bytecode.code_capacity);
		memcpy(bytecode_ptr->bytecode.code, old_code, bytecode_ptr->bytecode.code_size * sizeof(YkInstruction));
	}

	YkInstruction* last_i = bytecode_ptr->bytecode.code + bytecode_ptr->bytecode.code_size++;
	last_i->ptr = ptr;
	last_i->modifier = modifier;
	last_i->opcode = op;
	YK_GC_UNPROTECT;
}

static YkObject yk_nreverse(YkObject list) {
	YK_ASSERT(YK_LISTP(list));

	YkObject current = list,
		next, previous = YK_NIL,
		last = YK_NIL;

	while (current != YK_NIL) {
		next = YK_CDR(current);
		YK_CDR(current) = previous;
		previous = current;
		last = current;
		current = next;
	}

	return last;
}

static YkObject yk_reverse(YkObject list) {
	YkObject new_list = YK_NIL;

	YK_GC_PROTECT2(new_list, list);

	YK_LIST_FOREACH(list, l) {
		new_list = yk_cons(YK_CAR(l), new_list);
	}

	YK_GC_UNPROTECT;
	return new_list;
}

static YkObject yk_delete(YkObject element, YkObject partial_list) {
	YkObject final_list = partial_list,
		previous = YK_NIL;

	if (partial_list == YK_NIL)
		return YK_NIL;

	for (YkObject pair = partial_list; pair != YK_NIL; pair = YK_CDR(pair)) {
		if (YK_CAR(pair) == element) {
			if (pair == final_list) {
				final_list = YK_CDR(pair);
			} else {
				YK_CDR(previous) = YK_CDR(pair);
			}
		}

		previous = pair;
	}

	return final_list;
}

#define IS_BLANK(x) ((x) == ' ' || (x) == '\n' || (x) == '\t')

static YkToken yk_read_get_token(const char* string, uint32_t *offset) {
start:
	for (; IS_BLANK(string[*offset]); (*offset)++);

	char a = string[(*offset)++];
	YkToken token;

	if (a == '(') {
		token.type = YK_TOKEN_LEFT_PAREN;
		return token;
	} else if (a == ')') {
		token.type = YK_TOKEN_RIGHT_PAREN;
		return token;
	} else if (a == '"') {
		uint32_t begin_index = *offset;

		while (true) {
			a = string[(*offset)++];

			if (a == '"') {
				YkToken t;
				t.type = YK_TOKEN_STRING;
				t.data.string_info.size = *offset - begin_index - 1;
				t.data.string_info.begin_index = begin_index;
				return t;
			} else if (a == '\0') {
				YK_ASSERT("No closing double quote" && 0);
			}
		}
	} else if (a == '\0') {
		token.type = YK_TOKEN_EOF;
		return token;
	} else if (a == '.') {
		token.type = YK_TOKEN_DOT;
		return token;
	} else if (a == ';') {
		char b;
		do {
			b = string[(*offset)++];
		} while(b != '\n' || b == '\0');
		goto start;
	} else {
		uint32_t begin_index = *offset - 1;

		while (true) {
			a = string[(*offset)++];

			if (IS_BLANK(a) || a == '(' || a == ')' ||
				a == '"' || a == '\0')
			{
				uint32_t size = *offset - begin_index - 1;

				YkInt integer;
				double dfloating;

				int type = parse_number(string + begin_index, size, &integer, &dfloating);

				(*offset)--;

				if (type == 0) {
					token.type = YK_TOKEN_INT;
					token.data.integer = integer;
					return token;
				} else if (type == 1) {
					token.type = YK_TOKEN_FLOAT;
					token.data.floating = dfloating;
					return token;
				} else {
					token.type = YK_TOKEN_SYMBOL;
					token.data.string_info.begin_index = begin_index;
					token.data.string_info.size = size;
					return token;
				}
			}
		}
	}
}

static YkObject yk_read_parse_expression(const char* string, uint32_t* offset);

static YkObject yk_read_parse_sexp(const char* string, uint32_t* offset) {
	YkObject list = YK_NIL;
	YK_GC_PROTECT1(list);

	uint32_t new_offset = *offset;
	YkToken	t = yk_read_get_token(string, &new_offset);

	if (t.type == YK_TOKEN_RIGHT_PAREN) {
		*offset = new_offset;
		return list;
	} else if (t.type == YK_TOKEN_DOT) {
		YkObject temp;
		YK_GC_PROTECT1(temp);

		*offset = new_offset;
		list = yk_read_parse_expression(string, offset);

		t = yk_read_get_token(string, offset);
		YK_ASSERT(t.type == YK_TOKEN_RIGHT_PAREN);

		YK_GC_UNPROTECT;
	} else {
		YkObject temp;
		YK_GC_PROTECT1(temp);

		temp = yk_read_parse_expression(string, offset);
		list = yk_cons(temp, yk_read_parse_sexp(string, offset));

		YK_GC_UNPROTECT;
	}

	YK_GC_UNPROTECT;
	return list;
}

static YkObject yk_read_parse_expression(const char* string, uint32_t* offset) {
    YkToken t = yk_read_get_token(string, offset);

	switch (t.type) {
	case YK_TOKEN_LEFT_PAREN:
		return yk_read_parse_sexp(string, offset);
	case YK_TOKEN_STRING:
		return yk_make_string(string + t.data.string_info.begin_index,
							  t.data.string_info.size);
	case YK_TOKEN_INT:
		return YK_MAKE_INT(t.data.integer);
	case YK_TOKEN_FLOAT:
		return YK_MAKE_FLOAT(t.data.floating);
	case YK_TOKEN_SYMBOL:
		return yk_make_symbol(string + t.data.string_info.begin_index,
							  t.data.string_info.size);
	default:
		YK_ASSERT("Unexpected token" && 0);
	}

	return YK_NIL;
}

static YkObject yk_read_parse_top(const char* string, uint32_t* offset) {
	YkObject list = YK_NIL, element = YK_NIL;
	YK_GC_PROTECT2(list, element);

	uint32_t new_offset  = *offset;
	YkToken	t = yk_read_get_token(string, &new_offset);

	if (t.type != YK_TOKEN_EOF) {
		element = yk_read_parse_expression(string, offset);
		list = yk_cons(element, yk_read_parse_top(string, offset));
	}

	YK_GC_UNPROTECT;
	return list;
}

YkObject yk_read(const char* string) {
	uint32_t offset = 0;
	YkObject r = yk_read_parse_top(string, &offset);
	return YK_CAR(r);
}

void yk_print(YkObject o) {
	YkObject output = YK_PTR(yk_var_output)->symbol.value;

	switch (YK_TYPEOF(o)) {
	case yk_t_list:
		if (YK_NULL(o)) {
			yk_stream_format(output, "nil");
		} else {
			YkObject c;
			yk_stream_format(output, "(");

			for (c = o; YK_CONSP(c); c = YK_CDR(c)) {
				yk_print(YK_CAR(c));

				if (YK_CONSP(YK_CDR(c)))
					yk_stream_format(output, " ");
			}

			if (!YK_NULL(c)) {
				yk_stream_format(output, " . ");
				yk_print(c);
			}

			yk_stream_format(output, ")");
		}
		break;
	case yk_t_int:
		yk_stream_format(output, "%ld", yk_signed_fixnum_to_long(YK_INT(o)));
		break;
	case yk_t_float:
		yk_stream_format(output, "%f", YK_FLOAT(o));
		break;
	case yk_t_symbol:
		yk_stream_format(output, "%s", yk_symbol_cstr(o));
		break;
	case yk_t_bytecode:
		yk_stream_format(output, "<bytecode %s at %p>",
						 yk_symbol_cstr(YK_PTR(o)->bytecode.name),
						 YK_PTR(o));
		break;
	case yk_t_closure:
		yk_stream_format(output, "<closure at %p closing ", YK_PTR(o));
		yk_print(YK_PTR(o)->closure.lexical_env);
		yk_stream_format(output, ">");
		break;
	case yk_t_c_proc:
#if YK_PROFILE
		yk_stream_format(output, "<compiled-function %s at %p (called %ld times totaling %g s)>",
						 yk_symbol_cstr(YK_PTR(o)->c_proc.name),
						 YK_PTR(o), YK_PTR(o)->c_proc.calls,
						 (YK_PTR(o)->c_proc.time_called) / CLOCKS_PER_SEC);
#else
		yk_stream_format(output, "<compiled-function %s at %p>",
						 yk_symbol_cstr(YK_PTR(o)->c_proc.name),
						 YK_PTR(o));
#endif
		break;
	case yk_t_array:
		yk_stream_format(output, "[");
		for (uint i = 0; i < YK_PTR(o)->array.size; i++) {
			yk_print(YK_PTR(o)->array.data[i]);

			if (i != YK_PTR(o)->array.size - 1)
				yk_stream_format(output, " ");
		}
		yk_stream_format(output, "]");
		break;
	case yk_t_string:
		yk_stream_format(output, "\"");
		yk_stream_format(output, "%s\"", YK_PTR(o)->string.data);
		break;
	case yk_t_file_stream:
		yk_stream_format(output, "<file stream at %p>", YK_PTR(o));
		break;
	case yk_t_string_stream:
		yk_stream_format(output, "<string stream at %p>", YK_PTR(o));
		break;
	case yk_t_continuation:
		yk_stream_format(output, "<continuation at %p>", YK_PTR(o));
		break;
	default:
		yk_stream_format(output, "Unknown type 0x%lx!\n", YK_TYPEOF(o));
	}
}

static inline void yk_exit_continuation(YkObject exit, YkObject* cont_stack_top) {
	YK_ASSERT(!(YK_PTR(exit)->continuation.exited));

	for (YkObject* o_ptr = yk_continuations_stack_top; o_ptr != cont_stack_top; o_ptr++) {
		YK_PTR(*o_ptr)->continuation.exited = 1;
	}

	yk_continuations_stack_top = cont_stack_top;

	YkDynamicBinding* ptr = yk_dynamic_bindings_stack_top;
	YkDynamicBinding* next_ptr = YK_PTR(exit)->continuation.dynamic_bindings_stack_pointer;
	for (; ptr != next_ptr; ptr++) { /* todo */
		YK_PTR(ptr->symbol)->symbol.value = ptr->old_value;
	}

	yk_dynamic_bindings_stack_top = YK_PTR(exit)->continuation.dynamic_bindings_stack_pointer;
	yk_lisp_stack_top = YK_PTR(exit)->continuation.lisp_stack_pointer;
	yk_lisp_frame_ptr = YK_PTR(exit)->continuation.lisp_frame_pointer;
	yk_bytecode_register = YK_PTR(exit)->continuation.bytecode_register;
	yk_program_counter = YK_PTR(exit)->continuation.program_counter;

	YK_PTR(exit)->continuation.exited = 1;
}

static void yk_debug_info() {
	printf("\n ______STACK_____\n");
	YkObject* stack_ptr = yk_lisp_stack_top,
		*frame_ptr = yk_lisp_frame_ptr;
	if (yk_lisp_stack_top - yk_lisp_stack < YK_STACK_MAX_SIZE) {
		while (stack_ptr < yk_lisp_stack + YK_STACK_MAX_SIZE) {
			for (; stack_ptr != frame_ptr; stack_ptr++) {
				printf(" | ");
				yk_print(*stack_ptr);
				printf("\t\t|\n");
			}

			if (frame_ptr != yk_lisp_stack + YK_STACK_MAX_SIZE) {
				printf(" | RET ");
				yk_print(stack_ptr[2]);
				printf("\t|\n");
			}

			frame_ptr = (YkObject*)*stack_ptr;
			stack_ptr += 3;
		}
	}
	else {
		printf(" | EMPTY\t|\n");
	}
	printf(" ----------------\n");

	printf("VALUE: ");
	yk_print(yk_value_register);
	printf("\n");
}

#define YK_RUN_DEBUG 0

YkObject yk_run(YkObject bytecode) {
	YK_ASSERT(YK_BYTECODEP(bytecode));

	yk_program_counter = YK_PTR(bytecode)->bytecode.code;
	yk_bytecode_register = bytecode;

	YkObject previous_exit_cont = yk_current_exit_cont;
	yk_current_exit_cont = yk_make_continuation(YK_PTR(bytecode)->bytecode.code_size - 1);

	if (setjmp(yk_jump_buf) != 0) {
		yk_exit_continuation(yk_current_exit_cont, yk_continuations_stack + YK_STACK_MAX_SIZE);
		goto end;
	}

start:
	switch (yk_program_counter->opcode) {
	case YK_OP_FETCH_LITERAL:
		yk_value_register = yk_program_counter->ptr;
		yk_program_counter++;
		break;
	case YK_OP_FETCH_GLOBAL:
	{
		YkObject val = YK_PTR(yk_program_counter->ptr)->symbol.value;
		YK_ASSERT(val != NULL);	/* Unbound variable */
		yk_value_register = val;
		yk_program_counter++;
	}
		break;
	case YK_OP_LEXICAL_VAR:
		yk_value_register = yk_lisp_stack_top[yk_program_counter->modifier];
		yk_program_counter++;
		break;
	case YK_OP_PUSH:
		if (yk_lisp_stack_top <= yk_lisp_stack)
			panic("Stack overflow!\n");

		YK_LISP_STACK_PUSH(yk_value_register);
		yk_program_counter++;
		break;
	case YK_OP_PREPARE_CALL:
	{
		YkInstruction* next_instruction =
			YK_PTR(yk_bytecode_register)->bytecode.code + yk_program_counter->modifier;

		YK_LISP_STACK_PUSH(yk_bytecode_register);
		YK_LISP_STACK_PUSH(next_instruction);
		YK_LISP_STACK_PUSH(yk_lisp_frame_ptr);
	}

		yk_lisp_frame_ptr = yk_lisp_stack_top;
		yk_program_counter++;
		break;
	case YK_OP_CALL:
		if (YK_CLOSUREP(yk_value_register)) {
			YK_LISP_STACK_PUSH(YK_PTR(yk_value_register)->closure.lexical_env);
			yk_value_register = YK_PTR(yk_value_register)->closure.bytecode;

			goto bytecode_call_label;
		}
		else if (YK_BYTECODEP(yk_value_register)) {
			YkObject code;
			YkInt nargs;

		bytecode_call_label:
			code = yk_value_register;
			nargs = YK_PTR(code)->bytecode.nargs;
			if (nargs >= 0) {
				YK_ASSERT(yk_program_counter->modifier == nargs);
			} else {
				YK_ASSERT(yk_program_counter->modifier >= -(nargs + 1));
			}

			yk_bytecode_register = code;
			yk_program_counter = YK_PTR(code)->bytecode.code;
		}
		else if (YK_CPROCP(yk_value_register)) {
			YkObject proc = YK_PTR(yk_value_register);
			YkInt nargs = proc->c_proc.nargs;
			if (nargs >= 0) {
				YK_ASSERT(yk_program_counter->modifier == nargs);
			}
			else {
				YK_ASSERT(yk_program_counter->modifier >= -(nargs + 1));
			}

			yk_value_register = proc->c_proc.cfun(yk_program_counter->modifier);
			yk_lisp_stack_top = yk_lisp_frame_ptr;

			YK_LISP_STACK_POP(yk_lisp_frame_ptr, YkObject**);
			YK_LISP_STACK_POP(yk_program_counter, YkInstruction**);
			YK_LISP_STACK_POP(yk_bytecode_register, YkObject*);
		}
		else {
			YK_ASSERT(0);
		}
		break;
	case YK_OP_TAIL_CALL:
		if (YK_CPROCP(yk_value_register)) {
			YkObject proc = YK_PTR(yk_value_register);
			YkInt nargs = proc->c_proc.nargs;
			if (nargs >= 0) {
				YK_ASSERT(yk_program_counter->modifier == nargs);
			}
			else {
				YK_ASSERT(yk_program_counter->modifier >= -(nargs + 1));
			}

			yk_value_register = proc->c_proc.cfun(yk_program_counter->modifier);
			yk_lisp_stack_top = yk_lisp_frame_ptr;

			YK_LISP_STACK_POP(yk_lisp_frame_ptr, YkObject**);
			YK_LISP_STACK_POP(yk_program_counter, YkInstruction**);
			YK_LISP_STACK_POP(yk_bytecode_register, YkObject*);
		} else {
			YkObject code;
			YkInt nargs, argcount;

			if (YK_CLOSUREP(yk_value_register)) {
				YK_LISP_STACK_PUSH(YK_PTR(yk_value_register)->closure.lexical_env);
				code = YK_PTR(yk_value_register)->closure.bytecode;
				nargs = YK_PTR(code)->bytecode.nargs;
				argcount = yk_program_counter->modifier + 1;
			} else if (YK_BYTECODEP(yk_value_register)) {
				code = yk_value_register;
				nargs = YK_PTR(code)->bytecode.nargs;
				argcount = yk_program_counter->modifier;
			} else {
				YK_ASSERT(0);
			}

			if (nargs >= 0) {
				YK_ASSERT(yk_program_counter->modifier == nargs);
			} else {
				YK_ASSERT(yk_program_counter->modifier >= -(nargs + 1));
			}

			YkObject* stack_ptr = yk_lisp_stack_top + argcount;
			for (uint i = 0; i < argcount; i++) {
				*(yk_lisp_frame_ptr - i - 1) = *(stack_ptr - i - 1);
			}

			yk_lisp_stack_top = yk_lisp_frame_ptr - argcount;

			yk_bytecode_register = code;
			yk_program_counter = YK_PTR(code)->bytecode.code;
		}
		break;
	case YK_OP_RET:
		yk_lisp_stack_top = yk_lisp_frame_ptr;
		YK_LISP_STACK_POP(yk_lisp_frame_ptr, YkObject**);
		YK_LISP_STACK_POP(yk_program_counter, YkInstruction**);
		YK_LISP_STACK_POP(yk_bytecode_register, YkObject*);
		break;
	case YK_OP_JMP:
		yk_program_counter =
			YK_PTR(yk_bytecode_register)->bytecode.code + yk_program_counter->modifier;
		break;
	case YK_OP_JNIL:
		if (yk_value_register == YK_NIL) {
			yk_program_counter =
				YK_PTR(yk_bytecode_register)->bytecode.code + yk_program_counter->modifier;
		}
		else {
			yk_program_counter++;
		}
		break;
	case YK_OP_UNBIND:
		yk_lisp_stack_top += yk_program_counter->modifier;
		yk_program_counter++;
		break;
	case YK_OP_BIND_DYNAMIC:
	{
		YkObject sym = yk_program_counter->ptr;
		yk_dynamic_bindings_stack_top--;
		yk_dynamic_bindings_stack_top->symbol = sym;
		yk_dynamic_bindings_stack_top->old_value = YK_PTR(sym)->symbol.value;

		YK_PTR(sym)->symbol.value = yk_value_register;
	}
		yk_program_counter++;
		break;
	case YK_OP_UNBIND_DYNAMIC:
		for (uint16_t i = 0; i < yk_program_counter->modifier; i++) {
			YK_PTR(yk_dynamic_bindings_stack_top[i].symbol)->symbol.value =
				yk_dynamic_bindings_stack_top[i].old_value;
		}
		yk_dynamic_bindings_stack_top += yk_program_counter->modifier;
		yk_program_counter++;
		break;
	case YK_OP_WITH_CONT:
	{
		YkObject cont = yk_make_continuation(yk_program_counter->modifier);
		YK_PUSH(yk_continuations_stack_top, cont);
	}
		yk_program_counter++;
		break;
	case YK_OP_CONT:
		yk_value_register = yk_continuations_stack_top[yk_program_counter->modifier];
		yk_program_counter++;
		break;
	case YK_OP_EXIT_LEXICAL_CONT:
	{
		YkObject cont = yk_continuations_stack_top[yk_program_counter->modifier];
		yk_exit_continuation(cont, yk_continuations_stack_top + yk_program_counter->modifier);
		yk_continuations_stack_top++;
	}
		break;
	case YK_OP_EXIT_CLOSED_CONT:
	{
		YkInt offset = YK_INT(yk_program_counter->ptr);
		YkObject envt = yk_lisp_stack_top[yk_program_counter->modifier];
		assert(offset < YK_PTR(envt)->array.size);
		YkObject cont = YK_PTR(envt)->array.data[offset];
		yk_exit_continuation(cont, yk_continuations_stack_top + yk_program_counter->modifier);
	}
		break;
	case YK_OP_CLOSED_CONT:
	{
		YkInt offset = YK_INT(yk_program_counter->ptr);
		YkObject envt = yk_lisp_stack_top[yk_program_counter->modifier];
		assert(offset < YK_PTR(envt)->array.size);
		yk_value_register = YK_PTR(envt)->array.data[offset];
		yk_program_counter++;
	}
		break;
	case YK_OP_EXIT:
	{
		YkObject exit;
		YK_POP(yk_continuations_stack_top, YkObject*, exit);
		YK_PTR(exit)->continuation.exited = 1;
		yk_program_counter++;
	}
		break;
	case YK_OP_LEXICAL_SET:
		yk_lisp_stack_top[yk_program_counter->modifier] = yk_value_register;
		yk_program_counter++;
		break;
	case YK_OP_GLOBAL_SET:
		YK_PTR(yk_program_counter->ptr)->symbol.value = yk_value_register;
		yk_program_counter++;
		break;
	case YK_OP_CLOSED_VAR:
	{
		YkInt offset = YK_INT(yk_program_counter->ptr);
		YkObject envt = yk_lisp_stack_top[yk_program_counter->modifier];
		assert(offset < YK_PTR(envt)->array.size);
		yk_value_register = YK_PTR(envt)->array.data[offset];
	}
		yk_program_counter++;
		break;
	case YK_OP_CLOSED_SET:
	{
		YkInt offset = YK_INT(yk_program_counter->ptr);
		YkObject envt = yk_lisp_stack_top[yk_program_counter->modifier];
		YK_PTR(envt)->array.data[offset] = yk_value_register;
	}
		yk_program_counter++;
		break;
	case YK_OP_END:
		yk_lisp_stack_top = yk_lisp_frame_ptr;
		goto end;
		break;
	default:
		raise(SIGINT);
	}

#if YK_RUN_DEBUG
	yk_debug_info();
#endif

	goto start;

end:
	yk_current_exit_cont = previous_exit_cont;

#if YK_RUN_DEBUG
	yk_debug_info();
#endif

	return yk_value_register;
}

char* yk_opcode_names[] = {
	[YK_OP_FETCH_LITERAL] = "fetch-literal",
	[YK_OP_FETCH_GLOBAL] = "fetch-global",
	[YK_OP_LEXICAL_VAR] = "lexical-var",
	[YK_OP_PUSH] = "push",
	[YK_OP_PREPARE_CALL] = "prepare-call",
	[YK_OP_CALL] = "call",
	[YK_OP_TAIL_CALL] = "tail-call",
	[YK_OP_RET] = "ret",
	[YK_OP_JMP] = "jmp",
	[YK_OP_JNIL] = "jnil",
	[YK_OP_UNBIND] = "unbind",
	[YK_OP_BIND_DYNAMIC] = "bind-dynamic",
	[YK_OP_UNBIND_DYNAMIC] = "unbind-dynamic",
	[YK_OP_WITH_CONT] = "with-cont",
	[YK_OP_EXIT_LEXICAL_CONT] = "exit-lexical-cont",
	[YK_OP_EXIT_CLOSED_CONT] = "exit-closed-cont",
	[YK_OP_LEXICAL_SET] = "lexical-set",
	[YK_OP_GLOBAL_SET] = "global-set",
	[YK_OP_CLOSED_VAR] = "closed-var",
	[YK_OP_CLOSED_SET] = "closed-set",
	[YK_OP_BOX] = "box",
	[YK_OP_UNBOX] = "unbox",
	[YK_OP_EXIT] = "exit",
	[YK_OP_END] = "end"
};

void yk_bytecode_disassemble(YkObject bytecode) {
	if (YK_CLOSUREP(bytecode))
		bytecode = YK_PTR(bytecode)->closure.bytecode;

	YK_ASSERT(YK_BYTECODEP(bytecode));

	printf("Bytecode ");
	yk_print(YK_PTR(bytecode)->bytecode.name);
	printf("\n");

	for (uint i = 0; i < YK_PTR(bytecode)->bytecode.code_size; i++) {
		YkInstruction instruction = YK_PTR(bytecode)->bytecode.code[i];
		printf("%d\t(%s", i, yk_opcode_names[instruction.opcode]);

		if (instruction.opcode == YK_OP_CLOSED_VAR ||
			instruction.opcode == YK_OP_CLOSED_SET)
		{
			printf(" ");
			yk_print(instruction.ptr);
			printf(" %u)\n", instruction.modifier);
		} else if (instruction.opcode == YK_OP_FETCH_LITERAL ||
				   instruction.opcode == YK_OP_FETCH_GLOBAL  ||
				   instruction.opcode == YK_OP_GLOBAL_SET    ||
				   instruction.opcode == YK_OP_BIND_DYNAMIC  ||
				   instruction.opcode == YK_OP_UNBIND_DYNAMIC)
		{
			printf(" ");
			yk_print(instruction.ptr);
			printf(")\n");
		} else if (instruction.opcode == YK_OP_PUSH ||
				   instruction.opcode == YK_OP_RET)
		{
			printf(")\n");
		} else {
			printf(" %u)\n", instruction.modifier);
		}
	}
}

void yk_w_undeclared_var_init(YkWarning* warning, char* file, uint32_t line,
							  uint32_t character, YkObject symbol)
{
	warning->type = YK_W_UNDECLARED_VARIABLE;

	warning->file = file;
	warning->line = line;

	warning->character = character;
	warning->warning.undeclared_variable.symbol = symbol;
}

void yk_w_wrong_number_of_arguments_init(YkWarning* warning, char* file, uint32_t line,
										 uint32_t character, YkObject function_symbol,
										 YkInt expected_number, YkUint given_number)
{
	warning->type = YK_W_WRONG_NUMBER_OF_ARGUMENTS;
	warning->file = file;
	warning->line = line;
	warning->character = character;

	warning->warning.wrong_number_of_arguments.function_symbol = function_symbol;
	warning->warning.wrong_number_of_arguments.expected_number = expected_number;
	warning->warning.wrong_number_of_arguments.given_number = given_number;
}

void yk_w_assigning_to_function_init(YkWarning* warning, char* file, uint32_t line,
									 uint32_t character, YkObject function_symbol)
{
	warning->type = YK_W_WRONG_NUMBER_OF_ARGUMENTS;
	warning->file = file;
	warning->line = line;
	warning->character = character;

	warning->warning.assigning_to_function.function_symbol = function_symbol;
}

void yk_w_dynamic_bind_function_init(YkWarning* warning, char* file, uint32_t line,
									 uint32_t character, YkObject function_symbol)
{
	warning->type = YK_W_DYNAMIC_BIND_FUNCTION;
	warning->file = file;
	warning->line = line;
	warning->character = character;

	warning->warning.dynamic_bind_function.function_symbol = function_symbol;
}

void yk_w_print(YkWarning* w) {
	switch (w->type) {
	case YK_W_UNDECLARED_VARIABLE:
		printf("Undeclared variable: %s at %s:%d\n",
			   yk_symbol_cstr(w->warning.undeclared_variable.symbol),
			   w->file, w->line);
		break;
	case YK_W_WRONG_NUMBER_OF_ARGUMENTS:
		printf("Wrong number of arguments for %s: expected %ld, got %ld at %s:%d\n",
			   yk_symbol_cstr(w->warning.wrong_number_of_arguments.function_symbol),
			   w->warning.wrong_number_of_arguments.expected_number,
			   w->warning.wrong_number_of_arguments.given_number,
			   w->file, w->line);
		break;
	case YK_W_ASSIGNING_TO_FUNCTION:
		printf("Assignment to variable '%s' declared as a function at %s:%d\n",
			   yk_symbol_cstr(w->warning.assigning_to_function.function_symbol),
			   w->file, w->line);
		break;
	case YK_W_DYNAMIC_BIND_FUNCTION:
		printf("Dynamic binding to the variable '%s' declared as a function at %s:%d\n",
			   yk_symbol_cstr(w->warning.assigning_to_function.function_symbol),
			   w->file, w->line);
		break;
	}
}

void yk_w_remove_untrue(DynamicArray* warnings) {
	for (uint i = 0; i < warnings->size;) {
		YkWarning* w = dynamic_array_at(warnings, i);
		if (w->type == YK_W_UNDECLARED_VARIABLE &&
			YK_PTR(w->warning.undeclared_variable.symbol)->symbol.declared)
		{
			dynamic_array_remove(warnings, i);
		} else {
			i++;
		}
	}
}

static YkCompilerVar* yk_make_compiler_var(YkObject sym, YkCompilerVar* next) {
	YkCompilerVar* var = malloc(sizeof(YkCompilerVar));
	var->symbol = sym;
	var->type = YK_VAR_NORMAL;
	var->value_type = yk_t_start;
	var->next = next;

	return var;
}

static YkCompilerVar* yk_compiler_var_copy(YkCompilerVar* model, YkCompilerVar* next) {
	YkCompilerVar* var = malloc(sizeof(YkCompilerVar));
	var->symbol = model->symbol;
	var->type = model->type;
	var->value_type = model->value_type;
	var->next = next;

	return var;
}

static YkCompilerVar* yk_make_environnement_var(YkCompilerVar* next) {
	YkCompilerVar* var = malloc(sizeof(YkCompilerVar));
	var->symbol = yk_symbol_environnement;
	var->type = YK_VAR_ENVIRONNEMENT;
	var->value_type = yk_t_start;
	var->next = next;

	return var;
}

static YkCompilerVar* yk_make_return_var(YkCompilerVar* next) {
	YkCompilerVar* var = malloc(sizeof(YkCompilerVar));
	var->symbol = YK_NIL;
	var->type = YK_VAR_RETURN;
	var->value_type = yk_t_start;
	var->next = next;

	return var;
}

static YkCompilerVar* yk_make_unused_var(YkCompilerVar* next) {
	YkCompilerVar* var = malloc(sizeof(YkCompilerVar));
	var->symbol = YK_NIL;
	var->type = YK_VAR_UNUSED;
	var->value_type = yk_t_start;
	var->next = next;

	return var;
}

static YkClosedVar* yk_make_closed_var(YkCompilerVar* lexical_stack, YkClosedVar* next) {
	YkClosedVar* closed_vars = malloc(sizeof(YkClosedVar));
	closed_vars->lexical_stack = lexical_stack;
	closed_vars->next = next;

	return closed_vars;
}

static void yk_closed_vars_destroy_until(YkClosedVar* vars, YkClosedVar* limit) {
	while (vars != limit) {
		YkClosedVar* next = vars->next;
#ifdef _DEBUG
		m_bzero(vars, sizeof(YkClosedVar));
#endif
		free(vars);
		vars = next;
	}
}

static bool yk_closed_vars_member(YkObject element, YkClosedVar* closed) {
	for (YkClosedVar* clvar = closed; clvar != NULL; clvar = clvar->next) {
		for (YkCompilerVar* cvar = clvar->lexical_stack; cvar != NULL; cvar = cvar->next) {
			if ((cvar->type == YK_VAR_NORMAL || cvar->type == YK_VAR_BOXED) &&
				cvar->symbol == element)
			{
				return true;
			}
		}
	}

	return false;
}

static YkCompilerVar* yk_compiler_vars_nreverse(YkCompilerVar* vars) {
	YkCompilerVar *current = vars,
		*next, *previous = NULL,
		*last = NULL;

	while (current != NULL) {
		next = current->next;
		current->next = previous;
		previous = current;
		last = current;
		current = next;
	}

	return last;
}

static YkCompilerVar* yk_compiler_vars_reverse(YkCompilerVar* vars) {
	YkCompilerVar* new_vars = NULL;

	for (YkCompilerVar* v = vars; v != NULL; v = v->next) {
		new_vars = yk_compiler_var_copy(v, new_vars);
	}

	return new_vars;
}

static YkUint yk_compiler_vars_length(YkCompilerVar* vars) {
	YkUint i = 0;

	for (YkCompilerVar* cvar = vars; cvar != NULL; cvar = cvar->next)
		i++;

	return 0;
}

static YkCompilerVar* yk_compiler_vars_delete(YkObject element, YkCompilerVar* partial_list) {
	YkCompilerVar *final_list = partial_list,
		*previous = NULL;

	if (partial_list == NULL)
		return NULL;

	for (YkCompilerVar* var = partial_list; var != NULL; var = var->next) {
		if ((var->type == YK_VAR_NORMAL || var->type == YK_VAR_BOXED) &&
			var->symbol == element)
		{
			if (var == final_list) {
				final_list = var->next;
			} else {
				previous->next = var->next;
			}
		}

		previous = var;
	}

	return final_list;
}

static YkCompilerVar* yk_compiler_vars_append(YkCompilerVar* a, YkCompilerVar* b) {
	YkCompilerVar* result = NULL;

	for (YkCompilerVar* v = a; v != NULL; v = v->next) {
		result = yk_compiler_var_copy(v, result);
	}

	for (YkCompilerVar* v = b; v != NULL; v = v->next) {
		result = yk_compiler_var_copy(v, result);
	}

	return yk_compiler_vars_nreverse(result);
}

static void yk_compiler_vars_destroy_until(YkCompilerVar* vars, YkCompilerVar* limit) {
	while (vars != limit) {
		YkCompilerVar* next = vars->next;
#ifdef _DEBUG
		m_bzero(vars, sizeof(YkClosedVar));
#endif
		free(vars);
		vars = next;
	}
}

static void yk_compiler_vars_destroy(YkCompilerVar* vars) {
	yk_compiler_vars_destroy_until(vars, NULL);
}

YkInt yk_lexical_offset(YkObject symbol, YkCompilerVar* lexical_stack) {
	YkInt j = 0;
	YkCompilerVar* i;

	for (i = lexical_stack; i != NULL; i = i->next) {
		if ((i->type == YK_VAR_NORMAL || i->type == YK_VAR_BOXED)
			&& i->symbol == symbol)
		{
			break;
		}

		if (i->type == YK_VAR_RETURN) {
			j += 3;
		} else {
			j++;
		}
	}

	if (i != NULL) {
		return j;
	} else {
		return -1;
	}
}

YkInt yk_lexical_environnement_offset(YkCompilerVar* lexical_stack) {
	YkInt j = 0;
	YkCompilerVar* i;

	for (i = lexical_stack; i != NULL; i = i->next) {
		if (i->type == YK_VAR_ENVIRONNEMENT) {
			break;
		}

		if (i->type == YK_VAR_RETURN) {
			j += 3;
		} else {
			j++;
		}
	}

	if (i != NULL) {
		return j;
	} else {
		return -1;
	}
}

YkObject yk_normalize_list(YkObject list) {
	YkObject normal = YK_NIL, l = YK_NIL;
	YK_GC_PROTECT2(normal, l);

	for (l = list; YK_CONSP(l); l = YK_CDR(l)) {
		normal = yk_cons(YK_CAR(l), normal);
	}

	if (l != YK_NIL) {
		normal = yk_cons(l, normal);
	}

	YK_GC_UNPROTECT;
	return yk_nreverse(normal);
}

void yk_compiler_state_init(YkCompilerState* state, YkObject expr, DynamicArray* warnings) {
	state->expr = expr;

	state->cont_stack = NULL;
	state->lexical_stack = NULL;

	state->warnings = warnings;

	state->var_upenvs = NULL;
	state->cont_upenvs = NULL;

	state->closed_vars = NULL;
	state->closed_conts = NULL;

	state->is_tail = false;
}

static void yk_compile_loop(YkObject bytecode, YkCompilerState* state);

static void yk_compile_with_push(YkObject bytecode, YkCompilerState* state) {
	state->is_tail = false;
	yk_compile_loop(bytecode, state);
	yk_bytecode_emit(bytecode, YK_OP_PUSH, 0, YK_NIL);

	state->lexical_stack = yk_make_unused_var(state->lexical_stack);
}

static void yk_compile_combo(YkObject bytecode, YkCompilerState* new_state,
							 YkObject combo, bool is_tail)
{
	YK_LIST_FOREACH(combo, e) {
		bool compiled_is_tail = false;
		if (is_tail && !(YK_CONSP(YK_CDR(e))))
			compiled_is_tail = true;

		new_state->expr = YK_CAR(e);
		new_state->is_tail = compiled_is_tail;

		yk_compile_loop(bytecode, new_state);
	}
}

static void yk_compile_variable(YkObject bytecode, YkCompilerState* state,
								YkObject symbol, bool is_assign)
{
	YkCompilerVar* lexical_stack = state->lexical_stack;
	YkInt offset = yk_lexical_offset(symbol, lexical_stack);

	if (offset >= 0) {
		YkOpcode op = is_assign ? YK_OP_LEXICAL_SET : YK_OP_LEXICAL_VAR;
		yk_bytecode_emit(bytecode, op, offset, YK_NIL);
	} else {
		if (state->closed_vars != NULL) {
			printf("test: ");
			yk_print(symbol);
			printf("\n");

			printf("==START==\n");
			for (YkCompilerVar* e = state->closed_vars; e != NULL; e = e->next) {
				printf(" t: %d, s: ", e->type);
				yk_print(e->symbol);
				printf("\n");
			}
			printf("==END==\n");
		}

		int k = yk_lexical_offset(symbol, state->closed_vars);

		if (k >= 0) {
			YkInt offset = k;
			int environnement_offset = yk_lexical_environnement_offset(state->lexical_stack);

			YkOpcode op = is_assign ? YK_OP_CLOSED_SET : YK_OP_CLOSED_VAR;
			yk_bytecode_emit(bytecode, op, environnement_offset, YK_MAKE_INT(offset));
		} else if (YK_PTR(symbol)->symbol.type == yk_s_constant) {
			yk_bytecode_emit(bytecode, YK_OP_FETCH_LITERAL, 0, YK_PTR(symbol)->symbol.value);
		} else {
			if (YK_PTR(symbol)->symbol.value == NULL && !YK_PTR(symbol)->symbol.declared) {
				YkWarning* warning = dynamic_array_push_back(state->warnings, 1);
				yk_w_undeclared_var_init(warning, "NONE", 0, 0, symbol);
			} else if (is_assign && YK_PTR(symbol)->symbol.type == yk_s_function) {
				YkWarning* w = dynamic_array_push_back(state->warnings, 1);
				yk_w_assigning_to_function_init(w, "None", 0, 0, symbol);
			}

			YkOpcode op = is_assign ? YK_OP_GLOBAL_SET : YK_OP_FETCH_GLOBAL;
			yk_bytecode_emit(bytecode, op, 0, symbol);
		}
	}
}

static void yk_compile_let(YkObject bytecode, YkCompilerState* state,
						   YkObject bindings, YkObject body)
{
	YkUint bindings_count = 0;
	YkCompilerVar* body_lexical_stack = state->lexical_stack;

	YkCompilerState new_state = *state;

	YK_LIST_FOREACH(bindings, l) {
		YkObject pair = YK_CAR(l);
		new_state.expr = YK_CAR(YK_CDR(pair));

		yk_compile_with_push(bytecode, &new_state);
		body_lexical_stack = yk_make_compiler_var(YK_CAR(pair), body_lexical_stack);
		bindings_count++;
	}

	new_state = *state;
	new_state.lexical_stack = body_lexical_stack;

	yk_compile_combo(bytecode, &new_state, body, state->is_tail);
	yk_bytecode_emit(bytecode, YK_OP_UNBIND, bindings_count, YK_NIL);

	yk_compiler_vars_destroy_until(body_lexical_stack, state->lexical_stack);
}

static void yk_compile_dynamic_let(YkObject bytecode, YkCompilerState* state,
								   YkObject bindings, YkObject body)
{
	YkInt bindings_count = 0;

	YK_LIST_FOREACH(bindings, b) {
		YkObject pair = YK_CAR(b);

		{
			YkCompilerState new_state = *state;
			new_state.expr = YK_CAR(YK_CDR(pair));
			new_state.is_tail = false;

			yk_compile_loop(bytecode, &new_state);
		}

		yk_bytecode_emit(bytecode, YK_OP_BIND_DYNAMIC, 0, YK_CAR(pair));

		if (YK_PTR(YK_CAR(pair))->symbol.type == yk_s_function) {
			YkWarning* w = dynamic_array_push_back(state->warnings, 1);
			yk_w_dynamic_bind_function_init(w, "None", 0, 0, YK_CAR(pair));
		}

		bindings_count++;
	}

	YkCompilerState new_state = *state;
	yk_compile_combo(bytecode, &new_state, body, false);

	yk_bytecode_emit(bytecode, YK_OP_UNBIND_DYNAMIC, bindings_count, YK_NIL);
}

static void yk_compile_setq(YkObject bytecode, YkCompilerState* state,
							YkObject symbol, YkObject value)
{
	YkCompilerState new_state = *state;
	new_state.expr = value;
	new_state.is_tail = false;

	yk_compile_loop(bytecode, &new_state);

	yk_compile_variable(bytecode, state, symbol, true);
}

static void yk_compile_comptime(YkCompilerState* state, YkObject forms) {
	YK_ASSERT(state->lexical_stack == NULL && state->cont_stack == NULL);

	YkObject comptime_bytecode = yk_make_bytecode_begin(yk_make_symbol_cstr("compile-time-bytecode"), 0);
	YK_GC_PROTECT1(comptime_bytecode);

	YkCompilerState new_state;
	yk_compiler_state_init(&new_state, YK_NIL, state->warnings);

	yk_compile_combo(comptime_bytecode, &new_state, forms, false);
	yk_bytecode_emit(comptime_bytecode, YK_OP_END, 0, YK_NIL);

	yk_run(comptime_bytecode);

	YK_GC_UNPROTECT;
}

static YkCompilerVar* yk_find_closed_vars_combo(YkObject exprs, YkClosedVar* upenvs, YkObject env) {
	YkCompilerVar* closed = NULL;

	YK_LIST_FOREACH(exprs, e) {
		closed = yk_compiler_vars_append(yk_find_closed_vars(YK_CAR(e), upenvs, env), closed);
	}

	YkCompilerVar *partial_list = closed, *final_list = NULL;

	while (partial_list != NULL) {
		final_list = yk_compiler_var_copy(partial_list, final_list);
		partial_list = yk_compiler_vars_delete(partial_list->symbol, partial_list);
	}

	return final_list;
}

static YkCompilerVar* yk_find_closed_vars(YkObject expr, YkClosedVar* upenvs, YkObject env) {
	YkCompilerVar* closed = NULL;
	YK_GC_PROTECT2(expr, env);

	if (YK_CONSP(expr)) {
		YkObject first = YK_CAR(expr);
		if (first == yk_keyword_let) {
			YkObject body_env = env;
			YK_GC_PROTECT1(body_env);

			YkObject bindings = YK_CAR(YK_CDR(expr));

			YK_LIST_FOREACH(bindings, l) {
				YkObject pair = YK_CAR(l);
				YkObject expr = YK_CAR(YK_CDR(pair));

				closed = yk_compiler_vars_append(yk_find_closed_vars(expr, upenvs, env), closed);
				body_env = yk_cons(YK_CAR(pair), body_env);
			}

			closed = yk_compiler_vars_append(yk_find_closed_vars_combo(YK_CDR(YK_CDR(expr)),
																	   upenvs, body_env),
							   closed);
			YK_GC_UNPROTECT;
		} else if (first == yk_keyword_lambda) {
			YkObject lambda_env = yk_normalize_list(YK_CAR(YK_CDR(YK_CDR(expr))));
			YK_GC_PROTECT1(lambda_env);

			YkObject lambda_body = YK_CDR(YK_CDR(YK_CDR(expr)));

			closed = yk_find_closed_vars_combo(lambda_body, upenvs, lambda_env);
			YK_GC_UNPROTECT;
		} else if (first == yk_keyword_dynamic_let) {
			YkObject bindings = YK_CAR(YK_CDR(expr));
			YkObject body = YK_CDR(YK_CDR(expr));

			YK_LIST_FOREACH(bindings, l) {
				YkObject expr = YK_CAR(YK_CDR(YK_CAR(l)));
				closed = yk_compiler_vars_append(yk_find_closed_vars(expr, upenvs, env),
												 closed);
			}

			closed = yk_compiler_vars_append(yk_find_closed_vars_combo(body, upenvs, env),
											 closed);
		} else if (first == yk_keyword_setq) {
			YkObject value = YK_CAR(YK_CDR(YK_CDR(expr)));
			closed = yk_find_closed_vars(value, upenvs, env);
		} else if (first == yk_keyword_comptime) {
			YK_ASSERT(0);
		} else if (first == yk_keyword_do) {
			closed = yk_find_closed_vars_combo(YK_CDR(expr), upenvs, env);
		} else if (first == yk_keyword_if) {
			YkObject cond_clause = YK_CAR(YK_CDR(expr)),
				then_clause = YK_CAR(YK_CDR(YK_CDR(expr))),
				else_clause = YK_NIL;

			if (YK_CONSP(YK_CDR(YK_CDR(YK_CDR(expr))))) {
				else_clause = YK_CAR(YK_CDR(YK_CDR(YK_CDR(expr))));
			}

			closed = yk_compiler_vars_append(yk_find_closed_vars(cond_clause, upenvs, env),
											 yk_compiler_vars_append(yk_find_closed_vars(then_clause, upenvs, env),
																	 yk_find_closed_vars(else_clause, upenvs, env)));
		} else if (first == yk_keyword_with_cont) {
			YkObject cont_body = YK_CDR(YK_CDR(expr));
			closed = yk_find_closed_vars_combo(cont_body, upenvs, env);
		} else if (first == yk_keyword_exit) {
			YkObject value_body = YK_CAR(YK_CDR(YK_CDR(expr)));
			closed = yk_find_closed_vars(value_body, upenvs, env);
		} else if (first == yk_keyword_loop) {
			YkObject body = YK_CDR(expr);
			closed = yk_find_closed_vars_combo(body, upenvs, env);
		} else {
			YkObject operand = YK_CAR(expr);
			if (YK_SYMBOLP(operand) && YK_PTR(operand)->symbol.type == yk_s_macro) {
				YkObject macro_return =	yk_apply(YK_PTR(operand)->symbol.value, YK_CDR(expr));
				return yk_find_closed_vars(macro_return, upenvs, env);
			}

			closed = yk_find_closed_vars_combo(expr, upenvs, env);
		}
	} else if (YK_SYMBOLP(expr) && !yk_member(expr, env) &&
			   yk_closed_vars_member(expr, upenvs))
	{
		closed = yk_make_compiler_var(expr, NULL);
	}

	YK_GC_UNPROTECT;
	return closed;
}

static YkCompilerVar* yk_find_closed_conts_combo(YkObject exprs, YkClosedVar* upenvs, YkObject env) {
	YkCompilerVar* closed = NULL;

	YK_LIST_FOREACH(exprs, e) {
		closed = yk_compiler_vars_append(yk_find_closed_conts(YK_CAR(e), upenvs, env),
										 closed);
	}

	YkCompilerVar *partial_list = closed, *final_list = NULL;

	while (partial_list != NULL) {
		final_list = yk_compiler_var_copy(partial_list, final_list);
		partial_list = yk_compiler_vars_delete(partial_list->symbol, partial_list);
	}

	return final_list;
}

static YkCompilerVar* yk_find_closed_conts(YkObject expr, YkClosedVar* upenvs, YkObject env) {
	YkCompilerVar* closed = NULL;
	YK_GC_PROTECT2(expr, env);

	if (YK_CONSP(expr)) {
		YkObject first = YK_CAR(expr);
		if (first == yk_keyword_let) {
			YkObject bindings = YK_CAR(YK_CDR(expr));

			YK_LIST_FOREACH(bindings, l) {
				YkObject expr = YK_CAR(YK_CDR(YK_CAR(l)));
				closed = yk_compiler_vars_append(yk_find_closed_conts(expr, upenvs, env),
												 closed);
			}

			closed = yk_compiler_vars_append(yk_find_closed_conts_combo(YK_CDR(YK_CDR(expr)), upenvs, env),
											 closed);
		} else if (first == yk_keyword_lambda) {
			YkObject lambda_body = YK_CDR(YK_CDR(YK_CDR(expr)));
			closed = yk_find_closed_conts_combo(lambda_body, upenvs, env);
		} else if (first == yk_keyword_dynamic_let) {
			YkObject bindings = YK_CAR(YK_CDR(expr));
			YkObject body = YK_CDR(YK_CDR(expr));

			YK_LIST_FOREACH(bindings, l) {
				YkObject expr = YK_CAR(YK_CDR(YK_CAR(l)));
				closed = yk_compiler_vars_append(yk_find_closed_conts(expr, upenvs, env),
												 closed);
			}

			closed = yk_compiler_vars_append(yk_find_closed_conts_combo(body, upenvs, env),
											 closed);
		} else if (first == yk_keyword_setq) {
			YkObject value = YK_CAR(YK_CDR(YK_CDR(expr)));
			closed = yk_find_closed_conts(value, upenvs, env);
		} else if (first == yk_keyword_comptime) {
			YK_ASSERT(0);
		} else if (first == yk_keyword_do) {
			closed = yk_find_closed_conts_combo(YK_CDR(expr), upenvs, env);
		} else if (first == yk_keyword_if) {
			YkObject cond_clause = YK_CAR(YK_CDR(expr)),
				then_clause = YK_CAR(YK_CDR(YK_CDR(expr))),
				else_clause = YK_NIL;

			if (YK_CONSP(YK_CDR(YK_CDR(YK_CDR(expr))))) {
				else_clause = YK_CAR(YK_CDR(YK_CDR(YK_CDR(expr))));
			}

			closed = yk_compiler_vars_append(yk_find_closed_conts(cond_clause, upenvs, env),
											 yk_compiler_vars_append(yk_find_closed_conts(then_clause, upenvs, env),
																	 yk_find_closed_conts(else_clause, upenvs, env)));
		} else if (first == yk_keyword_with_cont) {
			YkObject cont_body = YK_CDR(YK_CDR(expr));
			YkObject new_env = yk_cons(YK_CAR(YK_CDR(expr)), env);

			closed = yk_find_closed_conts_combo(cont_body, upenvs, new_env);
		} else if (first == yk_keyword_exit) {
			YkObject symbol = YK_CAR(YK_CDR(expr));
			YkObject value_body = YK_CAR(YK_CDR(YK_CDR(expr)));

			if (YK_SYMBOLP(symbol) && !yk_member(symbol, env) &&
				yk_closed_vars_member(symbol, upenvs))
			{
				closed = yk_make_compiler_var(symbol, NULL);
			}

			closed = yk_compiler_vars_append(closed, yk_find_closed_conts(value_body, upenvs, env));
		} else if (first == yk_keyword_loop) {
			YkObject body = YK_CDR(expr);
			closed = yk_find_closed_conts_combo(body, upenvs, env);
		} else {
			YkObject operand = YK_CAR(expr);
			if (YK_SYMBOLP(operand) && YK_PTR(operand)->symbol.type == yk_s_macro) {
				YkObject macro_return =	yk_apply(YK_PTR(operand)->symbol.value, YK_CDR(expr));
				return yk_find_closed_conts(macro_return, upenvs, env);
			}

			closed = yk_find_closed_conts_combo(expr, upenvs, env);
		}
	}

	YK_GC_UNPROTECT;
	return closed;
}

static void yk_compile_exit(YkObject bytecode, YkCompilerState* state,
							YkObject symbol, YkObject value_body, bool in_value_reg);

static void yk_compile_lambda(YkObject bytecode, YkCompilerState* state, YkObject name,
							  YkObject arglist, YkObject body)
{
	YkObject lambda_bytecode = YK_NIL;
	YK_GC_PROTECT1(lambda_bytecode);

	YkCompilerVar *lambda_lexical_stack = NULL,
		*found_closed_conts, *found_closed_vars,
		*reversed_closed_vars, *reversed_closed_conts;

	YkObject l;
	YkInt argcount = 0;
	for (l = arglist; YK_CONSP(l); l = YK_CDR(l)) {
		YkObject arg = YK_CAR(l);
		YK_ASSERT(YK_SYMBOLP(arg));
		lambda_lexical_stack = yk_make_compiler_var(arg, lambda_lexical_stack);
		argcount++;
	}

	if (l != YK_NIL && YK_SYMBOLP(l)) {
		argcount = -(argcount + 1);
	}

	lambda_lexical_stack = yk_compiler_vars_nreverse(lambda_lexical_stack);
	lambda_bytecode = yk_make_bytecode_begin(name, argcount);
	if (body != YK_NIL && YK_TYPEOF(YK_CAR(body)) == yk_t_string) {
		YK_PTR(lambda_bytecode)->bytecode.docstring = YK_CAR(body);
		body = YK_CDR(body);
	}

	YkUint argcount_index = YK_PTR(lambda_bytecode)->bytecode.code_size;

	if (argcount < 0) {
		YkUint size = YK_PTR(lambda_bytecode)->bytecode.code_size + 5,
			offset = -argcount - 1;

		yk_bytecode_emit(lambda_bytecode, YK_OP_PREPARE_CALL, size, YK_NIL);
		yk_bytecode_emit(lambda_bytecode, YK_OP_FETCH_LITERAL, 0, YK_MAKE_INT(offset));
		yk_bytecode_emit(lambda_bytecode, YK_OP_PUSH, 0, YK_NIL);
		yk_bytecode_emit(lambda_bytecode, YK_OP_FETCH_LITERAL, 0, yk_arglist_cfun);
		yk_bytecode_emit(lambda_bytecode, YK_OP_CALL, 1, YK_NIL);
		yk_bytecode_emit(lambda_bytecode, YK_OP_PUSH, 0, YK_NIL);

		lambda_lexical_stack = yk_make_compiler_var(l, lambda_lexical_stack);
	}

	YkClosedVar *new_var_upenvs = yk_make_closed_var(state->lexical_stack, state->var_upenvs),
		*new_cont_upenvs = yk_make_closed_var(state->cont_stack, state->cont_upenvs);

	found_closed_vars = yk_find_closed_vars_combo(body, new_var_upenvs, YK_NIL);
	found_closed_conts = yk_find_closed_conts_combo(body, new_cont_upenvs, YK_NIL);

	YkCompilerState new_state = *state;
	new_state.lexical_stack = lambda_lexical_stack;
	new_state.var_upenvs = new_var_upenvs;
	new_state.cont_upenvs = new_cont_upenvs;
	new_state.cont_stack = NULL;
	new_state.closed_vars = found_closed_vars;
	new_state.closed_conts = found_closed_conts;

	if (found_closed_vars != NULL || found_closed_conts != NULL) {
		reversed_closed_vars = yk_compiler_vars_reverse(found_closed_vars);
		reversed_closed_conts = yk_compiler_vars_reverse(found_closed_conts);

		if (argcount < 0) {
			YK_PTR(lambda_bytecode)->bytecode.code[argcount_index + 1].ptr = YK_MAKE_INT(-argcount);

			lambda_lexical_stack = lambda_lexical_stack->next;
			lambda_lexical_stack = yk_make_compiler_var(l, yk_make_environnement_var(lambda_lexical_stack));
		} else {
			lambda_lexical_stack = yk_make_environnement_var(lambda_lexical_stack);
		}

		new_state.lexical_stack = lambda_lexical_stack;

		YK_LIST_FOREACH(body, e) { /* COMBO */
			new_state.expr = YK_CAR(e);
			new_state.is_tail = !YK_CONSP(YK_CDR(e));

			yk_compile_loop(lambda_bytecode, &new_state);
		}

		yk_bytecode_emit(lambda_bytecode, YK_OP_RET, 0, YK_NIL);

		uint32_t prep_call_index = YK_PTR(bytecode)->bytecode.code_size;

		yk_bytecode_emit(bytecode, YK_OP_PREPARE_CALL, 0, YK_NIL);
		yk_bytecode_emit(bytecode, YK_OP_PREPARE_CALL, 0, YK_NIL);

		new_state = *state;
		new_state.lexical_stack = yk_make_return_var(yk_make_return_var(new_state.lexical_stack));

		uint closed_size = 0;

		for (YkCompilerVar* e = reversed_closed_conts; e != NULL; e = e->next) {
			YkObject cont_symbol = e->symbol;

			yk_compile_exit(bytecode, &new_state, cont_symbol, YK_NIL, true);
			yk_bytecode_emit(bytecode, YK_OP_PUSH, 0, YK_NIL);
			new_state.lexical_stack = yk_make_unused_var(new_state.lexical_stack);
			closed_size++;
		}

		new_state.is_tail = false;

		for (YkCompilerVar* e = reversed_closed_vars; e != NULL; e = e->next) {
			YkObject var_symbol = e->symbol;

			new_state.expr = var_symbol;
			yk_compile_with_push(bytecode, &new_state);
			closed_size++;
		}

		yk_compiler_vars_destroy(reversed_closed_vars);
		yk_compiler_vars_destroy(reversed_closed_conts);

		yk_bytecode_emit(bytecode, YK_OP_FETCH_LITERAL, 0, yk_array_cfun);
		yk_bytecode_emit(bytecode, YK_OP_CALL, closed_size, YK_NIL);
		YK_PTR(bytecode)->bytecode.code[prep_call_index + 1].modifier = YK_PTR(bytecode)->bytecode.code_size;

		yk_bytecode_emit(bytecode, YK_OP_PUSH, 0, YK_NIL);
		yk_bytecode_emit(bytecode, YK_OP_FETCH_LITERAL, 0, lambda_bytecode);
		yk_bytecode_emit(bytecode, YK_OP_PUSH, 0, YK_NIL);
		yk_bytecode_emit(bytecode, YK_OP_FETCH_LITERAL, 0, yk_make_closure_cfun);
		yk_bytecode_emit(bytecode, YK_OP_CALL, 2, YK_NIL);

		YK_PTR(bytecode)->bytecode.code[prep_call_index].modifier =	YK_PTR(bytecode)->bytecode.code_size;
	} else {
		YK_LIST_FOREACH(body, e) { /* COMBO */
			new_state.expr = YK_CAR(e);
			new_state.is_tail = !YK_CONSP(YK_CDR(e));

			yk_compile_loop(lambda_bytecode, &new_state);
		}

		yk_bytecode_emit(lambda_bytecode, YK_OP_RET, 0, YK_NIL);
		yk_bytecode_emit(bytecode, YK_OP_FETCH_LITERAL, 0, lambda_bytecode);
	}

	yk_compiler_vars_destroy(found_closed_vars);
	yk_compiler_vars_destroy(found_closed_conts);
	yk_compiler_vars_destroy(lambda_lexical_stack);

	yk_closed_vars_destroy_until(new_var_upenvs, state->var_upenvs);
	yk_closed_vars_destroy_until(new_cont_upenvs, state->cont_upenvs);

	YK_GC_UNPROTECT;
}

static void yk_compile_if(YkObject bytecode, YkCompilerState* state, YkObject cond_clause,
						  YkObject then_clause, YkObject else_clause)
{
	YkCompilerState new_state = *state;

	new_state.is_tail = false;
	new_state.expr = cond_clause;
	yk_compile_loop(bytecode, &new_state);

	YkUint branch_offset = YK_PTR(bytecode)->bytecode.code_size;
	yk_bytecode_emit(bytecode, YK_OP_JNIL, 69, YK_NIL);

	new_state.is_tail = state->is_tail;
	new_state.expr = then_clause;
	yk_compile_loop(bytecode, &new_state);

	YkUint else_offset = YK_PTR(bytecode)->bytecode.code_size;
	YK_PTR(bytecode)->bytecode.code[branch_offset].modifier = else_offset + 1;
	yk_bytecode_emit(bytecode, YK_OP_JMP, 69, YK_NIL);

	new_state.is_tail = state->is_tail;
	new_state.expr = else_clause;
	yk_compile_loop(bytecode, &new_state);

	YK_PTR(bytecode)->bytecode.code[else_offset].modifier =	YK_PTR(bytecode)->bytecode.code_size;
}

static void yk_compile_with_cont(YkObject bytecode, YkCompilerState* state,
								 YkObject cont_sym, YkObject cont_body)
{
	uint before_size = YK_PTR(bytecode)->bytecode.code_size;
	yk_bytecode_emit(bytecode, YK_OP_WITH_CONT, 0, YK_NIL);

	YkCompilerState new_state = *state;
	new_state.cont_stack = yk_make_compiler_var(cont_sym, new_state.cont_stack);
	new_state.is_tail = false;

	yk_compile_combo(bytecode, &new_state, cont_body, false);

	uint after_size = YK_PTR(bytecode)->bytecode.code_size;
	YK_PTR(bytecode)->bytecode.code[before_size].modifier = after_size + 1;
	yk_bytecode_emit(bytecode, YK_OP_EXIT, 0, YK_NIL);
}

static void yk_compile_exit(YkObject bytecode, YkCompilerState* state,
							YkObject symbol, YkObject value_body, bool in_value_reg)
{
	if (!in_value_reg) {
		YkCompilerState new_state = *state;
		new_state.is_tail = false;
		new_state.expr = value_body;

		yk_compile_loop(bytecode, &new_state);
	}

	int k = yk_lexical_offset(symbol, state->closed_conts);

	if (k >= 0) {
		YkInt offset = k + yk_compiler_vars_length(state->closed_vars);
		int environnement_offset = yk_lexical_environnement_offset(state->lexical_stack);
		yk_bytecode_emit(bytecode, in_value_reg ? YK_OP_CLOSED_CONT : YK_OP_EXIT_CLOSED_CONT,
						 environnement_offset, YK_MAKE_INT(offset));
	} else {
		int cont_offset = yk_lexical_offset(symbol, state->cont_stack);
		YK_ASSERT(cont_offset >= 0);
		yk_bytecode_emit(bytecode, in_value_reg ? YK_OP_CONT : YK_OP_EXIT_LEXICAL_CONT,
						 cont_offset, YK_NIL);
	}
}

static void yk_compile_call(YkObject bytecode, YkCompilerState* state) {
	YkUint argcount = 0;

	YkCompilerState new_state = *state;

	if (YK_SYMBOLP(YK_CAR(state->expr)) &&
		YK_PTR(YK_CAR(state->expr))->symbol.type == yk_s_macro)
	{
		YkObject macro_return =	yk_apply(YK_PTR(YK_CAR(state->expr))->symbol.value,
										 YK_CDR(state->expr));
		new_state.expr = macro_return;
		yk_compile_loop(bytecode, &new_state);
		return;
	}

	YkObject arguments = YK_NIL, new_stack = YK_NIL;
	YK_GC_PROTECT2(arguments, new_stack);
	uint64_t prepare_call_offset;

	if (!state->is_tail) {
		new_state.lexical_stack = yk_make_return_var(new_state.lexical_stack);

		yk_bytecode_emit(bytecode, YK_OP_PREPARE_CALL, 0, YK_NIL);
		prepare_call_offset = YK_PTR(bytecode)->bytecode.code_size - 1;
	}

	arguments = yk_reverse(YK_CDR(state->expr));

	YK_LIST_FOREACH(arguments, e) {
		new_state.expr = YK_CAR(e);
		yk_compile_with_push(bytecode, &new_state);

		argcount++;
	}

	YK_GC_UNPROTECT;

	if (YK_SYMBOLP(YK_CAR(state->expr))) {
		YkObject sym = YK_CAR(state->expr);
		if (YK_PTR(sym)->symbol.type == yk_s_function) {
			YkInt function_nargs = YK_PTR(sym)->symbol.function_nargs;

			if (function_nargs < 0) {
				if ((YkInt)argcount < -(function_nargs + 1)) {
					YkWarning* warning = dynamic_array_push_back(state->warnings, 1);
					yk_w_wrong_number_of_arguments_init(warning, "NONE", 0, 0, sym, function_nargs, argcount);
				}
			} else if (function_nargs != (YkInt)argcount) {
				YkWarning* warning = dynamic_array_push_back(state->warnings, 1);
				yk_w_wrong_number_of_arguments_init(warning, "NONE", 0, 0, sym, function_nargs, argcount);
			}
		}
	}

	new_state.expr = YK_CAR(state->expr);
	new_state.is_tail = false;

	yk_compile_loop(bytecode, &new_state);

	if (state->is_tail) {
		yk_bytecode_emit(bytecode, YK_OP_TAIL_CALL, argcount, YK_NIL);
	} else {
		yk_bytecode_emit(bytecode, YK_OP_CALL, argcount, YK_NIL);
		YK_PTR(bytecode)->bytecode.code[prepare_call_offset].modifier = YK_PTR(bytecode)->bytecode.code_size;
	}
}

static void yk_compile_loop(YkObject bytecode, YkCompilerState* state) {
	YK_GC_PROTECT2(bytecode, state->expr);

	switch (YK_TYPEOF(state->expr)) {
	case yk_t_array:
	case yk_t_bytecode:
	case yk_t_c_proc:
	case yk_t_class:
	case yk_t_closure:
	case yk_t_float:
	case yk_t_instance:
	case yk_t_int:
	case yk_t_file_stream:
	case yk_t_string_stream:
	case yk_t_string:
		yk_bytecode_emit(bytecode, YK_OP_FETCH_LITERAL, 0, state->expr);
		break;
	case yk_t_symbol:
		yk_compile_variable(bytecode, state, state->expr, false);
		break;
	case yk_t_list:
	{
		if (state->expr == YK_NIL) {
			yk_bytecode_emit(bytecode, YK_OP_FETCH_LITERAL, 0, YK_NIL);
			goto end;
		}

		YkObject first = YK_CAR(state->expr);

		if (first == yk_keyword_quote) {
			YK_ASSERT(yk_length(state->expr) == 2);
			yk_bytecode_emit(bytecode, YK_OP_FETCH_LITERAL, 0, YK_CAR(YK_CDR(state->expr)));
		} else if (first == yk_keyword_let) {
			YkObject bindings = YK_CAR(YK_CDR(state->expr));
			YkObject body = YK_CDR(YK_CDR(state->expr));

			yk_compile_let(bytecode, state, bindings, body);
		} else if (first == yk_keyword_dynamic_let) {
			YkObject bindings = YK_CAR(YK_CDR(state->expr));
			YkObject body = YK_CDR(YK_CDR(state->expr));

			yk_compile_dynamic_let(bytecode, state, bindings, body);
		} else if (first == yk_keyword_setq) {
			YK_ASSERT(yk_length(state->expr) == 3);

			YkObject symbol = YK_CAR(YK_CDR(state->expr));
			YkObject value = YK_CAR(YK_CDR(YK_CDR(state->expr)));

			yk_compile_setq(bytecode, state, symbol, value);
		} else if (first == yk_keyword_comptime) {
			YkObject comptime_forms = YK_CDR(state->expr);

			yk_compile_comptime(state, comptime_forms);
		} else if (first == yk_keyword_lambda) {
			YK_ASSERT(yk_length(state->expr) >= 4);
			YK_ASSERT(YK_SYMBOLP(YK_CAR(YK_CDR(state->expr))));

			YkObject name = YK_CAR(YK_CDR(state->expr));
			YkObject arglist = YK_CAR(YK_CDR(YK_CDR(state->expr)));
			YkObject body = YK_CDR(YK_CDR(YK_CDR(state->expr)));

			yk_compile_lambda(bytecode, state, name, arglist, body);
	 	} else if (first == yk_keyword_do) {
			YkCompilerState new_state = *state;
			yk_compile_combo(bytecode, &new_state, YK_CDR(state->expr), state->is_tail);
		} else if (first == yk_keyword_if) {
			YkObject cond_clause = YK_CAR(YK_CDR(state->expr)),
				then_clause = YK_CAR(YK_CDR(YK_CDR(state->expr))),
				else_clause = YK_NIL;

			if (YK_CONSP(YK_CDR(YK_CDR(YK_CDR(state->expr))))) {
				else_clause = YK_CAR(YK_CDR(YK_CDR(YK_CDR(state->expr))));
			}

			yk_compile_if(bytecode, state, cond_clause, then_clause, else_clause);
		} else if (first == yk_keyword_with_cont) {
			YkObject cont_sym = YK_CAR(YK_CDR(state->expr));
			YkObject cont_body = YK_CDR(YK_CDR(state->expr));

			yk_compile_with_cont(bytecode, state, cont_sym, cont_body);
		} else if (first == yk_keyword_exit) {
			YK_ASSERT(yk_length(state->expr) == 3);

			YkObject symbol = YK_CAR(YK_CDR(state->expr));
			YkObject value_body = YK_CAR(YK_CDR(YK_CDR(state->expr)));

			yk_compile_exit(bytecode, state, symbol, value_body, false);
		} else if (first == yk_keyword_loop) {
			YkUint begin_size = YK_PTR(bytecode)->bytecode.code_size;
			YkObject body = YK_CDR(state->expr);

			YkCompilerState new_state = *state;
			new_state.is_tail = false;

			yk_compile_combo(bytecode, &new_state, body, false);
			yk_bytecode_emit(bytecode, YK_OP_JMP, begin_size, YK_NIL);
		} else {
			yk_compile_call(bytecode, state);
		}
	}
		break;
	}

end:
	YK_GC_UNPROTECT;
}

void yk_compile(YkObject forms, YkObject bytecode) {
	YK_ASSERT(YK_BYTECODEP(bytecode));

	DynamicArray warnings;
	DYNAMIC_ARRAY_CREATE(&warnings, YkWarning);

	YkCompilerState state;
	yk_compiler_state_init(&state, forms, &warnings);

	yk_compile_loop(bytecode, &state);
	yk_bytecode_emit(bytecode, YK_OP_END, 0, YK_NIL);

	yk_w_remove_untrue(&warnings);
	if (warnings.size != 0) {
		printf("======WARNINGS====\n");

		for (uint i = 0; i < warnings.size; i++) {
			yk_w_print(dynamic_array_at(&warnings, i));
		}
	}
}

void yk_repl() {
	char buffer[2048];
	YkObject forms = YK_NIL, bytecode = YK_NIL;

	YK_GC_PROTECT2(forms, bytecode);

	do {
		printf("\n> ");
		fgets(buffer, sizeof(buffer), stdin);

		forms = yk_read(buffer);

		bytecode = yk_make_bytecode_begin(yk_make_symbol_cstr("toplevel"), 0);
		yk_compile(forms, bytecode);

		yk_print(yk_run(bytecode));
		printf("\n");
	} while (strcmp(buffer, "bye\n") != 0);

	YK_GC_UNPROTECT;
}
