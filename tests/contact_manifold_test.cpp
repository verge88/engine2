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
}

int main() {
    using namespace engine2;

    // Two aligned faces overlap by 0.2. The clipped incident quad should
    // survive as four distinct corner contacts instead of one midpoint.
    {
        const OrientedBox a{
            glm::vec3(0.0f),glm::mat3(1.0f),glm::vec3(1.0f)
        };
        const OrientedBox b{
            glm::vec3(0.0f,1.8f,0.0f),
            glm::mat3(1.0f),glm::vec3(1.0f)
        };
        const ContactManifold manifold=orientedBoxManifold(a,b);
        require(manifold.colliding,
                "Aligned OBB faces should collide");
        require(manifold.pointCount==4,
                "Aligned OBB face contact should produce four points");
        require(glm::dot(manifold.normal,glm::vec3(0,1,0))>0.999f,
                "Aligned face manifold normal is incorrect");
        require(near(manifold.penetration,0.2f),
                "Aligned face manifold penetration is incorrect");
        for (std::size_t i=0;i<manifold.pointCount;++i) {
            require(near(manifold.points[i].penetration,0.2f),
                    "Clipped face point penetration is incorrect");
            require(near(manifold.points[i].point.y,0.9f),
                    "Face contact point should lie midway between surfaces");
        }
    }

    // A smaller rotated incident face remains completely inside the large
    // reference face. Clipping should preserve all four rotated corners.
    {
        const OrientedBox reference{
            glm::vec3(0.0f),glm::mat3(1.0f),
            glm::vec3(2.0f,0.5f,2.0f)
        };
        glm::mat4 transform=glm::rotate(
            glm::mat4(1.0f),glm::radians(25.0f),glm::vec3(0,1,0));
        const OrientedBox incident{
            glm::vec3(0.0f,0.9f,0.0f),
            rigidBodyRotation(transform),
            glm::vec3(0.75f,0.5f,0.55f)
        };
        const ContactManifold manifold=
            orientedBoxManifold(reference,incident);
        require(manifold.colliding && manifold.pointCount==4,
                "Rotated contained face should retain four clipped contacts");
        require(glm::dot(manifold.normal,glm::vec3(0,1,0))>0.999f,
                "Rotated face manifold selected the wrong separating axis");
    }

    // Partial face overlap exercises Sutherland-Hodgman clipping against the
    // reference side planes. Every retained point must stay inside the face.
    {
        const OrientedBox a{
            glm::vec3(0.0f),glm::mat3(1.0f),
            glm::vec3(1.0f,0.5f,1.0f)
        };
        const OrientedBox b{
            glm::vec3(1.35f,0.9f,0.0f),glm::mat3(1.0f),
            glm::vec3(0.75f,0.5f,0.75f)
        };
        const ContactManifold manifold=orientedBoxManifold(a,b);
        require(manifold.colliding && manifold.pointCount>=2 &&
                manifold.pointCount<=4,
                "Partial face overlap should create a bounded clipped manifold");
        for (std::size_t i=0;i<manifold.pointCount;++i) {
            const glm::vec3 point=manifold.points[i].point;
            require(point.x<=1.01f && point.x>=-1.01f &&
                    point.z<=1.01f && point.z>=-1.01f,
                    "Clipped contact escaped the reference face side planes");
        }
    }

    // A face resting on a static box should remain stable and warm-start all
    // manifold points across fixed steps. This is the regression the old
    // representative single-point solver could not model as a true support
    // polygon.
    {
        Scene scene;
        const Entity base=scene.createEntity("static base");
        glm::mat4 baseTransform=glm::translate(
            glm::mat4(1.0f),glm::vec3(0.0f,0.0f,0.0f));
        scene.setLocalTransform(base,baseTransform);
        RigidBodyComponent baseBody;
        baseBody.shape=ColliderShape::Box;
        baseBody.halfExtents=glm::vec3(1.5f,0.5f,1.5f);
        baseBody.isStatic=true;
        baseBody.gravity=false;
        baseBody.restitution=0.0f;
        baseBody.staticFriction=0.8f;
        baseBody.dynamicFriction=0.5f;
        scene.setRigidBody(base,baseBody);

        const Entity top=scene.createEntity("resting box");
        glm::mat4 topTransform=glm::translate(
            glm::mat4(1.0f),glm::vec3(0.0f,1.0f,0.0f));
        scene.setLocalTransform(top,topTransform);
        RigidBodyComponent topBody;
        topBody.shape=ColliderShape::Box;
        topBody.halfExtents=glm::vec3(0.75f,0.5f,0.75f);
        topBody.mass=1.0f;
        topBody.gravity=true;
        topBody.groundHeight=-100.0f;
        topBody.restitution=0.0f;
        topBody.staticFriction=0.8f;
        topBody.dynamicFriction=0.5f;
        scene.setRigidBody(top,topBody);

        for (int i=0;i<120;++i)
            (void)scene.advance(Scene::fixedDeltaSeconds);

        const SceneStats stats=scene.stats();
        require(stats.multiPointManifolds>0,
                "Resting OBB face never generated a multi-point manifold");
        require(stats.manifoldPoints>=4,
                "Manifold telemetry did not count clipped contact points");
        require(stats.contactWarmStarts>0,
                "Multi-point resting contact did not warm start");
        const glm::vec3 position(scene.worldTransform(top)[3]);
        require(std::abs(position.y-1.0f)<0.03f,
                "Four-point OBB support did not keep the box at rest height");
        require(glm::length(scene.rigidBody(top).angularVelocity)<0.08f,
                "Symmetric resting face accumulated excessive angular jitter");
    }

    return 0;
}
