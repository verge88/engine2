#include "engine2/Scene.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <stdexcept>

namespace {
void require(bool okay,const char* message) {
    if (!okay) throw std::runtime_error(message);
}
glm::mat4 translation(float x,float y=2.0f,float z=0.0f) {
    return glm::translate(glm::mat4(1.0f),glm::vec3(x,y,z));
}
float x(const engine2::Scene& scene,engine2::Entity entity) {
    return scene.worldTransform(entity)[3][0];
}
void step(engine2::Scene& scene) {
    (void)scene.advance(engine2::Scene::fixedDeltaSeconds);
}
engine2::Entity sphere(
    engine2::Scene& scene,float origin,float speed,
    bool ccd=true,float mass=1.0f) {
    using namespace engine2;
    const auto entity=scene.createEntity("CCD sphere");
    scene.setLocalTransform(entity,translation(origin));
    RigidBodyComponent body;
    body.shape=ColliderShape::Sphere;
    body.radius=0.2f;
    body.gravity=false;
    body.groundHeight=-100.0f;
    body.mass=mass;
    body.restitution=1.0f;
    body.continuous=ccd;
    body.velocity={speed,0,0};
    scene.setRigidBody(entity,body);
    return entity;
}
engine2::Entity wall(engine2::Scene& scene,float origin) {
    using namespace engine2;
    const auto entity=scene.createEntity("CCD wall");
    scene.setLocalTransform(entity,translation(origin));
    RigidBodyComponent box;
    box.shape=ColliderShape::Box;
    box.halfExtents={0.1f,0.5f,0.5f};
    box.gravity=false;
    box.isStatic=true;
    box.restitution=1.0f;
    scene.setRigidBody(entity,box);
    return entity;
}
bool close(float a,float b,float tolerance=0.005f) {
    return std::abs(a-b)<tolerance;
}
}

int main() {
    using namespace engine2;

    // Crossed trajectories: discrete simulation tunnels, CCD must reverse
    // both sphere velocities at their actual global time of impact.
    {
        Scene discrete;
        const Entity a=sphere(discrete,-2,300,false);
        const Entity b=sphere(discrete,2,-300,false);
        step(discrete);
        require(x(discrete,a)>x(discrete,b) &&
                discrete.stats().ccdHits==0,
                "Legacy discrete spheres should cross at high speeds");

        Scene continuous;
        const Entity first=sphere(continuous,-2,300);
        const Entity second=sphere(continuous,2,-300);
        step(continuous);
        const auto telemetry=continuous.stats();
        require(x(continuous,first)<-3.3f &&
                x(continuous,second)>3.3f &&
                close(continuous.rigidBody(first).velocity.x,-300.0f) &&
                close(continuous.rigidBody(second).velocity.x,300.0f),
                "Moving/moving CCD must stop dynamic sphere tunnelling");
        require(telemetry.ccdHits==1 && telemetry.ccdDynamicHits==1 &&
                telemetry.ccdBudgetExhaustions==0 &&
                telemetry.broadphasePairs>0,
                "Moving sphere CCD counters or broadphase missing");
    }

    // Both actors explicitly opt into continuous detection, preserving the
    // previous discrete semantics of unflagged bodies.
    {
        Scene onlyOne;
        const Entity first=sphere(onlyOne,-2,300,true);
        const Entity second=sphere(onlyOne,2,-300,false);
        step(onlyOne);
        require(onlyOne.stats().ccdDynamicHits==0 &&
                x(onlyOne,first)>x(onlyOne,second),
                "Both moving bodies must opt in to continuous CCD");
    }

    // Unequal masses: analytic 1-D momentum and energy conservation.
    {
        Scene unequal;
        const Entity light=sphere(unequal,-2,300,true,1);
        const Entity heavy=sphere(unequal,2,-300,true,3);
        step(unequal);
        require(close(unequal.rigidBody(light).velocity.x,-600.0f) &&
                close(unequal.rigidBody(heavy).velocity.x,0.0f) &&
                unequal.stats().ccdDynamicHits==1,
                "Unequal-mass dynamic CCD impulse is incorrect");
    }

    // Sphere enters a corridor and must hit BOTH walls in ONE fixed step.
    // The old one-impact CCD crossed the left wall after rebounding right.
    {
        Scene corridor;
        wall(corridor,1.5f);
        wall(corridor,-1.5f);
        const Entity projectile=sphere(corridor,0,240);
        step(corridor);
        require(corridor.stats().ccdHits==2 &&
                corridor.stats().ccdDynamicHits==0 &&
                corridor.stats().ccdBudgetExhaustions==0 &&
                close(corridor.rigidBody(projectile).velocity.x,240.0f) &&
                close(x(corridor,projectile),-0.8f,0.02f),
                "Multi-impact CCD failed to bounce between two static walls");
    }

    // Event ordering is GLOBAL, not a per-body independent correction:
    // A hits B then B hits C in the remaining time of the same fixed step.
    {
        Scene chain;
        const Entity a=sphere(chain,-4,600);
        const Entity b=sphere(chain,0,0);
        const Entity c=sphere(chain,2,0);
        step(chain);
        require(chain.stats().ccdHits==2 &&
                chain.stats().ccdDynamicHits==2 &&
                close(chain.rigidBody(a).velocity.x,0) &&
                close(chain.rigidBody(b).velocity.x,0) &&
                close(chain.rigidBody(c).velocity.x,600),
                "Global CCD event ordering must transfer momentum A->B->C");
    }

    // Limit event work in a pathological high-speed corridor and disclose
    // the bounded fallback instead of hanging the physics update.
    {
        Scene rapid;
        wall(rapid,-1.5f);
        wall(rapid,1.5f);
        const Entity moving=sphere(rapid,0,10000);
        step(rapid);
        require(rapid.stats().ccdHits==12 &&
                rapid.stats().ccdBudgetExhaustions==1 &&
                std::isfinite(x(rapid,moving)),
                "CCD event budget must cap per-step work and report exhaustion");
    }

    return 0;
}
