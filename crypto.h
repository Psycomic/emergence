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

extern uint8_t aes_initialization_vector[16];

#endif
