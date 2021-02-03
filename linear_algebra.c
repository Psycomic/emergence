#include <assert.h>
#include <float.h>

#include <stdlib.h>
#include <stdio.h>

#include <string.h>

#include <immintrin.h>

#define _USE_MATH_DEFINES
#include <math.h>

#include "linear_algebra.h"
#include "misc.h"

void vector2_add(Vector2* dest, Vector2 a, Vector2 b) {
	dest->x = a.x + b.x;
	dest->y = a.y + b.y;
}

void vector2_sub(Vector2* dest, Vector2 a, Vector2 b) {
	dest->x = a.x - b.x;
	dest->y = a.y - b.y;
}

void vector2_neg(Vector2* dest, Vector2 a) {
	dest->x = -a.x;
	dest->y = -a.y;
}

void vector3_add(Vector3* dest, Vector3 a, Vector3 b) {
	dest->x = a.x + b.x;
	dest->y = a.y + b.y;
	dest->z = a.z + b.z;
}

void vector3_sub(Vector3* dest, Vector3 a, Vector3 b) {
	dest->x = a.x - b.x;
	dest->y = a.y - b.y;
	dest->z = a.z - b.z;
}

void vector3_neg(Vector3* dest) {
	dest->x = -dest->x;
	dest->y = -dest->y;
	dest->z = -dest->z;
}

void vector3_scalar_mul(Vector3* dest, Vector3 a, float s) {
	dest->x = a.x * s;
	dest->y = a.y * s;
	dest->z = a.z * s;
}

void vector3_cross(Vector3* dest, Vector3 a, Vector3 b) {
	dest->x = a.y * b.z - a.z * b.y;
	dest->y = a.z * b.x - a.x * b.z;
	dest->z = a.x * b.y - a.y * b.x;
}

float vector3_dot(Vector3 a, Vector3 b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

float vector3_magnitude(Vector3 a) {
	return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
}

void vector3_normalize(Vector3* dest, Vector3 src) {
	float magnitude = vector3_magnitude(src);

	dest->x = src.x / magnitude;
	dest->y = src.y / magnitude;
	dest->z = src.z / magnitude;
}

void mat4_create_translation(Mat4 destination, Vector3 direction) {
	for (uint i = 0; i < 4; ++i) {
		for (uint j = 0; j < 4; ++j) {
			if (i == 3 && j != 3)
				destination[j + i * 4] = direction.D[j];
			else if (i == j)
				destination[j + i * 4] = 1;
			else
				destination[j + i * 4] = 0;
		}
	}
}

void mat4_create_scale(Mat4 destination, Vector3 scale) {
	for (uint i = 0; i < 4; ++i) {
		for (uint j = 0; j < 4; ++j) {
			if (i == j && j < 3)
				destination[j + i * 4] = scale.D[j];
			else if (i == j)
				destination[j + i * 4] = 1;
			else
				destination[j + i * 4] = 0;
		}
	}
}

void mat4_create_perspective(Mat4 destination, float far, float near, float fov, float aspect_ratio) {
#ifdef _DEBUG
	assert(fov > 0.f && fov < 180.f);
#endif

	float s = 1.f / tanf((fov / 2) * ((float)M_PI / 180));

	for (uint i = 0; i < 16; ++i) {
		destination[i] = 0;
	}

	destination[0] = s / aspect_ratio;
	destination[5] = s;

	destination[10] = -(far) / (far - near);
	destination[11] = -1;
	destination[14] = -(2 * far * near) / (far - near);
}

void mat4_create_orthogonal(Mat4 out, float left, float right, float bottom, float top, float near, float far) {
	float lr = 1.f / (left - right);
	float bt = 1.f / (bottom - top);
	float nf = 1.f / (near - far);

	out[0] = -2 * lr;
	out[1] = 0;
	out[2] = 0;
	out[3] = 0;
	out[4] = 0;
	out[5] = -2 * bt;
	out[6] = 0;
	out[7] = 0;
	out[8] = 0;
	out[9] = 0;
	out[10] = 2 * nf;
	out[11] = 0;
	out[12] = (left + right) * lr;
	out[13] = (top + bottom) * bt;
	out[14] = (far + near) * nf;
	out[15] = 1;
}

void mat4_create_rotation_x(Mat4 destination, float angle) {
	for (uint i = 0; i < 4; ++i) {
		for (uint j = 0; j < 4; ++j) {
			if (i == j)
				destination[j + i * 4] = 1;
			else
				destination[j + i * 4] = 0;
		}
	}

	destination[5] = cosf(angle);
	destination[6] = sinf(angle);

	destination[9] = -sinf(angle);
	destination[10] = cosf(angle);
}

void mat4_create_rotation_y(Mat4 destination, float angle) {
	for (uint i = 0; i < 4; ++i) {
		for (uint j = 0; j < 4; ++j) {
			if (i == j)
				destination[j + i * 4] = 1;
			else
				destination[j + i * 4] = 0;
		}
	}

	destination[0] = cosf(angle);
	destination[2] = sinf(angle);

	destination[8] = -sinf(angle);
	destination[10] = cosf(angle);
}

void mat4_create_rotation_z(Mat4 destination, float angle) {
	for (uint i = 0; i < 4; ++i) {
		for (uint j = 0; j < 4; ++j) {
			if (i == j)
				destination[j + i * 4] = 1;
			else
				destination[j + i * 4] = 0;
		}
	}

	destination[0] = cosf(angle);
	destination[1] = sinf(angle);

	destination[4] = -sinf(angle);
	destination[5] = cosf(angle);
}

void mat4_vector4_mul(Vector4* destination, Vector4 v, Mat4 mat) {
	for (uint i = 0; i < 4; ++i) {
		destination->D[i] = 0.f;

		for (uint j = 0; j < 4; ++j) {
			destination->D[i] += v.D[j] * mat[j + i * 4];
		}
	}
}

void mat4_vector3_mul(Vector3* destination, Vector3 v, Mat4 mat) {
	for (uint i = 0; i < 3; ++i) {
		destination->D[i] = 0.f;

		for (uint j = 0; j < 4; ++j) {
			if (j == 3)
				destination->D[i] += mat[j + i * 4];
			else
				destination->D[i] += v.D[j] * mat[j + i * 4];
		}
	}
}

void mat4_mat4_mul(Mat4 destination, Mat4 a, Mat4 b) {
	for (uint i = 0; i < 4; ++i) {
		for (uint j = 0; j < 4; ++j) {
			destination[j + i * 4] = 0.f;

			for (uint k = 0; k < 4; ++k) {
				destination[j + i * 4] += a[i * 4 + k] * b[j + k * 4];
			}
		}
	}
}

void mat4_print(Mat4 m) {
	for (uint i = 0; i < 4; ++i) {
		for (uint j = 0; j < 4; ++j) {
			printf("%.2f\t", m[j + i * 4]);
		}

		printf("\n");
	}
}

void triangle_normal_from_vertices(Vector3* n, Vector3 A, Vector3 B, Vector3 C) {
	Vector3 triangle_edge1;
	Vector3 triangle_edge2;

	vector3_sub(&triangle_edge1, B, A);
	vector3_sub(&triangle_edge2, C, A);

	vector3_cross(n, triangle_edge1, triangle_edge2);
}

float triangle_point_collide(Vector3 normal, Vector3 point, Vector3 p) {
	Vector3 relative_vector;
	vector3_sub(&relative_vector, p, point);

	return vector3_dot(normal, relative_vector);
}

float triangle_line_distance(Vector3 triangle_normal, Vector3 triangle_point, Vector3 line_normal, Vector3 line_point) {
	Vector3 distance_vector;
	vector3_sub(&distance_vector, triangle_point, line_point);

	float f_dot_1 = vector3_dot(distance_vector, triangle_normal);
	float f_dot_2 = vector3_dot(line_normal, triangle_normal);

	return f_dot_1 / f_dot_2;
}

void vertices_normals(Vector3* normals, Vector3* vertices, uint vertices_size, unsigned short* elements, uint elements_size) {
	for (uint i = 0; i < elements_size; i += 3) {
		triangle_normal_from_vertices(
			normals + i / 3,
			vertices[elements[i]],
			vertices[elements[i + 1]],
			vertices[elements[i + 2]]);
	}
}

BOOL in_interval(float a, float x, float y) {
	return a >= x && a <= y;
}

BOOL interval_intersect(float a_min, float a_max, float b_min, float b_max) {
	return in_interval(a_min, b_min, b_max) || in_interval(a_max, b_min, b_max) || in_interval(b_min, a_min, a_max) || in_interval(b_max, a_min, a_max);
}

float point_line_intersect(float l1_projection, float l2_projection, float l1_distance, float l2_distance) {
	return l1_projection + (l2_projection - l1_projection) * (l1_distance / (l1_distance - l2_distance));
}

Collision triangle_triangle_collide(Vector3* triangle1, Vector3* triangle2) {
	Collision result = { -1.f, {0.f, 0.f, 0.f} };

	Vector3 plane1_normal;
	Vector3 plane2_normal;

	triangle_normal_from_vertices(&plane1_normal, triangle1[0], triangle1[1], triangle1[2]);
	triangle_normal_from_vertices(&plane2_normal, triangle2[0], triangle2[1], triangle2[2]);

	float dot_plane1[3];
	float d1 = -vector3_dot(plane1_normal, triangle1[0]);

	for (uint i = 0; i < 3; i++)
		dot_plane1[i] = vector3_dot(plane1_normal, triangle2[i]) + d1;

	if ((dot_plane1[0] < 0.f) == (dot_plane1[1] < 0.f) &&
		(dot_plane1[1] < 0.f) == (dot_plane1[2] < 0.f))
		return result;

	float dot_plane2[3];
	float d2 = -vector3_dot(plane2_normal, triangle2[0]);

	for (uint i = 0; i < 3; i++)
		dot_plane2[i] = vector3_dot(plane2_normal, triangle1[i]) + d2;

	if ((dot_plane2[0] < 0.f) == (dot_plane2[1] < 0.f) &&
		(dot_plane2[1] < 0.f) == (dot_plane2[2] < 0.f))
		return result;

	Vector3 line_direction;
	vector3_cross(&line_direction, plane1_normal, plane2_normal);

	float triangle1_projection[3];
	for (uint i = 0; i < 3; i++)
		triangle1_projection[i] = vector3_dot(line_direction, triangle1[i]);

	float triangle2_projection[3];
	for (uint i = 0; i < 3; i++)
		triangle2_projection[i] = vector3_dot(line_direction, triangle2[i]);

	float plane1_distance_constant = vector3_dot(plane1_normal, plane1_normal);
	float plane2_distance_constant = vector3_dot(plane2_normal, plane2_normal);

	uint triangle1_unique_vertex;

	if ((dot_plane2[0] < 0.f) == (dot_plane2[1] < 0.f))
		triangle1_unique_vertex = 2;
	else if ((dot_plane2[0] < 0.f) == (dot_plane2[2] < 0.f))
		triangle1_unique_vertex = 1;
	else
		triangle1_unique_vertex = 0;

	uint x1 = triangle1_unique_vertex,
		y1 = (triangle1_unique_vertex + 1) % 3,
		z1 = (triangle1_unique_vertex + 2) % 3;

	uint triangle2_unique_vertex;

	if ((dot_plane1[0] < 0.f) == (dot_plane1[1] < 0.f))
		triangle2_unique_vertex = 2;
	else if ((dot_plane1[0] < 0.f) == (dot_plane1[2] < 0.f))
		triangle2_unique_vertex = 1;
	else
		triangle2_unique_vertex = 0;

	uint x2 = triangle2_unique_vertex,
		y2 = (triangle2_unique_vertex + 1) % 3,
		z2 = (triangle2_unique_vertex + 2) % 3;

	float tri1_t1 = point_line_intersect(triangle1_projection[y1], triangle1_projection[x1], dot_plane2[y1], dot_plane2[x1]),
		tri1_t2 = point_line_intersect(triangle1_projection[z1], triangle1_projection[x1], dot_plane2[z1], dot_plane2[x1]),
		tri2_t1 = point_line_intersect(triangle2_projection[y2], triangle2_projection[x2], dot_plane1[y2], dot_plane1[x2]),
		tri2_t2 = point_line_intersect(triangle2_projection[z2], triangle2_projection[x2], dot_plane1[z2], dot_plane1[x2]);

	float tri1_interval_min = min(tri1_t1, tri1_t2),
		tri1_interval_max = max(tri1_t1, tri1_t2),
		tri2_interval_min = min(tri2_t1, tri2_t2),
		tri2_interval_max = max(tri2_t1, tri2_t2);

	if (!interval_intersect(tri1_interval_min, tri1_interval_max, tri2_interval_min, tri2_interval_max))
		return result;

	result.depth = 0.f;
	result.normal = plane1_normal;

	return result;
}

Collision shape_shape_collide(Shape* shape1, Shape* shape2) {
	Collision result = { -1.f, { 0.f, 0.f, 0.f } };

	for (uint x = 0; x < shape1->elements_size; x += 3) {
		Vector3 shape1_triangle[3];

		uint id_x, id_y, id_z;

		if (shape1->elements) {
			id_x = shape1->elements[x];
			id_y = shape1->elements[x + 1];
			id_z = shape1->elements[x + 2];
		}
		else {
			id_x = x;
			id_y = x + 1;
			id_z = x + 2;
		}

		vector3_add(shape1_triangle, shape1->vertices[id_x], shape1->position);
		vector3_add(shape1_triangle + 1, shape1->vertices[id_y], shape1->position);
		vector3_add(shape1_triangle + 2, shape1->vertices[id_z], shape1->position);

		for (uint y = 0; y < shape2->elements_size; y += 3) {
			Vector3 shape2_triangle[3];

			uint id_x, id_y, id_z;

			if (shape1->elements) {
				id_x = shape2->elements[y];
				id_y = shape2->elements[y + 1];
				id_z = shape2->elements[y + 2];
			}
			else {
				id_x = y;
				id_y = y + 1;
				id_z = y + 2;
			}

			vector3_add(shape2_triangle, shape2->vertices[id_x], shape2->position);
			vector3_add(shape2_triangle + 1, shape2->vertices[id_y], shape2->position);
			vector3_add(shape2_triangle + 2, shape2->vertices[id_z], shape2->position);

			result = triangle_triangle_collide(shape1_triangle, shape2_triangle);
			if (result.depth > 0.f) {
				return result;
			}
		}
	}

	return result;
}
