#include "random.h"

static unsigned long x = 123456789, y = 362436069, z = 521288629;

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
