#ifndef NOISE_HEADER
#define NOISE_HEADER

#include "linear_algebra.h"

typedef unsigned int uint;

float random_float(void);
float lerp(float a0, float a1, float w);
float llerp(float a, float b, float w);
float sign(float x);

// Perlin / octavien noise : interpolated white noise, with layers
void perlin_initialize_gradient(float* gradient, uint size);
float perlin_noise(float* gradient, uint size, float x, float y);

void octavien_initialize_gradient(float* octaves[], uint layers, uint size, float frequency);
float octavien_noise(float* octaves[], uint size, uint count, float x, float y, float frequency, float amplitude);

// Voroi noise: cellular-like patterns based on points
float voronoi_noise(uint dimensions, float* points, uint points_number, float* position, float (*noise_accessor)(float* distance, float* position));

float cellular_noise(float* distance, float* position);
float worley_noise(float* distance, float* position);
float cave_noise(float* distance, float* position);

// Some interesting noise
void hopalong_fractal(Vector3* destination, uint iterations_number, float a, float b, float c, float scale);

// Create shapes from noise function
void terrain_create(Vector3* terrain_vertices, uint size, float height, float width, float (*noise_function)(float x, float y));

// The resulting array is of size (terrain_size - 1) * (terrain_size - 1) * 6
void terrain_elements(unsigned short* elements, uint terrain_size);

#endif
