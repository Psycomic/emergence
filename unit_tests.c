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

	ulisp_init();

	{
		uint8_t aes_key[32];
		memset(aes_key, '2', sizeof(*aes_key) * 32);

		uint8_t aes_message[120];
		memset(aes_message, 'A', sizeof(*aes_message) * 120);

		uint8_t out[128];
		aes_encrypt(aes_message, 120, aes_key, out);

		for (uint i = 0; i < 128 / sizeof(uint64_t); i++) {
			printf("message: 0x%016lx\n", *(uint64_t*)&out[i]);
		}
	}

	{
		const char message[] = "dsjiodsji osjiofdsjoi dfpsnfsoijesoiejz iez jneprinzep zieojrzo inijfsdiopijozejfoie fjzn ifzjonpefozijoazijoa zjoienaiz jenioazrj";

		printf("Message is %s\n", message);

		uint8_t hash[32];
		keccak_hash_256((uint8_t*)message, strlen(message), hash, sizeof(hash));

		uint64_t first_count = bits_count((uint8_t*)hash, (sizeof(uint64_t) * 32) / 2);
		uint64_t second_count = bits_count((uint8_t*)hash + 32 / 2, (sizeof(uint64_t) * 32) / 2);

		printf("Ratio: %lu / %lu = %.4f\n", first_count, second_count, (float) first_count / second_count);

		for (uint i = 0; i < ARRAY_SIZE(hash); i += sizeof(uint64_t))
			printf("hash: 0x%016lx\n", *(uint64_t*)&hash[i]);
	}

	exit(0);
}
