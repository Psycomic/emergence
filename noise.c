#define _USE_MATH_DEFINES
#include <math.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <assert.h>

#include "noise.h"
#include "misc.h"
#include "random.h"

#define index(x, y, z, size) x + y * size + z * size * size

typedef unsigned int uint;

float lerp(float a0, float a1, float w) {
	float mu2;

	mu2 = (1.f - cosf(w * M_PI)) / 2;
	return(a0 * (1 - mu2) + a1 * mu2);
}

inline float llerp(float a, float b, float w) {
	return (1.f - w) * a + w * b;
}

float sign(float x) {
	if (x > 0.f)
		return 1.f;
	else
		return -1.f;
}

void perlin_initialize_gradient(float* gradient, uint size) {
	for (size_t i = 0; i < size; i += 2) {
		gradient[i] = random_uniform(-1.f, 1.f);
		gradient[i + 1] = random_uniform(-1.f, 1.f);
	}
}

float dot_grid_gradient(float* gradient, uint size, int ix, int iy, float x, float y) {
	float dx = x - (float)ix;
	float dy = y - (float)iy;

	if (ix < size && iy < size && ix >= 0.f && iy >= 0.f)
		return (dx * gradient[index(ix, iy, 0, size)] + dy * gradient[index(ix, iy, 1, size)]);
	else
		return 0.f;
}

float perlin_noise(float* gradient, uint size, float x, float y) {
	int x0 = (int)(x * size);
	int x1 = x0 + 1;
	int y0 = (int)(y * size);
	int y1 = y0 + 1;

	float sx = x * size - (float)x0;
	float sy = y * size - (float)y0;

	float n0, n1, ix0, ix1, value;

	n0 = dot_grid_gradient(gradient, size, x0, y0, x * size, y * size);
	n1 = dot_grid_gradient(gradient, size, x1, y0, x * size, y * size);
	ix0 = llerp(n0, n1, sx);

	n0 = dot_grid_gradient(gradient, size, x0, y1, x * size, y * size);
	n1 = dot_grid_gradient(gradient, size, x1, y1, x * size, y * size);
	ix1 = llerp(n0, n1, sx);

	value = llerp(ix0, ix1, sy);
	return value;
}

void octaves_init(Octaves* octaves, uint layers_count, uint size, float frequency, float amplitude) {
	octaves->amplitude = amplitude;
	octaves->frequency = frequency;
	octaves->layers = malloc(sizeof(float*) * layers_count);
	octaves->layers_count = layers_count;
	octaves->size = size;

	for (uint i = 0; i < layers_count; i++) {
		uint octave_size = size * ((uint)powf(frequency, (float)i));
		octaves->layers[i] = malloc(sizeof(float) * octave_size * octave_size * 2);
		perlin_initialize_gradient(octaves->layers[i], octave_size * octave_size * 2);
	}
}

float octavien_noise(Octaves* octaves, float x, float y) {
	float final_height = 0.f;

	for (uint i = 0; i < octaves->layers_count; i++) {
		float scale = powf(octaves->frequency, (float)i);
		float importance = powf(octaves->amplitude, (float)i);

		uint octave_size = (float)octaves->size * scale;

		final_height += perlin_noise(octaves->layers[i], octave_size, x, y) * importance;
	}

	return final_height;
}

float ridged_noise(Octaves* octaves, float x, float y) {
	float final_height = 0.f;

	for (uint i = 0; i < octaves->layers_count; i++) {
		float scale = powf(octaves->frequency, (float)i);
		float importance = powf(octaves->amplitude, (float)i);

		uint octave_size = (float)octaves->size * scale;

		final_height += -fabs(perlin_noise(octaves->layers[i], octave_size, x, y) * importance);
	}

	return final_height;
}

float distortion_noise(Octaves* octaves, float x, float y, float distortion, float scale) {
	float qx = octavien_noise(octaves, x, y) * scale,
		qy = octavien_noise(octaves, x + distortion, y + distortion) * scale;

	return ridged_noise(octaves, x + qx, y + qy);
}


float voronoi_noise(uint dimensions, float* points, uint points_number, float* position, float (*noise_accessor)(float* distance, float* position)) {
	float distances[5];

	for (uint k = 0; k < 5; k++) {
		float min_distance = 10.f;
		uint min_index = 0;

		for (uint i = k; i < points_number; i++) {
			float distance = 0.f;

			for (uint j = 0; j < dimensions; j++)
				distance += (points[i * dimensions + j] - position[j]) * (points[i * dimensions + j] - position[j]);

			distance = sqrtf(distance);

			if (distance < min_distance) {
				min_distance = distance;
				min_index = i;
			}
		}

		float temp[5]; // Here
		memcpy(temp, points + k * dimensions, sizeof(float) * dimensions);
		memcpy(points + k * dimensions, points + min_index * dimensions, sizeof(float) * dimensions);
		memcpy(points + min_index * dimensions, temp, sizeof(float) * dimensions);

		distances[k] = min_distance;
	}

	return (noise_accessor)(distances, position);
}

float worley_noise(float* distance, float* position) {
	return distance[1] - distance[0];
}

float cave_noise(float* distance, float* position) {
	return distance[0] + distance[1];
}

float cellular_noise(float* distance, float* position) {
	return distance[0];
}

void hopalong_fractal(Vector3* destination, uint iterations_number, float a, float b, float c, float scale) {
	float x = 0.f,
		y = 0.f;

	for (uint i = 0; i < iterations_number; i++) {
		float temp_x = y - sign(x) * sqrtf(fabsf(b * x - c));
		y = a - x;

		x = temp_x;

		destination[i].x = x * scale;
		destination[i].y = y * scale;
		destination[i].z = 0.f;
	}
}

void terrain_create(Vector3* terrain_vertices, uint size, float height, float width, float (*noise_function)(float x, float y)) {
	for (uint x = 0; x < size; ++x) {
		for (uint y = 0; y < size; ++y) {
			float real_x = (float)x / size,
				real_y = (float)y / size;

			float height_at_point = noise_function(real_x, real_y);

			terrain_vertices[x + y * size].x = real_x * width;
			terrain_vertices[x + y * size].z = real_y * width;
			terrain_vertices[x + y * size].y = height_at_point * height;
		}
	}
}

void noise_image_create(Image* destination, uint32_t image_size, float (*noise_function)(float x, float y)) {
	image_blank_init(destination, image_size, image_size, GL_RGB);

	for (uint x = 0; x < image_size; ++x) {
		for (uint y = 0; y < image_size; ++y) {
			float real_x = (float)x / image_size,
				real_y = (float)y / image_size;

			float height_at_point = noise_function(real_x, real_y);

			destination->data[x * image_size * 3 + y * 3 + 0] = height_at_point * 255;
			destination->data[x * image_size * 3 + y * 3 + 1] = height_at_point * 255;
			destination->data[x * image_size * 3 + y * 3 + 2] = height_at_point * 255;
		}
	}
}

#undef index
#define index(x, y, s) (x) * (s) + (y)

void terrain_elements(uint* elements, uint terrain_size) {
	for (uint x = 0; x < terrain_size - 1; ++x) {
		for (uint y = 0; y < terrain_size - 1; ++y) {
			uint p = x * (terrain_size - 1) * 6 + y * 6;

			elements[p + 0] = index(x, y, terrain_size);
			elements[p + 1] = index(x + 1, y, terrain_size);
			elements[p + 2] = index(x, y + 1, terrain_size);
			elements[p + 4] = index(x + 1, y, terrain_size);
			elements[p + 3] = index(x, y + 1, terrain_size);
			elements[p + 5] = index(x + 1, y + 1, terrain_size);
		}
	}
}
