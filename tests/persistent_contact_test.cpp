#include "engine2/Scene.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <stdexcept>

namespace {
void require(bool okay,const char* message) {
    if (!okay) throw std::runtime_error(message);
}
glm::mat4 at(float y) {
    return glm::translate(glm::mat4(1.0f),glm::vec3(0,y,0));
}
engine2::Entity makeBox(engine2::Scene& scene,float y,bool isStatic) {
    using namespace engine2;
    const Entity entity=scene.createEntity(isStatic ? "base" : "stack box");
    scene.setLocalTransform(entity,at(y));
    RigidBodyComponent body;
    body.shape=ColliderShape::Box;
    body.halfExtents={0.5f,0.5f,0.5f};
    body.gravity=!isStatic;
    body.groundHeight=-100.0f;
    body.mass=1.0f;
    body.restitution=0.0f;
    body.staticFriction=0.8f;
    body.dynamicFriction=0.6f;
    body.isStatic=isStatic;
    scene.setRigidBody(entity,body);
    return entity;
}
float y(const engine2::Scene& scene,engine2::Entity entity) {
    return scene.worldTransform(entity)[3][1];
}
}

int main() {
    using namespace engine2;

    Scene scene;
    makeBox(scene,0.0f,true);
    const Entity lower=makeBox(scene,1.0f,false);
    const Entity upper=makeBox(scene,2.0f,false);

    // Let gravity establish a two-contact resting stack. The first contact
    // step creates the cache; later fixed steps should reuse it.
    for (int i=0;i<180;++i)
        (void)scene.advance(Scene::fixedDeltaSeconds);

    const SceneStats settled=scene.stats();
    require(settled.persistentContacts>=2,
            "Resting box stack must retain persistent pair contacts");
    require(settled.contactWarmStarts>100,
            "Resting contacts must be warm-started across fixed steps");
    require(std::abs(y(scene,lower)-1.0f)<0.02f &&
            std::abs(y(scene,upper)-2.0f)<0.03f,
            "Persistent contacts must keep a resting stack near its support");
    require(std::abs(scene.rigidBody(lower).velocity.y)<0.05f &&
            std::abs(scene.rigidBody(upper).velocity.y)<0.05f,
            "Warm-started resting stack retained vertical jitter");

    // Manual transform edits invalidate only contacts involving that entity,
    // so stale impulses cannot be applied after a teleport.
    const std::size_t beforeTeleport=scene.stats().persistentContacts;
    scene.setLocalTransform(upper,at(4.0f));
    const SceneStats invalidated=scene.stats();
    require(invalidated.persistentContacts<beforeTeleport,
            "Teleport must invalidate persistent contacts for the moved body");

    const std::uint64_t warmBefore=invalidated.contactWarmStarts;
    (void)scene.advance(Scene::fixedDeltaSeconds);
    require(scene.stats().contactWarmStarts>=warmBefore,
            "Warm-start telemetry must remain monotonic");
    require(y(scene,upper)>3.9f,
            "Teleported body received a stale persistent contact impulse");

    // Replacing rigid-body parameters also invalidates cached impulses.
    RigidBodyComponent lowerBody=scene.rigidBody(lower);
    scene.setRigidBody(lower,lowerBody);
    require(scene.stats().persistentContacts==0,
            "Rigid-body replacement must invalidate its cached contacts");

    return 0;
}
