#ifndef _CRYPTO_HEADER
#define _CRYPTO_HEADER
#include <inttypes.h>

void aes_encrypt_block(uint8_t* block, uint8_t* key, uint8_t* out);
void aes_decrypt_block(uint8_t* block, uint8_t* key, uint8_t* out);
void aes_encrypt(uint8_t* message, uint64_t message_size, uint8_t* key, uint8_t* out);
void aes_decrypt(uint8_t* message, uint64_t message_size, uint8_t* key, uint8_t* out);
void keccak_hash(uint8_t* message, uint64_t message_size, uint8_t* hash);
void keccak_hash_256(uint8_t* message, uint64_t message_size, uint8_t* hash, uint64_t hash_size);

#endif
