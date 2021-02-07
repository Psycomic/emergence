#include "misc.h"
#include "batch_renderer.h"
#include "render.h"
#include "ulisp.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

void execute_tests(void) {
	// Hash table test
	HashTable* table = hash_table_create(4);

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

	char buffer[2048];
	ulisp_init();

	while (1) {
		printf("\n> ");
		fgets(buffer, sizeof(buffer), stdin);
		if (strcmp(buffer, ":q\n") == 0)
			break;

		LispObject* obj = ulisp_eval(ulisp_read_list(buffer));
		ulisp_print(obj, stdout);
 	}
}
