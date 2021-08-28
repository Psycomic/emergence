#include "crypto.h"
#include "random.h"
#include "misc.h"

#include <memory.h>
#include <math.h>
#include <stdio.h>
#include <assert.h>

static uint32_t aes_nr = 14;
static uint32_t aes_nk = 8;
static uint32_t aes_nb = 4;

static uint32_t aes_rcon[10] = {
	0x01 << 24,
	0x02 << 24,
	0x04 << 24,
	0x08 << 24,
	0x10 << 24,
	0x20 << 24,
	0x40 << 24,
	0x80 << 24,
	0x1B << 24,
	0x36 << 24
};

static uint8_t aes_sub_table[0x10][0x10] = {
	{ 0x63, 0xca, 0xb7, 0x04, 0x09, 0x53, 0xd0, 0x51, 0xcd, 0x60, 0xe0, 0xe7, 0xba, 0x70, 0xe1, 0x8c },
	{ 0x7c, 0x82, 0xfd, 0xc7, 0x83, 0xd1, 0xef, 0xa3, 0x0c, 0x81, 0x32, 0xc8, 0x78, 0x3e, 0xf8, 0xa1 },
	{ 0x77, 0xc9, 0x93, 0x23, 0x2c, 0x00, 0xaa, 0x40, 0x13, 0x4f, 0x3a, 0x37, 0x25, 0xb5, 0x98, 0x89 },
	{ 0x7b, 0x7d, 0x26, 0xc3, 0x1a, 0xed, 0xfb, 0x8f, 0xec, 0xdc, 0x0a, 0x6d, 0x2e, 0x66, 0x11, 0x0d },
	{ 0xf2, 0xfa, 0x36, 0x18, 0x1b, 0x20, 0x43, 0x92, 0x5f, 0x22, 0x49, 0x8d, 0x1c, 0x48, 0x69, 0xbf },
	{ 0x6b, 0x59, 0x3f, 0x96, 0x6e, 0xfc, 0x4d, 0x9d, 0x97, 0x2a, 0x06, 0xd5, 0xa6, 0x03, 0xd9, 0xe6 },
	{ 0x6f, 0x47, 0xf7, 0x05, 0x5a, 0xb1, 0x33, 0x38, 0x44, 0x90, 0x24, 0x4e, 0xb4, 0xf6, 0x8e, 0x42 },
	{ 0xc5, 0xf0, 0xcc, 0x9a, 0xa0, 0x5b, 0x85, 0xf5, 0x17, 0x88, 0x5c, 0xa9, 0xc6, 0x0e, 0x94, 0x68 },
	{ 0x30, 0xad, 0x34, 0x07, 0x52, 0x6a, 0x45, 0xbc, 0xc4, 0x46, 0xc2, 0x6c, 0xe8, 0x61, 0x9b, 0x41 },
	{ 0x01, 0xd4, 0xa5, 0x12, 0x3b, 0xcb, 0xf9, 0xb6, 0xa7, 0xee, 0xd3, 0x56, 0xdd, 0x35, 0x1e, 0x99 },
	{ 0x67, 0xa2, 0xe5, 0x80, 0xd6, 0xbe, 0x02, 0xda, 0x7e, 0xb8, 0xac, 0xf4, 0x74, 0x57, 0x87, 0x2d },
	{ 0x2b, 0xaf, 0xf1, 0xe2, 0xb3, 0x39, 0x7f, 0x21, 0x3d, 0x14, 0x62, 0xea, 0x1f, 0xb9, 0xe9, 0x0f },
	{ 0xfe, 0x9c, 0x71, 0xeb, 0x29, 0x4a, 0x50, 0x10, 0x64, 0xde, 0x91, 0x65, 0x4b, 0x86, 0xce, 0xb0 },
	{ 0xd7, 0xa4, 0xd8, 0x27, 0xe3, 0x4c, 0x3c, 0xff, 0x5d, 0x5e, 0x95, 0x7a, 0xbd, 0xc1, 0x55, 0x54 },
	{ 0xab, 0x72, 0x31, 0xb2, 0x2f, 0x58, 0x9f, 0xf3, 0x19, 0x0b, 0xe4, 0xae, 0x8b, 0x1d, 0x28, 0xbb },
	{ 0x76, 0xc0, 0x15, 0x75, 0x84, 0xcf, 0xa8, 0xd2, 0x73, 0xdb, 0x79, 0x08, 0x8a, 0x9e, 0xdf, 0x16 }
};

uint8_t aes_inv_sub_table[0x10][0x10] = {
	{ 0x52, 0x7c, 0x54, 0x08, 0x72, 0x6c, 0x90, 0xd0, 0x3a, 0x96, 0x47, 0xfc, 0x1f, 0x60, 0xa0, 0x17 },
    { 0x09, 0xe3, 0x7b, 0x2e, 0xf8, 0x70, 0xd8, 0x2c, 0x91, 0xac, 0xf1, 0x56, 0xdd, 0x51, 0xe0, 0x2b },
    { 0x6a, 0x39, 0x94, 0xa1, 0xf6, 0x48, 0xab, 0x1e, 0x11, 0x74, 0x1a, 0x3e, 0xa8, 0x7f, 0x3b, 0x04 },
    { 0xd5, 0x82, 0x32, 0x66, 0x64, 0x50, 0x00, 0x8f, 0x41, 0x22, 0x71, 0x4b, 0x33, 0xa9, 0x4d, 0x7e },
    { 0x30, 0x9b, 0xa6, 0x28, 0x86, 0xfd, 0x8c, 0xca, 0x4f, 0xe7, 0x1d, 0xc6, 0x88, 0x19, 0xae, 0xba },
    { 0x36, 0x2f, 0xc2, 0xd9, 0x68, 0xed, 0xbc, 0x3f, 0x67, 0xad, 0x29, 0xd2, 0x07, 0xb5, 0x2a, 0x77 },
    { 0xa5, 0xff, 0x23, 0x24, 0x98, 0xb9, 0xd3, 0x0f, 0xdc, 0x35, 0xc5, 0x79, 0xc7, 0x4a, 0xf5, 0xd6 },
    { 0x38, 0x87, 0x3d, 0xb2, 0x16, 0xda, 0x0a, 0x02, 0xea, 0x85, 0x89, 0x20, 0x31, 0x0d, 0xb0, 0x26 },
    { 0xbf, 0x34, 0xee, 0x76, 0xd4, 0x5e, 0xf7, 0xc1, 0x97, 0xe2, 0x6f, 0x9a, 0xb1, 0x2d, 0xc8, 0xe1 },
    { 0x40, 0x8e, 0x4c, 0x5b, 0xa4, 0x15, 0xe4, 0xaf, 0xf2, 0xf9, 0xb7, 0xdb, 0x12, 0xe5, 0xeb, 0x69 },
	{ 0xa3, 0x43, 0x95, 0xa2, 0x5c, 0x46, 0x58, 0xbd, 0xcf, 0x37, 0x62, 0xc0, 0x10, 0x7a, 0xbb, 0x14 },
	{ 0x9e, 0x44, 0x0b, 0x49, 0xcc, 0x57, 0x05, 0x03, 0xce, 0xe8, 0x0e, 0xfe, 0x59, 0x9f, 0x3c, 0x63 },
	{ 0x81, 0xc4, 0x42, 0x6d, 0x5d, 0xa7, 0xb8, 0x01, 0xf0, 0x1c, 0xaa, 0x78, 0x27, 0x93, 0x83, 0x55 },
	{ 0xf3, 0xde, 0xfa, 0x8b, 0x65, 0x8d, 0xb3, 0x13, 0xb4, 0x75, 0x18, 0xcd, 0x80, 0xc9, 0x53, 0x21 },
	{ 0xd7, 0xe9, 0xc3, 0xd1, 0xb6, 0x9d, 0x45, 0x8a, 0xe6, 0xdf, 0xbe, 0x5a, 0xec, 0x9c, 0x99, 0x0c },
	{ 0xfb, 0xcb, 0x4e, 0x25, 0x92, 0x84, 0x06, 0x6b, 0x73, 0x6e, 0x1b, 0xf4, 0x5f, 0xef, 0x61, 0x7d }
};

uint8_t aes_initialization_vector[16] = {
	13, 244, 45, 98,
	146, 212, 23, 12,
	76, 12, 123, 209,
	23, 107, 123, 23
};

uint8_t aes_sub_byte(uint8_t byte) {
	uint8_t first = byte >> 4;
	uint8_t second = byte & 0x0f;

	return aes_sub_table[second][first];
}

uint8_t aes_inv_sub_byte(uint8_t byte) {
	uint8_t first = byte >> 4;
	uint8_t second = byte & 0x0f;

	return aes_inv_sub_table[second][first];
}

void aes_sub_bytes(uint8_t* state) {
	for (uint32_t i = 0; i < 16; i++)
		state[i] = aes_sub_byte(state[i]);
}

void aes_inv_sub_bytes(uint8_t* state) {
	for (uint32_t i = 0; i < 16; i++)
		state[i] = aes_inv_sub_byte(state[i]);
}

uint32_t aes_rot_word(uint32_t word) {
	uint32_t result = 0;
	uint8_t* result_ptr = (uint8_t*)&result;
	uint8_t* word_ptr = (uint8_t*)&word;

	result_ptr[0] = word_ptr[1];
	result_ptr[1] = word_ptr[2];
	result_ptr[2] = word_ptr[3];
	result_ptr[3] = word_ptr[0];

	return result;
}

uint32_t aes_sub_word(uint32_t word) {
	uint32_t result = 0;
	uint8_t* result_ptr = (uint8_t*)&result;
	uint8_t* word_ptr = (uint8_t*)&word;

	result_ptr[0] = aes_sub_byte(word_ptr[0]);
	result_ptr[1] = aes_sub_byte(word_ptr[1]);
	result_ptr[2] = aes_sub_byte(word_ptr[2]);
	result_ptr[3] = aes_sub_byte(word_ptr[3]);

	return result;
}

void aes_shift_rows(uint8_t* state) {
	uint8_t temp[16];
	memcpy(temp, state, 16);

	temp[4 * 1 + 0] = state[4 * 1 + 1];
	temp[4 * 1 + 1] = state[4 * 1 + 2];
	temp[4 * 1 + 2] = state[4 * 1 + 3];
	temp[4 * 1 + 3] = state[4 * 1 + 0];

	temp[4 * 2 + 0] = state[4 * 2 + 2];
	temp[4 * 2 + 1] = state[4 * 2 + 3];
	temp[4 * 2 + 2] = state[4 * 2 + 0];
	temp[4 * 2 + 3] = state[4 * 2 + 1];

	temp[4 * 3 + 0] = state[4 * 3 + 3];
	temp[4 * 3 + 1] = state[4 * 3 + 0];
	temp[4 * 3 + 2] = state[4 * 3 + 1];
	temp[4 * 3 + 3] = state[4 * 3 + 2];

	memcpy(state, temp, 16);
}

void aes_inv_shift_rows(uint8_t* state) {
	uint8_t temp[16];
	memcpy(temp, state, 16);

	temp[4 * 1 + 0] = state[4 * 1 + 3];
	temp[4 * 1 + 1] = state[4 * 1 + 0];
	temp[4 * 1 + 2] = state[4 * 1 + 1];
	temp[4 * 1 + 3] = state[4 * 1 + 2];

	temp[4 * 2 + 0] = state[4 * 2 + 2];
	temp[4 * 2 + 1] = state[4 * 2 + 3];
	temp[4 * 2 + 2] = state[4 * 2 + 0];
	temp[4 * 2 + 3] = state[4 * 2 + 1];

	temp[4 * 3 + 0] = state[4 * 3 + 1];
	temp[4 * 3 + 1] = state[4 * 3 + 2];
	temp[4 * 3 + 2] = state[4 * 3 + 3];
	temp[4 * 3 + 3] = state[4 * 3 + 0];

	memcpy(state, temp, 16);
}

uint8_t aes_gmul(uint8_t a, uint8_t b) {
    uint8_t p = 0;

    for (int counter = 0; counter < 8; counter++) {
        if ((b & 1) != 0) {
            p ^= a;
        }

        uint8_t hi_bit_set = (a & 0x80) != 0;
        a <<= 1;
        if (hi_bit_set) {
            a ^= 0x1B;
        }
        b >>= 1;
    }

    return p;
}

void aes_mix_colums(uint8_t* state) {
	uint8_t temp[16];
	memcpy(temp, state, 16);

	for (uint32_t i = 0; i < 4; i++) {
		temp[4 * 0 + i] = aes_gmul(2, state[4 * 0 + i]) ^ aes_gmul(3, state[4 * 1 + i]) ^ state[4 * 2 + i] ^ state[4 * 3 + i];
		temp[4 * 1 + i] = state[4 * 0 + i] ^ aes_gmul(2, state[4 * 1 + i]) ^ aes_gmul(3, state[4 * 2 + i]) ^ state[4 * 3 + i];
		temp[4 * 2 + i] = state[4 * 0 + i] ^ state[4 * 1 + i] ^ aes_gmul(2, state[4 * 2 + i]) ^ aes_gmul(3, state[4 * 3 + i]);
		temp[4 * 3 + i] = aes_gmul(3, state[4 * 0 + i]) ^ state[4 * 1 + i] ^ state[4 * 2 + i] ^ aes_gmul(2, state[4 * 3 + i]);
	}

	memcpy(state, temp, 16);
}

void aes_inv_mix_colums(uint8_t* state) {
	uint8_t temp[16];
	memcpy(temp, state, 16);

	for (uint32_t i = 0; i < 4; i++) {
		temp[4 * 0 + i] = aes_gmul(0x0e, state[4 * 0 + i]) ^ aes_gmul(0xb, state[4 * 1 + i]) ^
			aes_gmul(0xd, state[4 * 2 + i]) ^ aes_gmul(0x9, state[4 * 3 + i]);
		temp[4 * 1 + i] = aes_gmul(0x9, state[4 * 0 + i]) ^ aes_gmul(0xe, state[4 * 1 + i]) ^
			aes_gmul(0xb, state[4 * 2 + i]) ^ aes_gmul(0xd, state[4 * 3 + i]);
		temp[4 * 2 + i] = aes_gmul(0xd, state[4 * 0 + i]) ^ aes_gmul(0x9, state[4 * 1 + i]) ^
			aes_gmul(0xe, state[4 * 2 + i]) ^ aes_gmul(0xb, state[4 * 3 + i]);
		temp[4 * 3 + i] = aes_gmul(0xb, state[4 * 0 + i]) ^ aes_gmul(0xd, state[4 * 1 + i]) ^
			aes_gmul(0x9, state[4 * 2 + i]) ^ aes_gmul(0xe, state[4 * 3 + i]);
	}

	memcpy(state, temp, 16);
}

void aes_key_expansion(uint8_t* key, uint32_t* w) {
	uint32_t temp, i = 0;

	while (i < aes_nk) {
		w[i] = *(uint32_t*)(key + 4 * i);
		i++;
	}

	i = aes_nk;

	while (i < aes_nb * (aes_nr + 1)) {
		temp = w[i - 1];
		if (i % aes_nk == 0)
			temp = aes_sub_word(aes_rot_word(temp) ^ aes_rcon[i / aes_nk]);
		else if (i % aes_nk == 4)
			temp = aes_sub_word(temp);

		w[i] = w[i - aes_nk] ^ temp;
		i++;
	}
}

void aes_add_round_key(uint8_t* state, uint8_t* w, uint32_t round) {
	for (uint32_t i = 0; i < 16; i++) {
		state[i] ^= w[i + round * 4];
	}
}

void aes_encrypt_block(uint8_t* block, uint8_t* key, uint8_t* out) {
	uint8_t state[16];
	memcpy(state, block, 16);

	uint32_t w[4 * 15];
	aes_key_expansion(key, w);

	uint32_t round = 0;
	aes_add_round_key(state, (uint8_t*)w, round);

	for (round = 1; round < aes_nr; round++) {
		aes_sub_bytes(state);
		aes_shift_rows(state);
		aes_mix_colums(state);
		aes_add_round_key(state, (uint8_t*)w, round);
	}

	aes_sub_bytes(state);
	aes_shift_rows(state);

	aes_add_round_key(state, (uint8_t*)w, round);

	memcpy(out, state, 16);
}

void aes_decrypt_block(uint8_t* block, uint8_t* key, uint8_t* out) {
	uint8_t state[16];
	memcpy(state, block, 16);

	uint32_t w[4 * 15];
	aes_key_expansion(key, w);

	uint32_t round = aes_nr;
	aes_add_round_key(state, (uint8_t*)w, round);

	for (round = aes_nr - 1; round >= 1; round--) {
		aes_inv_shift_rows(state);
		aes_inv_sub_bytes(state);
		aes_add_round_key(state, (uint8_t*)w, round);
		aes_inv_mix_colums(state);
	}

	aes_inv_shift_rows(state);
	aes_inv_sub_bytes(state);

	aes_add_round_key(state, (uint8_t*)w, round);

	memcpy(out, state, 16);
}

void aes_encrypt_ecb(uint8_t* message, uint64_t message_size, uint8_t* key, uint8_t* out) {
	uint32_t pad_size = message_size % 16;
	uint32_t blocks_count = message_size / 16 + (pad_size == 0 ? 0 : 1);

	uint8_t* padded_message = malloc(blocks_count * 16);
	memcpy(padded_message, message, message_size);
	memset(padded_message + message_size, 0, pad_size);

	for (uint32_t i = 0; i < blocks_count; i++) {
		aes_encrypt_block(padded_message + i * 16, key, out + i * 16);
	}

	free(padded_message);
}

void aes_decrypt_ecb(uint8_t* message, uint64_t message_size, uint8_t* key, uint8_t* out) {
	uint32_t blocks_count = message_size / 16 + (message_size % 16 == 0 ? 0 : 1);

	for (uint32_t i = 0; i < blocks_count; i++) {
		aes_decrypt_block(message + i * 16, key, out + i * 16);
	}
}

void aes_encrypt_cbc(uint8_t* message, uint64_t message_size, uint8_t* key, uint8_t* out) {
	uint8_t last_block[16];
	memcpy(last_block, aes_initialization_vector, 16);
	uint blocks_count = message_size / 16;

	for (uint i = 0; i < blocks_count; i++) {
		uint8_t block[16];
		for (uint j = 0; j < 16; j++) {
			block[j] = message[i * 16 + j] ^ last_block[j];
		}

		aes_encrypt_block(block, key, last_block);
		memcpy(out + i * 16, last_block, 16);
	}
}

void aes_decrypt_cbc(uint8_t* message, uint64_t message_size, uint8_t* key, uint8_t* out) {
	uint8_t last_block[16];
	memcpy(last_block, aes_initialization_vector, 16);

	uint blocks_count = message_size / 16;

	for (uint i = 0; i < blocks_count; i++) {
		uint8_t block[16];
		aes_decrypt_block(message + i * 16, key, block);

		for (uint j = 0; j < 16; j++) {
			out[i * 16 + j] = block[j] ^ last_block[j];
		}

		memcpy(last_block, message + i * 16, 16);
	}
}

void pad_message(uint8_t* message, uint64_t message_size, uint32_t block_size, uint8_t* out) {
	uint padding_size = message_size % block_size;

	if (padding_size == 0) {
		memcpy(out, message, message_size);
		return;
	}

	for (uint i = 0; i < message_size + (block_size - padding_size); i++) {
		if (i < message_size)
			out[i] = message[i];
		else
			out[i] = 0;
	}
}

/*
A				A state array.
A[x, y, z]		For a state array A, the bit that corresponds to the triple (x, y, z) -> S[25 * x + 5 * y + z]
b				The width of a KECCAK-p permutation in bits.
c				The capacity of a sponge function.
d				The length of the digest of a hash function or the requested length of the output of an XOF, in bits.
f				The generic underlying function for the sponge construction.
ir				The round index for a KECCAK-p permutation.
J				The input string to RawSHAKE128 or RawSHAKE256.
l				For a K ECCAK -p permutation, the binary logarithm of the lane size, i.e., log 2 (w).
Lane (i, j)		For a state array A, a string of all the bits of the lane whose x and y coordinates are i and j.
M				The input string to a SHA-3 hash or XOF function.
N				The input string to SPONGE [f, pad, r] or K ECCAK [c].
nr				The number of rounds for a K ECCAK -p permutation.
pad				The generic padding rule for the sponge construction.
Plane (j)		For a state array A, a string of all the bits of the plane whose y coordinate is j.
r				The rate of a sponge function.
RC				For a round of a KECCAK-p permutation, the round constant.
w				The lane size of a KECCAK-p permutation in bits, i.e., b/25.
θ, ρ, π, χ, ι	The five step mappings that comprise a round.
KECCAK[c]		The KECCAK instance with KECCAK-f[1600] as the underlying permutation and capacity c.
KECCAK-f[b]		The family of seven permutations originally specified in [8] as the underlying function for KECCAK. The set of values for the width b of the permutations is {25, 50, 100, 200, 400, 800, 1600}.
KECCAK-p[b,nr]	The generalization of the KECCAK-f[b] permutations that is defined in this Standard by converting the number of rounds nr to an input parameter.
pad10*1			The multi-rate padding rule for KECCAK, originally specified in [8].
rc				The function that generates the variable bits of the round constants.
Rnd				The round function of a KECCAK-p permutation.
SHA3-224		The SHA-3 hash function that produces 224-bit digests.
SHA3-256		The SHA-3 hash function that produces 256-bit digests.
SHA3-384		The SHA-3 hash function that produces 384-bit digests.
SHA3-512		The SHA-3 hash function that produces 512-bit digests.
SHAKE128		The SHA-3 XOF that generally supports 128 bits of security strength, if the output is sufficiently long; see Sec. A.1.
SHAKE256		The SHA-3 XOF that generally supports 256 bits of security strength, if the output is sufficiently long; see Sec. A.1.
SPONGE[f,pad,r] The sponge function in which the underlying function is f, the padding rule is pad, and the rate is r.
*/

#define KECCAK_ROTL64(x, c) (((x) << (c)) | (x) >> (64 - (c)))
#define KECCAK_ROUNDS_COUNT 24

static const uint16_t keccak_offsets[5][5] = {
	{ 25, 39, 3, 10, 43 },
	{ 55, 20, 36, 44, 6 },
	{ 28, 27, 0, 1, 62 },
	{ 56, 14, 18, 2, 61 },
	{ 21, 8, 41, 45, 15 }
};

static const uint64_t keccakf_rndc[24] = {
    0x0000000000000001UL, 0x0000000000008082UL,
    0x800000000000808aUL, 0x8000000080008000UL,
    0x000000000000808bUL, 0x0000000080000001UL,
    0x8000000080008081UL, 0x8000000000008009UL,
    0x000000000000008aUL, 0x0000000000000088UL,
    0x0000000080008009UL, 0x000000008000000aUL,
    0x000000008000808bUL, 0x800000000000008bUL,
    0x8000000000008089UL, 0x8000000000008003UL,
    0x8000000000008002UL, 0x8000000000000080UL,
    0x000000000000800aUL, 0x800000008000000aUL,
    0x8000000080008081UL, 0x8000000000008080UL,
    0x0000000080000001UL, 0x8000000080008008UL
};

void keccak_f(uint64_t S[25]) {
	uint32_t nr;

	for (nr = 0; nr < KECCAK_ROUNDS_COUNT; nr++) {
		/* θ operation */
		uint64_t C[5];
		uint64_t D[5];

		for (uint8_t x = 0; x < 5; x++)
			C[x] = S[x] ^ S[x + 5] ^ S[x + 10] ^ S[x + 15] ^ S[x + 20];

		for (uint8_t x = 0; x < 5; x++) {
			D[x] = C[modi(x - 1, 5)] ^ KECCAK_ROTL64(C[(x + 1) % 5], 1);

			S[x] ^= D[x];
			S[x + 5] ^= D[x];
			S[x + 10] ^= D[x];
			S[x + 15] ^= D[x];
			S[x + 20] ^= D[x];
		}

		/* ρ operation */
		for (int32_t x = 0; x < 5; x++) {
			for (int32_t y = 0; y < 5; y++) {
				S[x + y * 5] = KECCAK_ROTL64(
					S[x + y * 5],
					keccak_offsets[modi(y - 2, 5)][modi(x - 2, 5)]);
			}
		}

		uint64_t S_prime[25];

		/* π operation */
		for (int32_t x = 0; x < 5; x++) {
			for (int32_t y = 0; y < 5; y++) {
				S_prime[x + y * 5] = S[modi(x + 3 * y, 5) + x * 5];
			}
		}

		memcpy(S, S_prime, sizeof(S_prime));

		/* χ operation */
		for (int32_t x = 0; x < 5; x++) {
			for (int32_t y = 0; y < 5; y++) {
				S_prime[x + y * 5] = S[x + y * 5] ^ (~S[(x + 1) % 5 + y * 5] & S[(x + 2) % 5 + y * 5]);
			}
		}

		memcpy(S, S_prime, sizeof(S_prime));

		/* ι operation */
		S[0] ^= keccakf_rndc[nr];
	}
}

void keccak_hash(uint8_t* message, uint64_t message_size, uint8_t* hash, uint64_t hash_size) {
	uint32_t pad = message_size % 200;

	uint32_t n = message_size / 200 + (pad != 0);
	const uint32_t r = 64;

	uint64_t S[25];				/* 8 * 25 = 200 */
	m_bzero(S, sizeof(S));

	uint64_t hash_count = 0;

compute_hash:
	for (uint32_t i = 0; i < n; i++)  {
		if (pad != 0 && i == n - 1) {
			uint8_t* small_S = (uint8_t*)S;

			for (uint32_t k = 0; k < pad; k++) {
				small_S[k] ^= message[k];
			}
		}
		else {
			for (uint32_t j = 0; j < r / sizeof(uint64_t); j++)
				S[j] ^= ((uint64_t*)message)[j];
		}

		keccak_f(S);
	}

	memcpy(hash + hash_count, S, min(hash_size, r));
	hash_count += r;

	if (hash_count < hash_size)
		goto compute_hash;
}

void keccak_hash_256(uint8_t* message, uint64_t message_size, uint8_t* hash) {
	keccak_hash(message, message_size, hash, 32);
}


#define BIT(a, n) ((a & (1 << (n))) != 0)

BinaryMatrix* binary_matrix_allocate(uint64_t width, uint64_t height) {
	uint bytes_width = width / 8 + 1;

	BinaryMatrix* matrix = malloc(sizeof(BinaryMatrix) + bytes_width * height);
	matrix->width = width;
	matrix->height = height;

	m_bzero(matrix->data, bytes_width * height);

	return matrix;
}

uint8_t binary_matrix_get(BinaryMatrix* matrix, uint64_t x, uint64_t y) {
	assert(x < matrix->width);
	assert(y < matrix->height);

	uint bytes_x = x / 8;
	uint bytes_width = matrix->width / 8 + 1;

	uint8_t a = matrix->data[bytes_x + y * bytes_width];

	return BIT(a, x % 8);
}

void binary_matrix_set(BinaryMatrix* matrix, uint64_t x, uint64_t y, uint8_t bit) {
	assert(x < matrix->width);
	assert(y < matrix->height);

	uint bytes_x = x / 8;
	uint bytes_width = matrix->width / 8 + 1;

	uint64_t index = bytes_x + y * bytes_width;
	uint8_t n = x % 8;

	matrix->data[index] = (matrix->data[index] & ~(1UL << n)) | (bit << n);
}

void binary_matrix_vector_multiply(uint8_t* vector, BinaryMatrix* matrix, uint8_t* out) {
	uint byte_width = matrix->width / 8 + 1;

	m_bzero(out, byte_width);

	for (uint y = 0; y < matrix->height; y++) {
		uint8_t op_vec = 0xff * BIT(vector[y / 8], y % 8);

		for (uint x = 0; x < byte_width; x++) {
			out[x] ^= op_vec & matrix->data[x + y * byte_width];
		}
	}
}

void binary_matrix_print(BinaryMatrix* matrix) {
	for (uint y = 0; y < matrix->height; y++) {
		for (uint x = 0; x < matrix->width; x++) {
			uint8_t bit = binary_matrix_get(matrix, x, y);

			if (bit)
				putchar('1');
			else
				putchar('0');

			putchar(' ');
		}

		putchar('\n');
	}
}

void binary_vector_print(uint8_t* vector, uint64_t vector_size) {
	for (uint i = 0; i < vector_size; i++) {
		printf("%d ", BIT(vector[i / 8], i % 8));
	}

	printf("\n");
}

uint popcnt8(uint8_t x) {
    x = (x & 0x55) + (x >> 1 & 0x55);
    x = (x & 0x33) + (x >> 2 & 0x33);
    x = (x & 0x0f) + (x >> 4 & 0x0f);

    return x;
}

uint64_t binary_vector_hamming_weight(uint8_t* vector, uint64_t size) {
	uint bytes_size = size / 8 + 1;
	uint64_t count = 0;

	for (uint i = 0; i < bytes_size; i++) {
		uint8_t a = vector[i];
		count += popcnt8(a);
	}

	return count;
}

uint64_t binary_vector_hamming_distance(uint8_t* vec1, uint8_t* vec2, uint64_t size) {
	uint bytes_size = size / 8 + 1;

	uint64_t count = 0;
	for (uint i = 0; i < bytes_size; i++) {
		uint8_t xored = vec1[i] ^ vec2[i];
		count += binary_vector_hamming_weight(&xored, 8);
	}

	return count;
}

void binary_matrix_copy_col(BinaryMatrix* matrix, uint64_t col, uint8_t* out) {
	m_bzero(out, matrix->height / 8 + 1);

	for (uint y = 0; y < matrix->height; y++) {
		out[y / 8] |= binary_matrix_get(matrix, col, y) << (y % 8);
	}
}

BinaryMatrix* binary_matrix_transpose(BinaryMatrix* matrix) {
	BinaryMatrix* new_matrix = binary_matrix_allocate(matrix->height, matrix->width);

	for (uint y = 0; y < matrix->height; y++) {
		for (uint x = 0; x < matrix->width; x++) {
			binary_matrix_set(new_matrix, y, x, binary_matrix_get(matrix, x, y));
		}
	}

	return new_matrix;
}

LinearCode make_hamming_code(uint64_t check_bits_count) {
	uint64_t width = (1 << check_bits_count) - 1;
	BinaryMatrix* generator = binary_matrix_allocate(width, width - check_bits_count);
	BinaryMatrix* parity_check = binary_matrix_allocate(width, check_bits_count);

	uint i = 0;
	for (uint x = 0; x < generator->width; x++) {
		if ((popcnt8(x + 1) == 1) != 1) {
			binary_matrix_set(generator, x, i++, 1);
		}
	}

	for (uint y = 0; y < parity_check->height; y++) {
		uint8_t bit = 0;

		for (uint x = 0; x < parity_check->width; x++) {
			if (x % (1 << y) == 0)
				bit = ~bit;

			binary_matrix_set(parity_check, x, y, bit);
		}
	}

	uint generator_index = 0;

	uint8_t* col = malloc(parity_check->height / 8 + 1);
	for (int x = parity_check->width - 1; x >= 0; x--) {
		binary_matrix_copy_col(parity_check, x, col);

		uint64_t count = binary_vector_hamming_weight(col, parity_check->height);

		if (count != 1) {
			for (uint i = 0; i < parity_check->height; i++) {
				binary_matrix_set(generator, (1 << i) - 1, generator_index,
								  BIT(col[i / 8], i % 8));
			}

			generator_index++;
		}
	}

	free(col);

	for (uint y = 0; y < parity_check->height; y++) {
		for (uint x = 0; x < parity_check->width / 2; x++) {
			uint8_t temp = binary_matrix_get(parity_check, x, y);
			binary_matrix_set(parity_check, x, y,
							  binary_matrix_get(parity_check, parity_check->width - 1 - x, y));

			binary_matrix_set(parity_check, parity_check->width - 1 - x, y, temp);
		}
	}

	LinearCode code = {
		.generator = generator,
		.parity_check = binary_matrix_transpose(parity_check)
	};
	free(parity_check);

	return code;
}
