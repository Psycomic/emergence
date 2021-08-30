#include <inttypes.h>

#define RANDOM_MAX_RAND ((uint64_t)0xffffffffffffffff)

void random_seed(uint64_t seed);
uint64_t random_randint();

float gaussian_random();
float random_normal(float mean, float deviation);
float random_float();
void random_arrayf(float* destination, uint64_t size);
float random_uniform(float min, float max);
void randomness_test(uint64_t (*random_fn)(), uint32_t size);

void random_csprng_bytes(uint8_t* dest, uint64_t size);
uint64_t random_csprng_randint();
void random_init();

uint64_t bits_count(uint8_t* stream, uint64_t size);
