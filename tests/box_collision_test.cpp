#include "engine2/CollisionMath.hpp"
#include "engine2/Scene.hpp"
#include "engine2/SceneIO.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <stdexcept>
#include <string>

namespace {

void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
bool close(float a, float b, float eps = 0.002f) {
    return std::abs(a-b) < eps;
}
glm::mat4 at(glm::vec3 p) {
    return glm::translate(glm::mat4(1.0f),p);
}
void steps(engine2::Scene& scene,int count) {
    for(int i=0;i<count;++i)
        (void)scene.advance(engine2::Scene::fixedDeltaSeconds);
}
template<typename F>
void rejected(F fn,const char* message) {
    bool invalid=false;
    try {fn();} catch(const std::exception&) {invalid=true;}
    check(invalid,message);
}

}

int main() {
    using namespace engine2;

    // Sphere outside a box: the normal must point from sphere toward box.
    const auto face = sphereBoxContact({-1.0f,0,0}, 0.6f,
                                       {0,0,0},{0.5f,0.5f,0.5f});
    check(face.colliding && close(face.penetration,0.1f) &&
          face.normal.x > 0.999f,
          "Sphere-box face normal or penetration incorrect");
    check(!sphereBoxContact({-1.2f,0,0},0.6f,
                            {0,0,0},{0.5f,0.5f,0.5f}).colliding,
          "Distant sphere must not hit a box");

    const auto corner=sphereBoxContact({0.75f,0.75f,0.0f},0.4f,
                                       {0,0,0},glm::vec3(0.5f));
    check(corner.colliding && corner.normal.x < -0.6f &&
          corner.normal.y < -0.6f,
          "Sphere-box corner contact direction incorrect");

    // Center inside the box: choose nearest exit face, with finite normal.
    const auto inside=sphereBoxContact({0.4f,0.0f,0.0f},0.25f,
                                       {0,0,0},glm::vec3(0.5f));
    check(inside.colliding && close(inside.penetration,0.35f) &&
          inside.normal.x < -0.999f,
          "Sphere inside box must resolve toward nearest exit face");

    const ColliderGeometry sphere{ColliderShape::Sphere,0.6f,glm::vec3(0.5f)};
    const ColliderGeometry box{ColliderShape::Box,0.5f,glm::vec3(0.5f)};
    const auto ab=colliderContact(sphere,{-1.0f,0,0},box,{0,0,0});
    const auto ba=colliderContact(box,{0,0,0},sphere,{-1.0f,0,0});
    check(ab.colliding && ba.colliding &&
          close(ab.penetration,ba.penetration) &&
          ab.normal.x > 0.9f && ba.normal.x < -0.9f,
          "Reversing sphere-box order must invert contact normal");

    const auto overlappingBoxes=boxBoxContact({-0.2f,0,0},{0.5f,0.5f,0.5f},
                                               {0.5f,0,0},{0.5f,0.5f,0.5f});
    check(overlappingBoxes.colliding &&
          close(overlappingBoxes.penetration,0.3f) &&
          overlappingBoxes.normal.x > 0.99f,
          "Box-box minimum translation axis incorrect");
    check(!boxBoxContact({-2,0,0},glm::vec3(0.5f),{2,0,0},glm::vec3(0.5f)).colliding,
          "Separated boxes cannot collide");

    Scene runtime;
    const auto obstacle=runtime.createEntity("fixed AABB");
    const auto projectile=runtime.createEntity("ball");
    runtime.setLocalTransform(obstacle,at({0,1,0}));
    runtime.setLocalTransform(projectile,at({-1.65f,1,0}));
    RigidBodyComponent fixed;
    fixed.shape=ColliderShape::Box;
    fixed.halfExtents={0.5f,0.5f,0.5f};
    fixed.isStatic=true;
    fixed.gravity=false;
    fixed.restitution=0.8f;
    runtime.setRigidBody(obstacle,fixed);
    RigidBodyComponent moving;
    moving.radius=0.45f;
    moving.restitution=0.8f;
    moving.gravity=false;
    moving.velocity={3.2f,0,0};
    runtime.setRigidBody(projectile,moving);
    steps(runtime,36);
    check(runtime.stats().boxContacts >= 1 &&
          runtime.stats().sphereContacts == 0,
          "Static AABB collision not counted");
    check(close(runtime.worldTransform(obstacle)[3][0],0.0f,0.0001f) &&
          runtime.rigidBody(projectile).velocity.x < -2.4f,
          "Static AABB did not rebound moving sphere");
    check(runtime.worldTransform(projectile)[3][0] <= -0.94f,
          "Sphere penetrated static AABB");

    Scene dynamic;
    const auto left=dynamic.createEntity();
    const auto right=dynamic.createEntity();
    dynamic.setLocalTransform(left,at({-0.6f,0,0}));
    dynamic.setLocalTransform(right,at({0.6f,0,0}));
    RigidBodyComponent b1,b2;
    b1.shape=ColliderShape::Box; b2.shape=ColliderShape::Box;
    b1.halfExtents=glm::vec3(0.5f); b2.halfExtents=glm::vec3(0.5f);
    b1.gravity=false; b2.gravity=false;
    b1.restitution=1.0f; b2.restitution=1.0f;
    b1.velocity={2,0,0}; b2.velocity={-2,0,0};
    dynamic.setRigidBody(left,b1);
    dynamic.setRigidBody(right,b2);
    steps(dynamic,10);
    check(dynamic.stats().boxContacts > 0 &&
          dynamic.rigidBody(left).velocity.x < -1.9f &&
          dynamic.rigidBody(right).velocity.x > 1.9f,
          "Dynamic box-box collision did not exchange velocities");

    // Serialize a box and read it through the existing scene JSON v1 schema.
    SceneAssetRegistry assets;
    const std::string state=serializeScene(runtime,assets);
    Scene restored=deserializeScene(state,assets);
    const auto handles=restored.entities();
    check(handles.size()==2 &&
          restored.rigidBody(handles[0]).shape==ColliderShape::Box &&
          close(restored.rigidBody(handles[0]).halfExtents.x,0.5f),
          "AABB collider was not persisted in JSON");
    check(serializeScene(restored,assets)==state,
          "AABB scene JSON roundtrip altered the data");

    // v1.2 JSON without the extra keys MUST still load as sphere colliders.
    Scene backward;
    const auto old=backward.createEntity();
    backward.setRigidBody(old,moving);
    std::string oldJson=serializeScene(backward,assets);
    auto removeProperty=[&](const std::string& name, bool arrayValue) {
        const std::string needle="\"" + name + "\": ";
        const auto found=oldJson.find(needle);
        check(found!=std::string::npos,"Scene JSON test field missing");
        const auto lineStart=oldJson.rfind('\n',found);
        check(lineStart!=std::string::npos,"JSON field line missing");
        const auto valueEnd=arrayValue
            ? oldJson.find(']',found) : oldJson.find('\n',found);
        check(valueEnd!=std::string::npos,"JSON field value missing");
        const auto lineEnd=oldJson.find('\n',valueEnd);
        check(lineEnd!=std::string::npos,"JSON field termination missing");
        oldJson.erase(lineStart+1,lineEnd-lineStart);
    };
    removeProperty("shape",false);
    removeProperty("halfExtents",true);
    Scene legacy=deserializeScene(oldJson,assets);
    check(legacy.entities().size()==1 &&
          legacy.rigidBody(legacy.entities()[0]).shape==ColliderShape::Sphere,
          "Older v1.2 JSON must default missing collider shape to sphere");
    rejected([&] {
        RigidBodyComponent invalid;
        invalid.shape=ColliderShape::Box;
        invalid.halfExtents={0.0f,1.0f,1.0f};
        runtime.setRigidBody(projectile,invalid);
    },"Zero box extent should be rejected");
    return 0;
}
