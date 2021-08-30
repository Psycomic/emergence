#ifndef _CRYPTO_HEADER
#define _CRYPTO_HEADER
#include <inttypes.h>
#include <stddef.h>

void aes_encrypt_block(uint8_t* block, uint8_t* key, uint8_t* out);
void aes_decrypt_block(uint8_t* block, uint8_t* key, uint8_t* out);
void aes_encrypt_ecb(uint8_t* message, uint64_t message_size, uint8_t* key, uint8_t* out);
void aes_decrypt_ecb(uint8_t* message, uint64_t message_size, uint8_t* key, uint8_t* out);
void aes_encrypt_cbc(uint8_t* message, uint64_t message_size, uint8_t* key, uint8_t* out);
void aes_decrypt_cbc(uint8_t* message, uint64_t message_size, uint8_t* key, uint8_t* out);
void keccak_hash(uint8_t* message, uint64_t message_size, uint8_t* hash, uint64_t hash_size);
void keccak_hash_256(uint8_t* message, uint64_t message_size, uint8_t* hash);
void pad_message(uint8_t* message, uint64_t message_size, uint32_t block_size, uint8_t* out);

typedef struct {
	uint64_t height, width;
	uint8_t data[];
} BinaryMatrix;

typedef struct {
	BinaryMatrix* generator;
	BinaryMatrix* parity_check;
} LinearCode;

BinaryMatrix* binary_matrix_allocate(uint64_t width, uint64_t height);
uint8_t binary_matrix_get(BinaryMatrix* matrix, uint64_t x, uint64_t y);
void binary_matrix_set(BinaryMatrix* matrix, uint64_t x, uint64_t y, uint8_t bit);
void binary_matrix_print(BinaryMatrix* matrix);
void binary_matrix_vector_multiply(uint8_t* vector, BinaryMatrix* matrix, uint8_t* out);
void binary_vector_print(uint8_t* vector, uint64_t vector_size);
uint64_t binary_vector_hamming_weight(uint8_t* vector, uint64_t size);
uint64_t binary_vector_hamming_distance(uint8_t* vec1, uint8_t* vec2, uint64_t size);
void binary_matrix_copy_col(BinaryMatrix* matrix, uint64_t col, uint8_t* out);
LinearCode make_hamming_code(uint64_t check_bits_count);

extern uint8_t aes_initialization_vector[16];

typedef struct {
	uint64_t width, height;
	int32_t data[];
} MatrixInt32;

typedef struct {
	uint64_t width, height;
	uint32_t data[];
} MatrixUint32;

typedef struct {
	uint64_t width, height;
	int16_t data[];
} MatrixInt16;

typedef struct {
	uint64_t width, height;
	uint16_t data[];
} MatrixUint16;

typedef struct {
	uint64_t width, height;
	float data[];
} MatrixFloat32;

MatrixUint16* matrix_random_randint_uint16(uint16_t low, uint16_t high, size_t w, size_t h);
MatrixInt16* matrix_random_randint_int16(int16_t low, int16_t high, size_t w, size_t h);
MatrixUint32* matrix_random_randint_uint32(uint32_t low, uint32_t high, size_t w, size_t h);
MatrixInt32* matrix_random_randint_int32(int32_t low, int32_t high, size_t w, size_t h);
MatrixFloat32* matrix_random_normal_float32(float mean, float deviation, size_t w, size_t h);

void matrix_round_to_uint16(MatrixFloat32* from, MatrixUint16* to);
void matrix_round_to_int16(MatrixFloat32* from, MatrixInt16* to);
void matrix_round_to_uint32(MatrixFloat32* from, MatrixUint32* to);
void matrix_round_to_int32(MatrixFloat32* from, MatrixInt32* to);

void matrix_mod_uint16(MatrixUint16* mat, uint16_t x);
void matrix_mod_int16(MatrixInt16* mat, int16_t x);
void matrix_mod_uint32(MatrixUint32* mat, uint32_t x);
void matrix_mod_int32(MatrixInt32* mat, int32_t x);

MatrixUint16* matrix_dot_uint16(MatrixUint16* a, MatrixUint16* b);

void matrix_print_uint16(MatrixUint16* mat);

#endif
