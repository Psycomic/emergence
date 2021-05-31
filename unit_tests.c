#include "misc.h"
#include "batch_renderer.h"
#include "render.h"
#include "ulisp.h"
#include "random.h"
#include "workers.h"
#include "crypto.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

void coroutine_test() {
	for (uint i = 0; i < 10; i++) {
		printf("Printing %u\n", i);
	}
}

void sig(void* data) {
	printf("Recieved %d!\n", (int)data);
}

int signal_test(WorkerData* data) {
	for (uint i = 0; i < 100; i++) {
		printf("I: %d\n", i);
		worker_emit(data, sig, (void*)i);
	}

	return 0;
}

void execute_tests(void) {
	Worker* worker = worker_create(signal_test, NULL);

	while (!worker_finished(worker))
		worker_update(worker);

	// string test

	char dest[8];

	strinsert(dest, "Hello, world!", "Fuck", 5, sizeof(dest));
	printf("Dest: %s\n", dest);

	// Hash table test
	HashTable* table = hash_table_create(3);

	uint value_one = 10,
		value_two = 20,
		value_three = 30,
		value_four = 40,
		value_five = 50;

	hash_table_set(table, "one", &value_one, sizeof(uint));
	hash_table_set(table, "two", &value_two, sizeof(uint));
	hash_table_set(table, "three", &value_three, sizeof(uint));
	hash_table_set(table, "four", &value_four, sizeof(uint));
	hash_table_set(table, "five", &value_five, sizeof(uint));

	value_five = 20;
	hash_table_set(table, "five", &value_five, sizeof(uint));

	assert(*(uint*)hash_table_get(table, "one") == value_one);
	assert(*(uint*)hash_table_get(table, "two") == value_two);
	assert(*(uint*)hash_table_get(table, "three") == value_three);
	assert(*(uint*)hash_table_get(table, "four") == value_four);
	assert(*(uint*)hash_table_get(table, "five") == value_five);

	long val;
	double floating;

	assert(parse_number("12345", &val, &floating) >= 0);
	assert(val == 12345);
	assert(parse_number("1hello, there", &val, &floating) < 0);

	for (uint64_t i = 0; i < 10; i++) {
		printf("Random float: %.2f\n", random_float());
	}

	printf("\n");

	{
		uint8_t aes_key[32];
		memset(aes_key, '2', sizeof(*aes_key) * 32);

		uint8_t aes_message[120];
		memset(aes_message, 'A', sizeof(*aes_message) * 120);

		uint8_t out[128];
		aes_encrypt(aes_message, 120, aes_key, out);

		for (uint i = 0; i < 128; i += sizeof(uint64_t)) {
			printf("message: 0x%016llx\n", *(uint64_t*)&out[i]);
		}
	}

	{
		const char message[] = "My friends, today is a very special day.";

		printf("Message is %s\n", message);

		uint8_t hash[32];
		keccak_hash_256((uint8_t*)message, strlen(message), hash, sizeof(hash));

		uint64_t first_count = bits_count((uint8_t*)hash, (sizeof(uint64_t) * 32) / 2);
		uint64_t second_count = bits_count((uint8_t*)hash + 32 / 2, (sizeof(uint64_t) * 32) / 2);

		printf("Ratio: %llu / %llu = %.4f\n", first_count, second_count, (float) first_count / second_count);

		for (uint i = 0; i < ARRAY_SIZE(hash); i += sizeof(uint64_t))
			printf("hash: 0x%016lx\n", *(uint64_t*)&hash[i]);
	}

	{
		uint64_t random_sequence[32];
		random_csprng_bytes(random_sequence, sizeof(random_sequence));

		for (uint i = 0; i < ARRAY_SIZE(random_sequence); i++)
			printf("random: %016llx\n", random_sequence[i]);

		randomness_test(random_csprng_randint, 2048);
	}

	{
		Vector2 A1 = { { 0, -10 } }, A2 = { { 100, -10 } };
		Vector2 B1 = { { 110, 0 } }, B2 = { { 110, 100 } };
		Vector2 test = vector2_line_intersection(A1, A2, B1, B2);

		printf("Point is (%.2f, %.2f)!\n", test.x, test.y);
	}

	{
		// (defun lerp (a b v)
		// 	    (+ a (* (- b a) v)))
		const char* lerp_proc =
			"("
			"(bind a)"
			"(bind b)"
			"(bind v)"
			"(push-cont 4)"
			"(push-cont 3)"
			"(lookup-var v)"
			"(push)"
			"(push-cont 2)"
			"(lookup-var a)"
			"(push)"
			"(lookup-var b)"
			"(push)"
			"(lookup-var -)"
			"(apply)"
			"(label 2)"
			"(push)"
			"(lookup-var *)"
			"(apply)"
			"(label 3)"
			"(push)"
			"(lookup-var a)"
			"(push)"
			"(lookup-var +)"
			"(apply)"
			"(label 4)"
			"(restore-cont)"
			")";

		LispObject* lerp_expression = ulisp_read(lerp_proc);
		LispTemplate* lerp_template = ulisp_assembly_compile(lerp_expression);
		LispObject* lerp_closure = ulisp_make_closure(lerp_template, nil);

		ulisp_add_to_environnement("lerp", lerp_closure);

		// (defun abs (x)
		//    (if (> x 0)
		//        x
		//        (- x)))

		const char* abs_string =
			"("
			"(bind x)"
			"(push-cont 5)"
			"(fetch-literal 0)"
			"(push)"
			"(lookup-var x)"
			"(push)"
			"(lookup-var >)"
			"(apply)"
			"(label 5)"
			"(branch-else 6)"
			"(lookup-var x)"	/* if true */
			"(branch 7)"
			"(label 6)"			/* if false */
			"(push-cont 7)"
			"(lookup-var x)"
			"(push)"
			"(lookup-var -)"
			"(apply)"
			"(label 7)"
			"(restore-cont)"
			")";

		LispObject* abs_expression = ulisp_read(abs_string);
		LispTemplate* abs_template = ulisp_assembly_compile(abs_expression);
		LispObject* abs_closure = ulisp_make_closure(abs_template, nil);

		ulisp_add_to_environnement("abs", abs_closure);

		const char* expression_string =
			"("
			"(push-cont 0)"
			"(fetch-literal -8)"
			"(push)"
			"(lookup-var abs)"
			"(apply)"
			"(label 0)"
			"(push)"
			")";

		LispObject* main_expression = ulisp_read(expression_string);
		LispTemplate* main_template = ulisp_assembly_compile(main_expression);
		ulisp_run(main_template);

		printf("========= ULISP BYTECODE TEST =============\n");
		printf("Expression: %s\nResult: ", expression_string);
		ulisp_print(value_register, stdout);
		printf(" !\n");

		LispObject* thing = ulisp_read("(if (> x 0) x (- x))");

		printf("Compiled: ");
		LispObject* bytecode = ulisp_compile(thing);
		ulisp_print(bytecode, stdout);
		printf("\n");
	}

	exit(0);
}
