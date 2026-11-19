#include "engine2/Scene.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <stdexcept>

namespace {
void require(bool okay, const char* message) {
    if (!okay) throw std::runtime_error(message);
}
glm::mat4 translation(float x, float y = 2.0f, float z = 0.0f) {
    return glm::translate(glm::mat4(1.0f), glm::vec3(x,y,z));
}
float x(const engine2::Scene& scene, engine2::Entity entity) {
    return scene.worldTransform(entity)[3][0];
}
void step(engine2::Scene& scene) {
    (void)scene.advance(engine2::Scene::fixedDeltaSeconds);
}
bool close(float a, float b, float tolerance = 0.006f) {
    return std::abs(a - b) < tolerance;
}

engine2::Entity box(engine2::Scene& scene, float origin, float speed,
                    bool ccd = true, float mass = 1.0f) {
    using namespace engine2;
    const Entity entity = scene.createEntity("CCD box");
    scene.setLocalTransform(entity, translation(origin));
    RigidBodyComponent body;
    body.shape = ColliderShape::Box;
    body.halfExtents = {0.2f,0.2f,0.2f};
    body.gravity = false;
    body.groundHeight = -100.0f;
    body.mass = mass;
    body.restitution = 1.0f;
    body.continuous = ccd;
    body.velocity = {speed,0,0};
    scene.setRigidBody(entity, body);
    return entity;
}

engine2::Entity sphere(engine2::Scene& scene, float origin, float speed,
                       bool ccd = true) {
    using namespace engine2;
    const Entity entity = scene.createEntity("CCD sphere");
    scene.setLocalTransform(entity, translation(origin));
    RigidBodyComponent body;
    body.shape = ColliderShape::Sphere;
    body.radius = 0.2f;
    body.gravity = false;
    body.groundHeight = -100.0f;
    body.restitution = 1.0f;
    body.continuous = ccd;
    body.velocity = {speed,0,0};
    scene.setRigidBody(entity, body);
    return entity;
}

engine2::Entity wall(engine2::Scene& scene, float origin) {
    using namespace engine2;
    const Entity entity = scene.createEntity("CCD wall");
    scene.setLocalTransform(entity, translation(origin));
    RigidBodyComponent body;
    body.shape = ColliderShape::Box;
    body.halfExtents = {0.1f,0.5f,0.5f};
    body.gravity = false;
    body.isStatic = true;
    body.restitution = 1.0f;
    scene.setRigidBody(entity, body);
    return entity;
}
}

int main() {
    using namespace engine2;

    // Discrete AABB motion tunnels through a thin wall; opt-in CCD bounces at
    // the exact face and consumes the remaining fixed-step time.
    {
        Scene discrete;
        const Entity projectile = box(discrete, -2.0f, 300.0f, false);
        wall(discrete, 0.0f);
        step(discrete);
        require(x(discrete, projectile) > 2.5f &&
                discrete.stats().ccdHits == 0,
                "Legacy discrete box should tunnel at high speed");

        Scene continuous;
        const Entity projectileCcd = box(continuous, -2.0f, 300.0f);
        wall(continuous, 0.0f);
        step(continuous);
        const SceneStats stats = continuous.stats();
        require(x(continuous, projectileCcd) < -3.5f &&
                close(continuous.rigidBody(projectileCcd).velocity.x, -300.0f),
                "Dynamic box CCD must bounce from a static wall");
        require(stats.ccdHits == 1 && stats.ccdDynamicHits == 0 &&
                stats.ccdBudgetExhaustions == 0,
                "Static-wall box CCD telemetry is incorrect");
    }

    // Two moving boxes opt in and exchange velocity instead of crossing.
    {
        Scene scene;
        const Entity first = box(scene, -2.0f, 300.0f);
        const Entity second = box(scene, 2.0f, -300.0f);
        step(scene);
        require(x(scene, first) < -3.3f && x(scene, second) > 3.3f &&
                close(scene.rigidBody(first).velocity.x, -300.0f) &&
                close(scene.rigidBody(second).velocity.x, 300.0f) &&
                scene.stats().ccdDynamicHits == 1,
                "Moving box/box CCD must resolve relative-motion impact");
    }

    // Mixed moving shapes share the same global event timeline.
    {
        Scene scene;
        const Entity movingBox = box(scene, -2.0f, 300.0f);
        const Entity movingSphere = sphere(scene, 2.0f, -300.0f);
        step(scene);
        require(x(scene, movingBox) < -3.3f &&
                x(scene, movingSphere) > 3.3f &&
                close(scene.rigidBody(movingBox).velocity.x, -300.0f) &&
                close(scene.rigidBody(movingSphere).velocity.x, 300.0f) &&
                scene.stats().ccdDynamicHits == 1,
                "Moving box/sphere CCD must share dynamic event handling");
    }

    // Preserve opt-in semantics: both moving participants must request CCD.
    {
        Scene scene;
        const Entity movingBox = box(scene, -2.0f, 300.0f, true);
        const Entity movingSphere = sphere(scene, 2.0f, -300.0f, false);
        step(scene);
        require(scene.stats().ccdDynamicHits == 0 &&
                x(scene, movingBox) > x(scene, movingSphere),
                "Unflagged dynamic body must preserve discrete CCD semantics");
    }

    // A fast box can hit two walls in one fixed step, exercising broadphase
    // rebuild after the first impact rather than using its original sweep.
    {
        Scene corridor;
        wall(corridor, -1.5f);
        wall(corridor, 1.5f);
        const Entity projectile = box(corridor, 0.0f, 240.0f);
        step(corridor);
        require(corridor.stats().ccdHits == 2 &&
                corridor.stats().ccdDynamicHits == 0 &&
                corridor.stats().ccdBudgetExhaustions == 0 &&
                close(corridor.rigidBody(projectile).velocity.x, 240.0f) &&
                close(x(corridor, projectile), -0.8f, 0.02f),
                "Dynamic box multi-impact CCD failed in wall corridor");
    }

    return 0;
}
