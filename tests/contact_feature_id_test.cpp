#include "engine2/CollisionMath.hpp"
#include "engine2/RigidBodyMath.hpp"
#include "engine2/Scene.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {
void require(bool okay,const char* message) {
    if (!okay) throw std::runtime_error(message);
}

std::vector<std::uint32_t> featureIds(
    const engine2::ContactManifold& manifold) {
    std::vector<std::uint32_t> ids;
    ids.reserve(manifold.pointCount);
    for (std::size_t i=0;i<manifold.pointCount;++i)
        ids.push_back(manifold.points[i].featureId);
    std::sort(ids.begin(),ids.end());
    return ids;
}

void requireUniqueNonzero(
    const engine2::ContactManifold& manifold,const char* message) {
    const auto ids=featureIds(manifold);
    if (ids.empty() || ids.front()==0)
        throw std::runtime_error(message);
    if (std::adjacent_find(ids.begin(),ids.end())!=ids.end())
        throw std::runtime_error(message);
}
}

int main() {
    using namespace engine2;

    // Four face contacts must carry four distinct local-topology IDs.
    const OrientedBox baseA{
        glm::vec3(0.0f),glm::mat3(1.0f),glm::vec3(1.0f)
    };
    const OrientedBox baseB{
        glm::vec3(0.0f,1.8f,0.0f),
        glm::mat3(1.0f),glm::vec3(1.0f)
    };
    const ContactManifold aligned=orientedBoxManifold(baseA,baseB);
    require(aligned.pointCount==4,
            "Aligned face manifold should expose four feature IDs");
    requireUniqueNonzero(
        aligned,"Aligned face feature IDs must be unique and nonzero");

    // World translation must not change local topological identity.
    const glm::vec3 translation(7.5f,-3.0f,4.25f);
    ContactManifold translated=orientedBoxManifold(
        {baseA.center+translation,baseA.rotation,baseA.halfExtents},
        {baseB.center+translation,baseB.rotation,baseB.halfExtents});
    require(featureIds(translated)==featureIds(aligned),
            "OBB feature IDs changed under common world translation");

    // Common world rotation changes every world contact coordinate while the
    // local face/vertex identities remain exactly the same.
    const glm::mat4 turn=glm::rotate(
        glm::mat4(1.0f),glm::radians(31.0f),glm::vec3(0,0,1));
    const glm::mat3 worldRotation=rigidBodyRotation(turn);
    const ContactManifold rotated=orientedBoxManifold(
        {worldRotation*baseA.center,worldRotation*baseA.rotation,
         baseA.halfExtents},
        {worldRotation*baseB.center,worldRotation*baseB.rotation,
         baseB.halfExtents});
    require(featureIds(rotated)==featureIds(aligned),
            "OBB feature IDs changed under common world rotation");

    // A smaller incident face entirely inside the reference face uses the
    // incident vertices as stable identity. Sliding inside that same face
    // should preserve the same four IDs.
    {
        const OrientedBox reference{
            glm::vec3(0.0f),glm::mat3(1.0f),
            glm::vec3(2.0f,0.5f,2.0f)
        };
        const glm::mat3 incidentRotation=rigidBodyRotation(glm::rotate(
            glm::mat4(1.0f),glm::radians(20.0f),glm::vec3(0,1,0)));
        const OrientedBox incident{
            glm::vec3(0.0f,0.9f,0.0f),incidentRotation,
            glm::vec3(0.65f,0.5f,0.55f)
        };
        const ContactManifold first=
            orientedBoxManifold(reference,incident);
        OrientedBox shifted=incident;
        shifted.center.x+=0.18f;
        shifted.center.z-=0.12f;
        const ContactManifold second=
            orientedBoxManifold(reference,shifted);
        require(first.pointCount==4 && second.pointCount==4,
                "Contained face slide changed manifold topology unexpectedly");
        requireUniqueNonzero(
            first,"Contained OBB face features must be unique");
        require(featureIds(first)==featureIds(second),
                "Contained OBB face feature IDs changed while sliding");
    }

    // Ground IDs derive from the box's local support vertices, so horizontal
    // translation and yaw do not relabel the four bottom corners.
    {
        const OrientedBox groundBox{
            glm::vec3(0.0f,0.5f,0.0f),glm::mat3(1.0f),
            glm::vec3(0.8f,0.5f,0.6f)
        };
        const ContactManifold first=orientedBoxPlaneManifold(
            groundBox,glm::vec3(0,1,0),0.0f);
        const glm::mat3 yaw=rigidBodyRotation(glm::rotate(
            glm::mat4(1.0f),glm::radians(37.0f),glm::vec3(0,1,0)));
        const OrientedBox moved{
            glm::vec3(3.0f,0.5f,-2.0f),yaw,
            groundBox.halfExtents
        };
        const ContactManifold second=orientedBoxPlaneManifold(
            moved,glm::vec3(0,1,0),0.0f);
        require(first.pointCount==4 && second.pointCount==4,
                "Horizontal OBB/ground topology should remain four points");
        requireUniqueNonzero(
            first,"Ground support feature IDs must be unique and nonzero");
        require(featureIds(first)==featureIds(second),
                "Ground support feature IDs changed under translation/yaw");
    }

    // Sphere contacts intentionally have no box-topology feature ID and keep
    // using the proximity fallback path.
    {
        const ContactManifold sphere=colliderPlaneManifoldOriented(
            {ColliderShape::Sphere,0.5f,glm::vec3(0.5f)},
            glm::vec3(0.0f,0.5f,0.0f),glm::mat3(1.0f),
            glm::vec3(0,1,0),0.0f);
        require(sphere.pointCount==1 && sphere.points[0].featureId==0,
                "Sphere/ground should not invent an OBB feature ID");
    }

    // Runtime proof: a resting box should reuse cached ground points primarily
    // through stable feature IDs across many fixed steps.
    {
        Scene scene;
        const Entity box=scene.createEntity("feature-id grounded box");
        scene.setLocalTransform(
            box,glm::translate(
                glm::mat4(1.0f),glm::vec3(0.0f,0.5f,0.0f)));

        RigidBodyComponent body;
        body.shape=ColliderShape::Box;
        body.halfExtents=glm::vec3(0.75f,0.5f,0.65f);
        body.mass=1.0f;
        body.gravity=true;
        body.groundHeight=0.0f;
        body.restitution=0.0f;
        body.staticFriction=0.8f;
        body.dynamicFriction=0.5f;
        scene.setRigidBody(box,body);

        for (int i=0;i<120;++i)
            (void)scene.advance(Scene::fixedDeltaSeconds);

        const SceneStats stats=scene.stats();
        require(stats.contactFeatureWarmStarts>100,
                "Resting OBB/ground contacts did not use feature-ID warm starts");
        require(stats.contactFeatureWarmStarts<=stats.contactWarmStarts,
                "Feature warm-start telemetry exceeded total warm starts");
    }

    return 0;
}
