#include <inttypes.h>

#define RANDOM_MAX_RAND (uint64_t)0xffffffffffffffff

void random_seed(uint64_t seed);
uint64_t random_randint();

float gaussian_random();
float random_float();
void random_arrayf(float* destination, uint64_t size);
float random_uniform(float min, float max);
