#include <inttypes.h>

#define RANDOM_MAX_RAND (uint64_t)0xffffffffffffffff

void random_seed(uint64_t seed);
uint64_t random_randint();

float gaussian_random();
float random_float();
void random_arrayf(float* destination, uint64_t size);
float random_uniform(float min, float max);
void randomness_test(uint64_t (*random_fn)(), uint32_t size);
void random_csprng_bytes(uint8_t* dest, uint64_t size);

uint64_t bits_count(uint8_t* stream, uint64_t size);

typedef struct {
	uint64_t random_seed;
	uint64_t cursor_position;
	uint32_t last_character;
	uint32_t timestamp;
	uint64_t last_state;
	uint64_t window_size;
} entropy_t;

entropy_t random_get_entropy();
