#ifndef LINEAR_ALGEBRA_HEADER
#define LINEAR_ALGEBRA_HEADER

typedef unsigned int uint;
typedef unsigned char BOOL;

/// A collection of 3 floats for linear algebra
typedef union {
	struct { float x, y; };
	float D[2];
} Vector2;

typedef union {
	struct { float x, y, z; };
	float D[3];
} Vector3;

typedef union {
	struct { float x, y, z, w;  };
	float D[4];
} Vector4;

typedef float Mat4[16];

/// A Shape	object (a polyhedron) used for collision detection
typedef struct {
	Vector3 position;

	Vector3* vertices;
	unsigned short* elements;

	uint vertices_size;
	uint elements_size;
} Shape;

typedef struct {
	float depth;
	Vector3 normal;
} Collision;

void vector2_add(Vector2* dest, Vector2 a, Vector2 b);
void vector2_sub(Vector2* dest, Vector2 a, Vector2 b);
void vector2_neg(Vector2* dest, Vector2 a);

void vector3_add(Vector3* dest, Vector3 a, Vector3 b);
void vector3_sub(Vector3* dest, Vector3 a, Vector3 b);
void vector3_neg(Vector3* dest);
void vector3_scalar_mul(Vector3* dest, Vector3 a, float s);
float vector3_dot(Vector3 a, Vector3 b);
float vector3_magnitude(Vector3 a);
void vector3_normalize(Vector3* dest, Vector3 src);

void mat4_create_translation(Mat4 destination, Vector3 direction);
void mat4_create_scale(Mat4 destination, Vector3 scale);
void mat4_create_perspective(Mat4 destination, float far, float near, float fov, float aspect_ratio);
void mat4_create_orthogonal(Mat4 out, float left, float right, float bottom, float top, float near, float far);

void mat4_create_rotation_x(Mat4 destination, float angle);
void mat4_create_rotation_y(Mat4 destination, float angle);
void mat4_create_rotation_z(Mat4 destination, float angle);

void mat4_mat4_mul(Mat4 destination, Mat4 a, Mat4 b);
void mat4_vector4_mul(Vector4* destination, Vector4 v, Mat4 mat);
void mat4_vector3_mul(Vector3* destination, Vector3 v, Mat4 mat);
void mat4_print(Mat4 m);

void triangle_normal_from_vertices(Vector3* n, Vector3 A, Vector3 B, Vector3 C);
float triangle_point_collide(Vector3 normal, Vector3 point, Vector3 p);
float triangle_line_distance(Vector3 triangle_normal, Vector3 triangle_point, Vector3 line_normal, Vector3 line_point);
Collision triangle_triangle_collide(Vector3* triangle1, Vector3* triangle2);
Collision shape_shape_collide(Shape* shape1, Shape* shape2);

#endif
