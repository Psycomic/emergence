#ifndef _CRYPTO_HEADER
#define _CRYPTO_HEADER
#include <inttypes.h>

void aes_encrypt_block(uint8_t* block, uint8_t* key, uint8_t* out);
void aes_decrypt_block(uint8_t* block, uint8_t* key, uint8_t* out);
void aes_encrypt_ecb(uint8_t* message, uint64_t message_size, uint8_t* key, uint8_t* out);
void aes_decrypt_ecb(uint8_t* message, uint64_t message_size, uint8_t* key, uint8_t* out);
void aes_encrypt_cbc(uint8_t* message, uint64_t message_size, uint8_t* key, uint8_t* out);
void aes_decrypt_cbc(uint8_t* message, uint64_t message_size, uint8_t* key, uint8_t* out);
void keccak_hash(uint8_t* message, uint64_t message_size, uint8_t* hash);
void keccak_hash_256(uint8_t* message, uint64_t message_size, uint8_t* hash, uint64_t hash_size);
void pad_message(uint8_t* message, uint64_t message_size, uint32_t block_size, uint8_t* out);

void hamming_7_4_encode(uint8_t bits, uint8_t* codeword);
int hamming_7_4_decode(uint8_t codeword);
void binary_print(uint8_t byte);

typedef struct {
	uint64_t height, width;
	uint8_t data[];
} BinaryMatrix;

BinaryMatrix* binary_matrix_allocate(uint64_t width, uint64_t height);
uint8_t binary_matrix_get(BinaryMatrix* matrix, uint64_t x, uint64_t y);
void binary_matrix_set(BinaryMatrix* matrix, uint64_t x, uint64_t y, uint8_t bit);
void binary_matrix_print(BinaryMatrix* matrix);
void binary_matrix_vector_multiply(uint8_t* vector, BinaryMatrix* matrix, uint8_t* out);
void binary_vector_print(uint8_t* vector, uint64_t vector_size);

extern uint8_t aes_initialization_vector[16];

#endif
