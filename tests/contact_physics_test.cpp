#include "engine2/Scene.hpp"
#include "engine2/SceneIO.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
void check(bool ok,const char* message) {
    if (!ok) throw std::runtime_error(message);
}
glm::mat4 at(float x,float y,float z) {
    return glm::translate(glm::mat4(1.0f),{x,y,z});
}
float py(const engine2::Scene& s,engine2::Entity e) {
    return s.worldTransform(e)[3][1];
}
float px(const engine2::Scene& s,engine2::Entity e) {
    return s.worldTransform(e)[3][0];
}
void advance(engine2::Scene& s,int frames) {
    for (int i=0;i<frames;++i)
        (void)s.advance(engine2::Scene::fixedDeltaSeconds);
}
template<class F>
void invalid(F&& f,const char* message) {
    bool threw=false;
    try {f();} catch(const std::invalid_argument&) {threw=true;}
    check(threw,message);
}
void deleteKey(std::string& json,const std::string& key) {
    const std::size_t found=json.find("\"" + key + "\": ");
    check(found!=std::string::npos,"Required scene JSON key not found");
    const std::size_t lineStart=json.rfind('\n',found);
    const std::size_t lineEnd=json.find('\n',found);
    check(lineStart!=std::string::npos && lineEnd!=std::string::npos,
          "Expected line-based serialized JSON");
    json.erase(lineStart+1,lineEnd-lineStart);
}
}

int main() {
    using namespace engine2;
    Scene frictionless;
    Scene friction;
    const Entity a=frictionless.createEntity("sliding");
    const Entity b=friction.createEntity("sliding");
    frictionless.setLocalTransform(a,at(0,0.51f,0));
    friction.setLocalTransform(b,at(0,0.51f,0));
    RigidBodyComponent body;
    body.shape=ColliderShape::Box;
    body.halfExtents={0.5f,0.5f,0.5f};
    body.groundHeight=0.0f;
    body.velocity={2,0,0};
    body.restitution=0.0f;
    frictionless.setRigidBody(a,body);
    body.staticFriction=0.85f;
    body.dynamicFriction=0.6f;
    friction.setRigidBody(b,body);
    advance(frictionless,90);
    advance(friction,90);
    check(std::abs(frictionless.rigidBody(a).velocity.x-2.0f)<0.0001f,
          "Zero friction unexpectedly slowed a legacy sliding body");
    check(friction.rigidBody(b).velocity.x<0.1f &&
          px(friction,b)+1.0f<px(frictionless,a),
          "Ground friction did not stop/slows a dynamic box");
    check(py(friction,b)>=0.4999f && friction.stats().solverPasses==8U*90U,
          "Floor constraint or sequential solver stats are incorrect");

    // Two supported dynamic boxes rest on a third static box. The old
    // single-pass pair solver allowed cumulative penetration and jitter.
    Scene stack;
    const auto base=stack.createEntity("base");
    const auto middle=stack.createEntity("middle");
    const auto top=stack.createEntity("top");
    stack.setLocalTransform(base,at(0,0.5f,0));
    stack.setLocalTransform(middle,at(0,1.49f,0));
    stack.setLocalTransform(top,at(0,2.48f,0));
    RigidBodyComponent box;
    box.shape=ColliderShape::Box;
    box.halfExtents={0.5f,0.5f,0.5f};
    box.groundHeight=0;
    box.restitution=0.0f;
    box.isStatic=true;
    box.gravity=false;
    stack.setRigidBody(base,box);
    box.isStatic=false;
    box.gravity=true;
    stack.setRigidBody(middle,box);
    stack.setRigidBody(top,box);
    advance(stack,180);
    check(std::abs(py(stack,base)-0.5f)<0.0001f,
          "Static support box moved");
    check(py(stack,middle)>1.43f && py(stack,middle)<1.57f &&
          py(stack,top)>2.37f && py(stack,top)<2.6f,
          "Stacked boxes must remain supported without sinking");
    check(stack.stats().boxContacts>0 &&
          stack.stats().solverPasses==8U*180U,
          "Stack contacts or eight-pass solver telemetry missing");

    // New coefficients survive JSON roundtrip; older v1 JSON without the
    // two optional friction properties still parses with zero coefficients.
    SceneAssetRegistry assets;
    const std::string current=serializeScene(friction,assets);
    const Scene restored=deserializeScene(current,assets);
    check(restored.entities().size()==1 &&
          std::abs(restored.rigidBody(restored.entities()[0]).staticFriction-0.85f)<1.e-5f &&
          std::abs(restored.rigidBody(restored.entities()[0]).dynamicFriction-0.6f)<1.e-5f &&
          serializeScene(restored,assets)==current,
          "Scene JSON friction roundtrip failed");
    std::string legacy=current;
    deleteKey(legacy,"staticFriction");
    deleteKey(legacy,"dynamicFriction");
    const Scene old=deserializeScene(legacy,assets);
    const auto oldHandle=old.entities()[0];
    check(old.rigidBody(oldHandle).staticFriction==0 &&
          old.rigidBody(oldHandle).dynamicFriction==0,
          "Old scene JSON must default to frictionless bodies");

    // Validation must reject bad values without corrupting live body state.
    for (float value : {-1.0f,5.0f,
                        std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::quiet_NaN()}) {
        invalid([&]{
            RigidBodyComponent modified=friction.rigidBody(b);
            modified.staticFriction=value;
            friction.setRigidBody(b,modified);
        },"Invalid static friction coefficient was accepted");
    }
    invalid([&]{
        RigidBodyComponent modified=friction.rigidBody(b);
        modified.dynamicFriction=1.0f;
        friction.setRigidBody(b,modified);
    },"Dynamic friction greater than static friction was accepted");
    check(friction.rigidBody(b).staticFriction==0.85f &&
          friction.rigidBody(b).dynamicFriction==0.6f,
          "Rejected friction edit mutated existing body");
    return 0;
}
