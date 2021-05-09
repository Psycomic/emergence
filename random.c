#include <stdio.h>
#include <memory.h>
#include <time.h>

#include "random.h"
#include "misc.h"
#include "crypto.h"
#include "window.h"

static unsigned long x = 123456789, y = 362436069, z = 521288629;
extern uint32_t last_character;

void random_seed(uint64_t seed) {
	x = seed;
}

uint64_t random_randint() {
	uint64_t t;

	x ^= x << 16;
    x ^= x >> 5;
    x ^= x << 1;

	t = x;
	x = y;
	y = z;
	z = t ^ x ^ y;

	return z;
}

float random_float() {
	return ((float)random_randint()) / (float)RANDOM_MAX_RAND;
}

float random_uniform(float min, float max) {
	return min + (max - min) * random_float();
}

float gaussian_random() {
	float sum = 0.f;

	for (uint8_t i = 0; i < 12; i++)
		sum += random_float();

	return sum - 6.f;
}

void random_arrayf(float* destination, uint64_t size) {
	for (uint64_t i = 0; i < size; ++i)
		destination[i] = random_float();
}

typedef struct {
	uint64_t random_seed;
	uint64_t cursor_position;
	uint32_t timestamp;
	uint64_t window_size;
	uint32_t characters[32];
	uint32_t character_capacity;
	uint8_t last_state[32];
} entropy_t;

static uint64_t csprng_generated = 0;
static uint64_t csprng_counter[2] = { 0, 0 };
static uint8_t csprng_block[16];
static uint8_t csprng_key[32];
static entropy_t csprng_entropy;
static entropy_t csprng_last_entropy;

void entropy_hook(void* data, uint character) {
	if (character != csprng_entropy.characters[modi(csprng_entropy.character_capacity - 1, 32)]) {
		csprng_entropy.characters[csprng_entropy.character_capacity] = character;
		csprng_entropy.character_capacity = (csprng_entropy.character_capacity + 1) % 32;
	}
}

void random_init() {
	csprng_entropy.character_capacity = 0;
	random_seed(time(NULL));

	window_add_character_hook(entropy_hook, NULL);
}

void random_update_entropy() {
	csprng_entropy.timestamp = time(NULL);

	csprng_entropy.random_seed = x;
	memcpy(&csprng_entropy.cursor_position, &g_window.cursor_position, sizeof(uint64_t));
	memcpy(&csprng_entropy.window_size, &g_window.size, sizeof(uint64_t));

	keccak_hash_256((uint8_t*)&csprng_last_entropy, sizeof(entropy_t), csprng_entropy.last_state, 32);

	csprng_last_entropy = csprng_entropy;
}

void random_csprng_bytes(uint8_t* dest, uint64_t size) {
	for (uint64_t i = 0; i < size; i++) {
		if (csprng_generated % (1 << 8) == 0) {
			random_update_entropy();

			keccak_hash_256((uint8_t*)&csprng_entropy, sizeof(entropy_t), csprng_key, 32);
			keccak_hash_256(csprng_key, sizeof(csprng_key), csprng_key, 32);
		}

		if (csprng_generated % 16 == 0) {
			aes_encrypt_block((uint8_t*)csprng_counter, csprng_key, csprng_block);

			if (csprng_counter[0] == RANDOM_MAX_RAND)
				csprng_counter[1]++;
			else
				csprng_counter[0]++;
		}

		dest[i] = csprng_block[csprng_generated % 16];

		csprng_generated++;
	}
}

uint64_t random_csprng_randint() {
	uint64_t dest;
	random_csprng_bytes((uint8_t*)&dest, sizeof(dest));

	return dest;
}

#define SAMPLE_SIZE 100000

uint64_t bits_count(uint8_t* stream, uint64_t size) {
	uint64_t result = 0;

	for (uint64_t i = 0; i < size; i++) {
		for (uint8_t j = 0; j < 8; j++) {
			result += (stream[i] >> j) & 1;
		}
	}

	return result;
}

void randomness_test(uint64_t (*random_fn)(), uint32_t size) {
	uint64_t* random_array = malloc(sizeof(uint64_t) * size);

	for (uint32_t i = 0; i < size; i++) {
		random_array[i] = random_fn();
	}

	uint64_t first_count = bits_count((uint8_t*)random_array, (sizeof(uint64_t) * size) / 2);
	uint64_t second_count = bits_count((uint8_t*)random_array + size / 2, (sizeof(uint64_t) * size) / 2);

	printf("Ratio: %lu / %lu = %.4f\n", first_count, second_count, (float) first_count / second_count);
	free(random_array);
}
