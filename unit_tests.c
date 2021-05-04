#include "misc.h"
#include "batch_renderer.h"
#include "render.h"
#include "ulisp.h"
#include "random.h"
#include "workers.h"

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

	char buffer[2048];
	ulisp_init();

/*	while (1) {
		printf("\n> ");
		fgets(buffer, sizeof(buffer), stdin);
		if (strcmp(buffer, ":q\n") == 0)
			break;

		LispObject* obj = ulisp_eval(ulisp_read_list(buffer), nil);
		ulisp_print(obj, stdout);
		}
*/
}
