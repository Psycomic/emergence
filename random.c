#include <stdio.h>
#include <memory.h>
#include <time.h>

#include "random.h"
#include "misc.h"
#include "crypto.h"
#include "window.h"

static unsigned long x = 123456789, y = 362436069, z = 521288629;
extern uint32_t last_character;
static entropy_t last_entropy;

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

entropy_t random_get_entropy() {
	entropy_t result;

	result.timestamp = time(NULL);

	result.random_seed = x;
	memcpy(&result.cursor_position, &g_window.cursor_position, sizeof(uint64_t));
	memcpy(&result.window_size, &g_window.size, sizeof(uint64_t));

	result.last_state = last_entropy.cursor_position ^ last_entropy.random_seed ^ last_entropy.timestamp ^
		last_entropy.window_size ^ last_entropy.last_state ^ last_entropy.last_character;

	last_entropy = result;

	return result;
}

void random_csprng_bytes(uint8_t* dest, uint64_t size) {
	/* TODO */
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
