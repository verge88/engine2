#include "engine2/CollisionMath.hpp"
#include "engine2/RigidBodyMath.hpp"
#include "engine2/Scene.hpp"

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <stdexcept>

namespace {
void require(bool okay,const char* message) {
    if (!okay) throw std::runtime_error(message);
}
bool near(float a,float b,float epsilon=0.004f) {
    return std::abs(a-b)<=epsilon;
}
glm::mat4 at(glm::vec3 position) {
    return glm::translate(glm::mat4(1.0f),position);
}
}

int main() {
    using namespace engine2;
    const glm::vec3 up(0.0f,1.0f,0.0f);

    // A horizontal box resting on y=0 exposes its complete bottom face:
    // four vertices, all on the plane.
    {
        const OrientedBox box{
            glm::vec3(0.0f,0.5f,0.0f),
            glm::mat3(1.0f),
            glm::vec3(1.0f,0.5f,0.75f)
        };
        const ContactManifold manifold=
            orientedBoxPlaneManifold(box,up,0.0f);
        require(manifold.colliding && manifold.pointCount==4,
                "Horizontal OBB/ground contact should have four points");
        require(glm::dot(manifold.normal,-up)>0.999f,
                "Ground manifold normal should point from body to plane");
        for (std::size_t i=0;i<manifold.pointCount;++i) {
            require(near(manifold.points[i].point.y,0.0f),
                    "Ground face contact point is not on the plane");
            require(near(manifold.points[i].penetration,0.0f),
                    "Resting ground face should not report penetration");
        }
    }

    // A 45-degree roll puts an entire box edge on the plane, so exactly two
    // support vertices should survive the support-feature tolerance.
    {
        const glm::mat3 rotation=rigidBodyRotation(glm::rotate(
            glm::mat4(1.0f),glm::quarter_pi<float>(),glm::vec3(0,0,1)));
        const glm::vec3 half(0.5f,0.5f,0.8f);
        const float height=orientedBoxProjectedRadius(
            {glm::vec3(0.0f),rotation,half},up);
        const OrientedBox box{
            glm::vec3(0.0f,height,0.0f),rotation,half
        };
        const ContactManifold manifold=
            orientedBoxPlaneManifold(box,up,0.0f);
        require(manifold.colliding && manifold.pointCount==2,
                "Rolled OBB should contact the ground along one edge");
    }

    // A generic two-axis tilt has one unique lowest corner.
    {
        glm::mat4 transform=glm::rotate(
            glm::mat4(1.0f),glm::radians(19.0f),glm::vec3(1,0,0));
        transform=glm::rotate(
            transform,glm::radians(13.0f),glm::vec3(0,0,1));
        const glm::mat3 rotation=rigidBodyRotation(transform);
        const glm::vec3 half(0.6f,0.4f,0.8f);
        const float height=orientedBoxProjectedRadius(
            {glm::vec3(0.0f),rotation,half},up);
        const ContactManifold manifold=orientedBoxPlaneManifold(
            {glm::vec3(0.0f,height,0.0f),rotation,half},up,0.0f);
        require(manifold.colliding && manifold.pointCount==1,
                "Generic tilted OBB should initially contact at one corner");
    }

    // The generic collider/plane helper keeps sphere ground contacts
    // single-point and uses the same body->plane normal convention.
    {
        const ContactManifold sphere=colliderPlaneManifoldOriented(
            {ColliderShape::Sphere,0.5f,glm::vec3(0.5f)},
            glm::vec3(0.0f,0.5f,0.0f),glm::mat3(1.0f),up,0.0f);
        require(sphere.colliding && sphere.pointCount==1,
                "Sphere/ground helper should remain single-point");
        require(glm::dot(sphere.normal,-up)>0.999f,
                "Sphere/ground normal convention changed");
    }

    // Runtime regression: a flat box should settle on the implicit ground
    // plane with a four-point persistent manifold rather than rocking around
    // one support point.
    {
        Scene scene;
        const Entity box=scene.createEntity("grounded box");
        scene.setLocalTransform(box,at({0.0f,0.5f,0.0f}));

        RigidBodyComponent body;
        body.shape=ColliderShape::Box;
        body.halfExtents=glm::vec3(0.75f,0.5f,0.65f);
        body.mass=1.5f;
        body.gravity=true;
        body.groundHeight=0.0f;
        body.restitution=0.0f;
        body.staticFriction=0.9f;
        body.dynamicFriction=0.6f;
        scene.setRigidBody(box,body);

        for (int i=0;i<180;++i)
            (void)scene.advance(Scene::fixedDeltaSeconds);

        const SceneStats stats=scene.stats();
        require(stats.multiPointManifolds>0,
                "Grounded box never generated a multi-point manifold");
        require(stats.manifoldPoints>=4,
                "Ground manifold points were not included in telemetry");
        require(stats.contactWarmStarts>100,
                "Ground manifold did not persistently warm start");
        require(stats.persistentContacts>=1,
                "Ground manifold was not retained in the contact cache");
        const glm::vec3 position(scene.worldTransform(box)[3]);
        require(std::abs(position.y-0.5f)<0.01f,
                "Ground manifold did not preserve box support height");
        require(std::abs(scene.rigidBody(box).velocity.y)<0.03f,
                "Grounded box retained vertical jitter");
        require(glm::length(scene.rigidBody(box).angularVelocity)<0.05f,
                "Symmetric ground face accumulated angular jitter");
    }

    // Symmetric multi-point friction should slow translation without the large
    // artificial spin produced by a single off-center floor support point.
    {
        Scene scene;
        const Entity box=scene.createEntity("sliding box");
        scene.setLocalTransform(box,at({0.0f,0.5f,0.0f}));

        RigidBodyComponent body;
        body.shape=ColliderShape::Box;
        body.halfExtents=glm::vec3(0.7f,0.5f,0.6f);
        body.mass=1.0f;
        body.gravity=true;
        body.groundHeight=0.0f;
        body.restitution=0.0f;
        body.staticFriction=0.9f;
        body.dynamicFriction=0.55f;
        body.velocity=glm::vec3(2.0f,0.0f,0.0f);
        scene.setRigidBody(box,body);

        for (int i=0;i<90;++i)
            (void)scene.advance(Scene::fixedDeltaSeconds);

        const RigidBodyComponent& settled=scene.rigidBody(box);
        require(std::abs(settled.velocity.x)<0.45f,
                "Multi-point ground friction did not slow the sliding box");
        require(glm::length(settled.angularVelocity)<0.35f,
                "Symmetric ground friction generated excessive box spin");
    }

    return 0;
}
