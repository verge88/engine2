#include "engine2/Scene.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <stdexcept>

namespace {
using namespace engine2;

void require(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
}
glm::mat4 location(float x,float y,float z) {
    return glm::translate(glm::mat4(1.0f),glm::vec3(x,y,z));
}
struct World {
    Scene scene;
    Entity sphere;
    Entity box;
};
World build(float localY,float localZ,bool continuous=true) {
    World world;
    world.box=world.scene.createEntity("Static box");
    world.sphere=world.scene.createEntity("Fast CCD sphere");
    world.scene.setLocalTransform(world.box,location(0,2,0));
    world.scene.setLocalTransform(world.sphere,location(-2,2+localY,localZ));

    RigidBodyComponent staticBox;
    staticBox.shape=ColliderShape::Box;
    staticBox.halfExtents={0.5f,0.5f,0.5f};
    staticBox.isStatic=true;
    staticBox.gravity=false;
    staticBox.restitution=1.0f;
    world.scene.setRigidBody(world.box,staticBox);

    RigidBodyComponent ball;
    ball.shape=ColliderShape::Sphere;
    ball.radius=0.3f;
    ball.continuous=continuous;
    ball.gravity=false;
    ball.groundHeight=-100.0f;
    ball.velocity={240,0,0};
    ball.restitution=1.0f;
    world.scene.setRigidBody(world.sphere,ball);
    return world;
}
void step(Scene& scene) {
    (void)scene.advance(Scene::fixedDeltaSeconds);
}
bool near(float a,float b,float eps=0.005f) {
    return std::abs(a-b)<eps;
}
}

int main() {
    using namespace engine2;

    {
        auto face=build(0,0);
        step(face.scene);
        const auto stats=face.scene.stats();
        require(stats.ccdHits==1 && stats.ccdBoxFaceHits==1 &&
                stats.ccdBoxEdgeHits==0 && stats.ccdBoxCornerHits==0 &&
                near(face.scene.rigidBody(face.sphere).velocity.x,-240),
                "Face CCD impact must preserve direct normal restitution");
    }
    {
        auto edge=build(0.7f,0);
        step(edge.scene);
        const auto stats=edge.scene.stats();
        const auto velocity=edge.scene.rigidBody(edge.sphere).velocity;
        require(stats.ccdHits==1 && stats.ccdBoxEdgeHits==1 &&
                stats.ccdBoxFaceHits==0 && stats.ccdBoxCornerHits==0 &&
                velocity.x<0 && velocity.y>150,
                "Edge CCD impact must deflect with an accurate diagonal normal");
    }
    {
        auto corner=build(0.7f,0.7f);
        step(corner.scene);
        const auto stats=corner.scene.stats();
        const auto velocity=corner.scene.rigidBody(corner.sphere).velocity;
        require(stats.ccdHits==1 && stats.ccdBoxCornerHits==1 &&
                stats.ccdBoxFaceHits==0 && stats.ccdBoxEdgeHits==0 &&
                velocity.x>0 && velocity.y>100 && velocity.z>100,
                "Corner CCD must apply an impulse along the 3D corner normal");
        require(stats.ccdBudgetExhaustions==0 && stats.ccdDynamicHits==0,
                "Corner CCD unexpectedly exhausted the global event budget");
    }
    {
        // The old expanded AABB test incorrectly generated a collision
        // because y/z fall within +/- (half + sphere radius) individually.
        auto falseCorner=build(0.79f,0.79f);
        step(falseCorner.scene);
        const auto stats=falseCorner.scene.stats();
        const auto velocity=falseCorner.scene.rigidBody(falseCorner.sphere).velocity;
        require(stats.ccdHits==0 && stats.ccdBoxFaceHits==0 &&
                stats.ccdBoxCornerHits==0 &&
                near(velocity.x,240) &&
                near(falseCorner.scene.worldTransform(falseCorner.sphere)[3].x,2),
                "Exact rounded-box CCD must not bounce from empty expanded-AABB corner");

        auto discrete=build(0.7f,0.7f,false);
        step(discrete.scene);
        require(discrete.scene.stats().ccdHits==0 &&
                near(discrete.scene.rigidBody(discrete.sphere).velocity.x,240),
                "Non-opted-in spheres must retain discrete behavior");
    }
    return 0;
}
