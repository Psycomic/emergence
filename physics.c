#define PHYSICS_INTERNAL

#include <stdio.h>
#include <math.h>
#include <stdlib.h>

#include "linear_algebra.h"

#define DEFAULT_WORLD_CAPACITY 10

typedef struct {
	Shape* shape;
	Vector3 velocity;
	Vector3 acceleration;
	Vector3 new_acceleration;
	float mass_inv;
} PhysicBody;

typedef struct {
	PhysicBody* bodies;

	Vector3 gravity;
	uint bodies_number;
} World;

void physic_body_create(PhysicBody* body, Shape* shape, float mass) {
	body->shape = shape;

	if (mass == 0.f)
		body->mass_inv = 0.f;
	else
		body->mass_inv = 1.f / mass;

	body->velocity = (Vector3) { { 0.f, 0.f, 0.f } };
	body->acceleration = (Vector3) { { 0.f, 0.f, 0.f } };
	body->new_acceleration = (Vector3) { { 0.f, 0.f, 0.f } };
}

void physic_body_apply_force(PhysicBody* body, Vector3 force) {
	vector3_scalar_mul(&force, force, body->mass_inv);
	vector3_add(&body->new_acceleration, body->new_acceleration, force);
}

void physic_body_update(PhysicBody* body, float delta) {
	Vector3 v;
	vector3_scalar_mul(&v, body->velocity, delta);
	Vector3 a;
	vector3_scalar_mul(&a, body->acceleration, delta * delta * 0.5f);
	vector3_add(&a, a, v);
	vector3_add(&body->shape->position, body->shape->position, a);

	Vector3 b;
	vector3_add(&b, body->acceleration, body->new_acceleration);
	vector3_scalar_mul(&b, b, delta * 0.5f);

	Vector3 new_vel;
	vector3_add(&new_vel, body->velocity, b);

	body->velocity = new_vel;
	body->acceleration = body->new_acceleration;
}

Shape* physic_body_get_shape(PhysicBody* body) {
	return body->shape;
}

void physic_body_solve_collision(PhysicBody* body1, PhysicBody* body2, Collision* collision) {
	// Solve only if colliding
	if (collision->depth < 0.f)
		return;

	Vector3 collision_normal = collision->normal;
	vector3_normalize(&collision_normal, collision_normal);

	// Computing the relative velocity and the velocity along the normal
	Vector3 relative_velocity;
	vector3_sub(&relative_velocity, body2->velocity, body1->velocity);

	float vel_along_normal = vector3_dot(relative_velocity, collision_normal);

	// If they won't intersect, don't slove
	if (vel_along_normal < 0)
		return;

	const float restitution = 1.f;
	const float j = (-(1 + restitution) * vel_along_normal) / (body1->mass_inv + body2->mass_inv);

	Vector3 impulse;
	vector3_scalar_mul(&impulse, collision_normal, j);

	Vector3 impulse_body1;
	Vector3 impulse_body2;
	vector3_scalar_mul(&impulse_body1, impulse, body1->mass_inv);
	vector3_scalar_mul(&impulse_body2, impulse, body2->mass_inv);

	// Applying the impulses
	vector3_sub(&body1->velocity, body1->velocity, impulse_body1);
	vector3_add(&body2->velocity, body2->velocity, impulse_body2);

	// Position correction
	const float percent = 0.0f; // usually 20% to 80%
	const float correction = (collision->depth / (body1->mass_inv + body2->mass_inv)) * percent;

	Vector3 correction_body1;
	Vector3 correction_body2;

	vector3_scalar_mul(&correction_body1, collision_normal, -correction * body1->mass_inv);
	vector3_scalar_mul(&correction_body2, collision_normal, correction * body2->mass_inv);

	for (uint i = 0; i < body1->shape->vertices_size; ++i)
		vector3_add(body1->shape->vertices + i, body1->shape->vertices[i], correction_body1);

	for (uint i = 0; i < body2->shape->vertices_size; ++i)
		vector3_add(body2->shape->vertices + i, body2->shape->vertices[i], correction_body2);
}

World* world_create(Vector3 gravity, uint capacity) {
	World* world = malloc(sizeof(World));

	world->bodies_number = 0;
	world->gravity = gravity;

	world->bodies = malloc(sizeof(PhysicBody) * capacity);

	return world;
}

PhysicBody* world_body_add(World* world, Shape* shape, float mass) {
	PhysicBody* new_body = world->bodies + world->bodies_number;

	physic_body_create(new_body, shape, mass);
	world->bodies_number++;

	return new_body;
}

void world_update(World* world, float delta) {
	for (uint i = 0; i < world->bodies_number; ++i) {
		physic_body_apply_force(world->bodies + i, world->gravity);
		physic_body_update(world->bodies + i, delta);

		for (uint j = i + 1; j < world->bodies_number; ++j) {
			Collision collision = shape_shape_collide(world->bodies[i].shape, world->bodies[j].shape);
			physic_body_solve_collision(world->bodies + i, world->bodies + j, &collision);
		}
	}
}

#undef PHYSICS_INTERNAL
