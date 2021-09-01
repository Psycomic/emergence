#include "misc.h"
#include "batch_renderer.h"
#include "render.h"
#include "yuki.h"
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
/*	Worker* worker = worker_create(signal_test, NULL);

	while (!worker_finished(worker))
		worker_update(worker);
*/
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
		keccak_hash_256((uint8_t*)message, strlen(message), hash);

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
		BinaryMatrix* matrix = binary_matrix_allocate(10, 10);
		for (uint i = 0; i < 10; i++)
			binary_matrix_set(matrix, i, i, 1);

		printf("Generator matrix:\n");
		binary_matrix_print(matrix);

		uint16_t vec = 854;
		uint16_t result;
		binary_matrix_vector_multiply((uint8_t*)&vec, matrix, (uint8_t*)&result);

		printf("Generated vector:\n");
		binary_vector_print((uint8_t*)&result, 10);

		LinearCode hamming_code = make_hamming_code(4);
		printf("Hamming code parity matrix:\n");
		binary_matrix_print(hamming_code.parity_check);

		printf("and generator matrix:\n");
		binary_matrix_print(hamming_code.generator);

		uint8_t value = 14;
		uint16_t encoded;

		binary_matrix_vector_multiply(&value, hamming_code.generator, (uint8_t*)&encoded);

		printf("%d encoded is\n", value);
		binary_vector_print((uint8_t*)&encoded, 15);

		uint8_t error;
		binary_matrix_vector_multiply((uint8_t*)&encoded, hamming_code.parity_check, &error);

		printf("And the error is in position %d!\n", error);

		uint8_t a = 0;
		uint8_t b = 9;

		printf("w(%d) = %lu\n", a, binary_vector_hamming_weight(&a, 5));
		printf("w(%d) = %lu\n", b, binary_vector_hamming_weight(&b, 5));

		printf("The distance between %d and %d is %lu!\n", a, b,
			   binary_vector_hamming_distance(&a, &b, 5));
	}

	{
		Vector2 A1 = { { 0, -10 } }, A2 = { { 100, -10 } };
		Vector2 B1 = { { 110, 0 } }, B2 = { { 110, 100 } };
		Vector2 test = vector2_line_intersection(A1, A2, B1, B2);

		printf("Point is (%.2f, %.2f)!\n", test.x, test.y);
	}

	{
		yk_init();

		YkObject fact = YK_NIL, range = YK_NIL, bytecode = YK_NIL;
		YK_GC_PROTECT3(fact, range, bytecode);

		/*
		  (func fact (x)
		  	(if (= x 0)
				1
				(* (fact (- x 1)) x)))
		 */

   		fact = yk_make_bytecode_begin(yk_make_symbol("fact"), 1);
		yk_bytecode_emit(fact, YK_OP_LEXICAL_VAR, 1, YK_NIL);
		yk_bytecode_emit(fact, YK_OP_PUSH, 0, YK_NIL);
		yk_bytecode_emit(fact, YK_OP_FETCH_LITERAL, 0, YK_MAKE_INT(0));
		yk_bytecode_emit(fact, YK_OP_PUSH, 0, YK_NIL);
		yk_bytecode_emit(fact, YK_OP_FETCH_GLOBAL, 0, yk_make_symbol("="));
		yk_bytecode_emit(fact, YK_OP_CALL, 2, YK_NIL);
		yk_bytecode_emit(fact, YK_OP_JNIL, 9, YK_NIL);
		yk_bytecode_emit(fact, YK_OP_FETCH_LITERAL, 0, YK_MAKE_INT(1));
		yk_bytecode_emit(fact, YK_OP_RET, 0, YK_NIL);
		yk_bytecode_emit(fact, YK_OP_FETCH_LITERAL, 0, YK_MAKE_INT(1));
		yk_bytecode_emit(fact, YK_OP_PUSH, 0, YK_NIL);
		yk_bytecode_emit(fact, YK_OP_LEXICAL_VAR, 2, YK_NIL);
		yk_bytecode_emit(fact, YK_OP_PUSH, 0, YK_NIL);
		yk_bytecode_emit(fact, YK_OP_FETCH_GLOBAL, 0, yk_make_symbol("-"));
		yk_bytecode_emit(fact, YK_OP_CALL, 2, YK_NIL);
		yk_bytecode_emit(fact, YK_OP_PUSH, 0, YK_NIL);
		yk_bytecode_emit(fact, YK_OP_FETCH_LITERAL, 0, fact);
		yk_bytecode_emit(fact, YK_OP_CALL, 1, YK_NIL);
		yk_bytecode_emit(fact, YK_OP_PUSH, 0, YK_NIL);
		yk_bytecode_emit(fact, YK_OP_LEXICAL_VAR, 2, YK_NIL);
		yk_bytecode_emit(fact, YK_OP_PUSH, 0, YK_NIL);
		yk_bytecode_emit(fact, YK_OP_FETCH_GLOBAL, 0, yk_make_symbol("*"));
		yk_bytecode_emit(fact, YK_OP_TAIL_CALL, 2, YK_NIL);

		/*
		  (func range (x acc)
		  	(if (= x 0)
				acc
				(range (- x 1)
					   (: x acc))))
		 */

		range = yk_make_bytecode_begin(yk_make_symbol("range"), 2);
		yk_bytecode_emit(range, YK_OP_FETCH_LITERAL, 0, YK_MAKE_INT(0));
		yk_bytecode_emit(range, YK_OP_PUSH, 0, YK_MAKE_INT(0));
		yk_bytecode_emit(range, YK_OP_LEXICAL_VAR, 2, YK_MAKE_INT(0));
		yk_bytecode_emit(range, YK_OP_PUSH, 0, YK_MAKE_INT(0));
		yk_bytecode_emit(range, YK_OP_FETCH_GLOBAL, 0, yk_make_symbol("="));
		yk_bytecode_emit(range, YK_OP_CALL, 2, YK_NIL);
		yk_bytecode_emit(range, YK_OP_JNIL, 9, YK_NIL);
		yk_bytecode_emit(range, YK_OP_LEXICAL_VAR, 2, YK_NIL);
		yk_bytecode_emit(range, YK_OP_RET, 0, YK_NIL);
		yk_bytecode_emit(range, YK_OP_LEXICAL_VAR, 2, YK_NIL);
		yk_bytecode_emit(range, YK_OP_PUSH, 0, YK_NIL);
		yk_bytecode_emit(range, YK_OP_LEXICAL_VAR, 2, YK_NIL);
		yk_bytecode_emit(range, YK_OP_PUSH, 0, YK_NIL);
		yk_bytecode_emit(range, YK_OP_FETCH_GLOBAL, 0, yk_make_symbol(":"));
		yk_bytecode_emit(range, YK_OP_CALL, 2, YK_NIL);
		yk_bytecode_emit(range, YK_OP_PUSH, 0, YK_NIL);
		yk_bytecode_emit(range, YK_OP_FETCH_LITERAL, 0, YK_MAKE_INT(1));
		yk_bytecode_emit(range, YK_OP_PUSH, 0, YK_NIL);
		yk_bytecode_emit(range, YK_OP_LEXICAL_VAR, 3, YK_NIL);
		yk_bytecode_emit(range, YK_OP_PUSH, 0, YK_NIL);
		yk_bytecode_emit(range, YK_OP_FETCH_GLOBAL, 0, yk_make_symbol("-"));
		yk_bytecode_emit(range, YK_OP_CALL, 2, YK_NIL);
		yk_bytecode_emit(range, YK_OP_PUSH, 0, YK_NIL);
		yk_bytecode_emit(range, YK_OP_FETCH_LITERAL, 0, range);
		yk_bytecode_emit(range, YK_OP_TAIL_CALL, 2, YK_NIL);

		bytecode = yk_make_bytecode_begin(yk_make_symbol("toplevel"), 0);
		yk_bytecode_emit(bytecode, YK_OP_FETCH_LITERAL, 0, YK_NIL);
		yk_bytecode_emit(bytecode, YK_OP_PUSH, 0, YK_NIL);
		yk_bytecode_emit(bytecode, YK_OP_FETCH_LITERAL, 0, YK_MAKE_INT(15));
		yk_bytecode_emit(bytecode, YK_OP_PUSH, 0, YK_NIL);
		yk_bytecode_emit(bytecode, YK_OP_FETCH_LITERAL, 0, range);
		yk_bytecode_emit(bytecode, YK_OP_CALL, 2, YK_NIL);
		yk_bytecode_emit(bytecode, YK_OP_FETCH_LITERAL, 0, YK_MAKE_INT(89));
		yk_bytecode_emit(bytecode, YK_OP_BIND_DYNAMIC, 0, yk_make_symbol("*var*"));
		yk_bytecode_emit(bytecode, YK_OP_FETCH_GLOBAL, 0, yk_make_symbol("*var*"));
		yk_bytecode_emit(bytecode, YK_OP_FETCH_LITERAL, 0, YK_MAKE_INT(689));
		yk_bytecode_emit(bytecode, YK_OP_BIND_DYNAMIC, 0, yk_make_symbol("*var*"));
		yk_bytecode_emit(bytecode, YK_OP_FETCH_GLOBAL, 0, yk_make_symbol("*var*"));
		yk_bytecode_emit(bytecode, YK_OP_UNBIND_DYNAMIC, 1, YK_NIL);
		yk_bytecode_emit(bytecode, YK_OP_FETCH_GLOBAL, 0, yk_make_symbol("*var*"));
		yk_bytecode_emit(bytecode, YK_OP_UNBIND_DYNAMIC, 0, YK_NIL);
		yk_bytecode_emit(bytecode, YK_OP_END, 0, YK_NIL);

		clock_t t1 = clock();
		yk_run(bytecode);
		clock_t t2 = clock();

		printf("\n");
		printf("Done in %lu clock_t\n", t2 - t1);

		YK_GC_UNPROTECT;

		/* yk_repl(); */
	}

	{
		random_seed(time(NULL));

		LWEPrivateKey prk;
		LWEPublicKey puk;

		lwe_generate_keys(5, 12, 5, 2, 4093, 0.0024f, &puk, &prk);

		printf("Public key A: \n");
		matrix_print_int32(puk.A);

		printf("Public key P: \n");
		matrix_print_int32(puk.P);

		printf("Private key S: \n");
		matrix_print_int32(prk.S);

		uint8_t encrypted = 25;
		binary_vector_print(&encrypted, 5);

		MatrixInt32* c;
		MatrixInt32* u;

		lwe_encrypt(&encrypted, &puk, &c, &u);

		MatrixInt32* decrypted = matrix_zeros_int32(1, 5);
		lwe_decrypt(c, u, &prk, decrypted);

		printf("Decrypted is:\n");
		matrix_print_int32(decrypted);
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

	exit(0);
}
