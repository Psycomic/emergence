#include "linear_algebra.h"

/// A camera, that holds a position and a rotation to render
/// a scene correctly

typedef struct {
	Vector3 position;

	float rx;
	float ry;

	int width, height;
	float fov;

	Mat4 perspective_matrix;	// Not having to recreate one every time
	Mat4 ortho_matrix;
	Mat4 rotation_matrix;		// Needed for the direction
} Camera;

void camera_create_rotation_matrix(Mat4 destination, float rx, float ry);
void camera_get_direction(Camera* camera, Vector3* direction, float speed);
void camera_translate(Camera* camera, Vector3 direction);
void camera_get_final_matrix(Camera* camera, Mat4 final_matrix);
void camera_init(Camera* camera, Vector3 position, float far, float near, float fov, int width, int height);
void camera_create_final_matrix(Mat4 destination, Mat4 perspective, Mat4 rotation, Vector3 position);
void camera_create_rotation_matrix(Mat4 destination, float rx, float ry);
void camera_rotate(Camera* camera, float rx, float ry);
