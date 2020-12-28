#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "images.h"
#include "misc.h"

int image_load_bmp(Image* image, const char* path) {
	FILE* bmp_file;

	if ((bmp_file = fopen(path, "rb")) < 0)
		return -1;

	uchar header[0x7a];

	fread(header, 1, 0x7a, bmp_file);

	if (header[0] != 'B' || header[1] != 'M') {
		fclose(bmp_file);
		return -1;
	}

	uint dataPos = *(uint*)&(header[0xa]);
	uint image_size = *(uint*)&(header[0x22]);
	uint width = *(uint*)&(header[0x12]);
	uint height = *(uint*)&(header[0x16]);

	image_size = width * height * 3;

	if (dataPos == 0)
		dataPos = 54;

	image->height = height;
	image->width = width;
	image->data = malloc(sizeof(uchar) * image_size);

	fread(image->data, 1, image_size, bmp_file);

	image->color_encoding = GL_BGR;

	fclose(bmp_file);

	return 0;
}

uint big_endian_uint(uint a) {
	char result[4];

	int j = 0, i;
	for (i = 3; i >= 0; i--)
		result[j++] = *(((uchar*)&a) + i);
	
	return *((uint*)result);
}

uint hash_sequence(const char* sequence, uint size) {
	uint hash = 54878;

	for (uint i = 0; i < size; i++)
		hash = ((hash + 8784) * (sequence[i] * 77845 % 548878) * 54874) % 88745465;

	return hash;
}

typedef struct {
	uint length;
	char chunk_type[4];
	uchar* data;
} PNGChunkHeader;
 
void image_read_png_chunk(PNGChunkHeader* chunk, FILE* file) {
	fread(chunk, 1, 8, file);

	chunk->length = big_endian_uint(chunk->length);

	if (chunk->length > 0) {
		chunk->data = malloc(chunk->length);
		fread(chunk->data, 1, chunk->length, file);
	}

	uchar crc[4];
	fread(crc, 1, 4, file);
}

typedef struct {
	uint width;
	uint height;
	uchar bit_depth;
	uchar color_type;
	uchar compression_method;
	uchar filter_method;
	uchar interlace_method;
} IDHRHeader;

typedef struct {
	uchar compression_method;
	uchar check_bits;
} PNGCompressedData;

int image_load_png(Image* image, const char* path) {
	FILE* png_file;

	if ((png_file = fopen(path, "rb")) < 0)
		return -1;

	uchar file_header[9];
	fread(file_header, 1, 8, png_file);

	file_header[8] = '\0';

	if (file_header[1] != 'P' || file_header[2] != 'N' || file_header[3] != 'G') {
		fclose(png_file);
		return -1;
	}

	PNGChunkHeader idhr_chunk;
	image_read_png_chunk(&idhr_chunk, png_file);

	IDHRHeader* header = idhr_chunk.data;

	if (header->color_type != 2 || header->bit_depth != 8 || header->compression_method != 0 || header->filter_method != 0 || header->interlace_method != 0) {
		printf("Format not supported !\n");
		return -1;
	}

	uint hashed_idat_string = hash_sequence("IDAT", 4);
	uint hashed_iend_string = hash_sequence("IEND", 4);

	PNGChunkHeader chunk;

	uchar* compressed_data = malloc(1024);
	uint compressed_data_size = 0,
		compressed_data_capacity = 1024;

	uint idat_chunk_count = 0;

	while (1) {
		image_read_png_chunk(&chunk, png_file);

		if (hash_sequence(chunk.chunk_type, 4) == hashed_idat_string) {
			if (compressed_data_size + chunk.length > compressed_data_capacity) {
				compressed_data_capacity += chunk.length;
				compressed_data = realloc(compressed_data, compressed_data_capacity);
			}

			memcpy(compressed_data + compressed_data_size, chunk.data, chunk.length);
			compressed_data_size += chunk.length;

			free(chunk.data);
		}
		else if (hash_sequence(chunk.chunk_type, 4) == hashed_iend_string)
			break;
		else
			free(chunk.data);
	}

	free(idhr_chunk.data);
	fclose(png_file);

	PNGCompressedData* data = compressed_data;

	printf("==COMPRESSED DATA==\n"
		"Flags\t0x%x\n"
		"Check\t0x%x\n",
		data->compression_method, data->check_bits);

	free(compressed_data);

	return 1;
}

void image_destroy(Image* image) {
	free(image->data);
}

