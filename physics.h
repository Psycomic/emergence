#ifndef PHYSICS_HEADER
#define PHYSICS_HEADER

#include "linear_algebra.h"

#ifndef PHYSICS_INTERNAL

typedef void PhysicBody;
typedef void World;

World* world_create(Vector3 gravity, uint capacity);
PhysicBody* world_body_add(World* world, Shape* shape, float mass);
void world_update(World* world, float delta);

void physic_body_apply_force(PhysicBody* body, Vector3 force);
Shape* physic_body_get_shape(PhysicBody* body);

#endif // !PHYSICS_INTERNAL
#endif // !PHYSICS_HEADER