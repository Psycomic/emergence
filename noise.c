#define _USE_MATH_DEFINES
#include <math.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <assert.h>

#include "linear_algebra.h"

#define index(x, y, z, size) x + y * size + z * size * size

typedef unsigned int uint;

float lerp(float a0, float a1, float w) {
	float mu2;

	mu2 = (1.f - cosf(w * M_PI)) / 2;
	return(a0 * (1 - mu2) + a1 * mu2);
}

float llerp(float a, float b, float w) {
	return a + (b - a) * w;
}

float sign(float x) {
	if (x > 0.f)
		return 1.f;
	else
		return -1.f;
}

float random_float() {
	return ((float)rand()) / RAND_MAX;
}

float random_uniform(float min, float max) {
	return min + (max - min) * random_float();
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

	return (dx * gradient[index(ix, iy, 0, size)] +
		dy * gradient[index(ix, iy, 1, size)]);
}

float perlin_noise(float* gradient, uint size, float x, float y) {
	int x0 = (int)x;
	int x1 = x0 + 1;
	int y0 = (int)y;
	int y1 = y0 + 1;

	float sx = x - (float)x0;
	float sy = y - (float)y0;

	float n0, n1, ix0, ix1, value;

	n0 = dot_grid_gradient(gradient, size, x0, y0, x, y);
	n1 = dot_grid_gradient(gradient, size, x1, y0, x, y);
	ix0 = lerp(n0, n1, sx);

	n0 = dot_grid_gradient(gradient, size, x0, y1, x, y);
	n1 = dot_grid_gradient(gradient, size, x1, y1, x, y);
	ix1 = lerp(n0, n1, sx);

	value = lerp(ix0, ix1, sy);
	return value;
}

void octavien_initialize_gradient(float* octaves[], uint layers, uint size, float frequency) {
	for (uint i = 0; i < layers; ++i) {
		uint octave_size = size * ((uint)powf(frequency, (float)i) + 1);

		octaves[i] = malloc(sizeof(float) * octave_size * octave_size * 2);
		perlin_initialize_gradient(octaves[i], octave_size * octave_size * 2);
	}
}

float octavien_noise(float* octaves[], uint size, uint count, float x, float y, float frequency, float amplitude) {
	float final_height = 0.f;

	for (uint i = 0; i < count; i++) {
		float scale = powf(frequency, (float)i);
		float importance = powf(amplitude, (float)i);

		uint octave_size = size * (uint)scale;

		final_height += perlin_noise(octaves[i], octave_size, x * scale, y * scale) * importance;
	}

	return final_height;
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

#undef cube_index
#define cube_index(x, y, z, s) (x) * (s) * (s) + (y) * (s) + (z)

void cube_create(float* cube, uint cube_size, float (*noise_function)(float x, float y, float z)) {
	for (uint x = 0; x < cube_size; ++x) {
		for (uint y = 0; y < cube_size; ++y) {
			for (uint z = 0; z < cube_size; z++) {
				cube[cube_index(x, y, z, cube_size)] =
					noise_function(((float)x) / cube_size, ((float)y) / cube_size, ((float)z) / cube_size);
			}
		}
	}
}

uint cube_neightbors(float* cube, uint cube_size, uint x, uint y, uint z) {
	uint count = 0;

	for (int i = x - 1; i < x + 1; i++)
		for (int j = y - 1; j < y + 1; j++)
			for (int k = z - 1; k < z + 1; k++)
				if (i >= 0 && j >= 0 && k >= 0 && i < cube_size && j < cube_size && k < cube_size)
					count += (cube[cube_index(i, j, k, cube_size)] < 0.5f);

	return count;
}

uint cube_edges(uint* edges, float* cube, uint cube_size) {
	uint edges_count = 0;

	for (uint x = 0; x < cube_size; x++) {
		for (uint y = 0; y < cube_size; y++) {
			for (uint z = 0; z < cube_size; z++) {
				if (cube[cube_index(x, y, z, cube_size)] == 1.f && cube_neightbors(cube, cube_size, x, y, z) > 0) {
					edges[cube_index(x, y, z, cube_size)] = 1;
					edges_count++;
				}
				else
					edges[cube_index(x, y, z, cube_size)] = 0;
			}
		}
	}

	return edges_count;
}

void cube_vertices(uint* edges, uint cube_size, unsigned short* elements, Vector3* vertices, float spacing) {
	uint vertices_index = 0;

	uint x, y, z;

beggining_of_loop:
	for (x = 0; x < cube_size; x++) {
		for (y = 0; y < cube_size; y++) {
			for (z = 0; z < cube_size; z++) {
				// Look for an unoccupied vertex
				if (edges[cube_index(x, y, z, cube_size)]) {
					uint c = 0;

					for (uint i = 0; i < vertices_index; i++) {
						if (vertices[i].x == (float)x / spacing && 
							vertices[i].y == (float)y / spacing && 
							vertices[i].z == (float)z / spacing)
							break;
						else
							c++;
					}

					if (c >= vertices_index)
						goto end_of_loop;
				}
			}
		}
	}

	return;

end_of_loop:
	while (1) {
		for (uint i = 0; i < vertices_index; i++) {
			if (vertices[i].x == (float)x / spacing &&
				vertices[i].y == (float)y / spacing &&
				vertices[i].z == (float)z / spacing)
				goto beggining_of_loop;
		}

		vertices[vertices_index].x = (float)x / spacing;
		vertices[vertices_index].y = (float)y / spacing;
		vertices[vertices_index].z = (float)z / spacing;

		vertices_index++;
	}
}

#undef index
#define index(x, y, s) (x) * (s) + (y)

void terrain_elements(unsigned short* elements, uint terrain_size) {
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
