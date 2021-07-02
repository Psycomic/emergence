#include "misc.h"
#include "batch_renderer.h"
#include "render.h"
#include "ulisp.h"
#include "random.h"
#include "workers.h"
#include "crypto.h"
#include "protocol7.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

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
	HashTable* table = hash_table_create(3, hash_string);

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
		random_csprng_bytes(aes_key, 32);

		char message_text[] =
			"HELLO COMRADES! I LOVE DONALD TRUMP AND I'M A RIGHT WING EXTREMIST!!!"
			"PLEASE LOOK AT ME CIA NIGGERS I WILL SHOOT YOU TO AVENGE TERRY DAVIS";

		uint8_t message[16 * 10];
		pad_message((uint8_t*)message_text, strlen(message_text), 16, message);

		size_t message_size = 16 * 10;

		uint8_t encrypted_message[16 * 10];
		aes_encrypt_cbc((uint8_t*)message, message_size, aes_key, encrypted_message);

		uint8_t decrypted_message[16 * 10];
		aes_decrypt_cbc(encrypted_message, message_size, aes_key, decrypted_message);

		printf("Decrypted: %s\n", decrypted_message);
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
		LispObject* definition = ulisp_read(
			"(begin "
			"  (def radians->degrees "
			"     (n-lambda radians->degrees (rad)"
			"        (* (/ rad pi) 180)))"
			"  (def pi 3.1415))");

		clock_t t1 = clock();

		LispObject* result;

		ULISP_TOPLEVEL {
			ulisp_eval(definition);
			result = ulisp_eval(ulisp_read("(radians->degrees 1.0)"));
		}
		ULISP_ABORT {
			printf("Problemos...\n");
			exit(0);
		}

		clock_t t2 = clock();

		printf("Result found in %ld microseconds: ", t2 - t1);
		ulisp_print(result, ulisp_standard_output);
		printf("\n");
	}

	{
		LispObject* sstream = ulisp_make_string_stream();

		ulisp_stream_write("Hello, world!\n", sstream);
		ulisp_stream_write("FUck you!\n", sstream);

		int a = 87, b = 42;
		float c = 5.1f, d = 6.3f;

		ulisp_stream_format(sstream, "A: %d, B : %d\n", a, b);
		ulisp_stream_format(sstream, "Fuck everybody!!!!\n");
		ulisp_stream_format(sstream, "C: %f, D: %f\n", c, d);
	}

	{
		if (p7_init() < 0) {
			printf("Failed!\n");
		}
		else {
			printf("Success!\n");
		}

		p7_node_discover("127.0.0.1", P7_PORT);
/*
		while (GL_TRUE) {
			p7_loop();
			usleep(1000);
		}
*/
	}
}
