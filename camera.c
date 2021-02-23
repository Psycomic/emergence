#include "camera.h"

void camera_create_rotation_matrix(Mat4 destination, float rx, float ry) {
	Mat4 rotation_matrix_x;
	Mat4 rotation_matrix_y;

	mat4_create_rotation_x(rotation_matrix_x, rx);
	mat4_create_rotation_y(rotation_matrix_y, ry);

	mat4_mat4_mul(destination, rotation_matrix_y, rotation_matrix_x); // First, Y rotation, after X rotation
}

void camera_create_final_matrix(Mat4 destination, Mat4 perspective, Mat4 rotation, Vector3 position) {
	Mat4 translation_matrix;
	Mat4 temporary_matrix;

	vector3_neg(&position);
	mat4_create_translation(translation_matrix, position);

	mat4_mat4_mul(temporary_matrix, translation_matrix, rotation);
	mat4_mat4_mul(destination, temporary_matrix, perspective);
}

void camera_init(Camera* camera, Vector3 position, float far, float near, float fov, int width, int height) {
	camera->position = position;

	camera->rx = 0.f;
	camera->ry = 0.f;

	camera->width = width;
	camera->height = height;

	mat4_create_perspective(camera->perspective_matrix, far, near, fov, (float) width / height);

	float half_width = (float)width / 2,
		half_height = (float)height / 2;

	mat4_create_orthogonal(camera->ortho_matrix, -half_width, half_width, -half_height, half_height, -20.f, 20.f);
}

void camera_get_final_matrix(Camera* camera, Mat4 final_matrix) {
	camera_create_rotation_matrix(camera->rotation_matrix, camera->rx, camera->ry);
	camera_create_final_matrix(final_matrix, camera->perspective_matrix, camera->rotation_matrix, camera->position);
}

void camera_get_direction(Camera* camera, Vector3* direction, float speed) {
	Vector3 orientation = { { 0.f, 0.f, 1.f } };

	mat4_vector3_mul(direction, orientation, camera->rotation_matrix);
	vector3_scalar_mul(direction, *direction, speed);
}

void camera_translate(Camera* camera, Vector3 direction) {
	vector3_sub(&camera->position, camera->position, direction);
}

void camera_rotate(Camera* camera, float rx, float ry) {
	camera->rx += rx;
	camera->ry += ry;
}
