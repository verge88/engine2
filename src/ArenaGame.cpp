#include "engine2/ArenaGame.hpp"

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <sstream>

namespace engine2 {
namespace {

constexpr glm::vec3 playerSpawn(0.0f,0.55f,4.15f);
constexpr std::array<glm::vec3,ArenaGame::collectibleCount> pickupSpawns{{
    {-3.7f,0.34f,-3.45f},
    { 3.65f,0.34f,-3.20f},
    {-3.25f,0.34f,0.20f},
    { 3.15f,0.34f,1.55f},
    { 0.00f,0.34f,2.25f}
}};
constexpr std::array<glm::vec3,ArenaGame::hazardCount> hazardSpawns{{
    {-2.55f,0.50f,-1.55f},
    { 2.45f,0.50f,-0.10f}
}};
constexpr std::array<glm::vec3,ArenaGame::hazardCount> hazardVelocities{{
    {4.6f,0.0f,3.25f},
    {-3.9f,0.0f,4.15f}
}};

glm::mat4 scaledAt(glm::vec3 position,glm::vec3 scale) {
    glm::mat4 result=glm::translate(glm::mat4(1.0f),position);
    return glm::scale(result,scale);
}

float xzDistance(glm::vec3 a,glm::vec3 b) {
    const glm::vec2 delta(a.x-b.x,a.z-b.z);
    return glm::length(delta);
}

Material material(glm::vec3 albedo,float metallic,float roughness,
                  glm::vec3 emissive=glm::vec3(0.0f)) {
    Material result;
    result.albedo=albedo;
    result.metallic=metallic;
    result.roughness=roughness;
    result.emissive=emissive;
    return result;
}

} // namespace

ArenaGame::ArenaGame(Scene& scene,
                     const Mesh* sphereMesh,
                     const Mesh* cubeMesh,
                     const Mesh* floorMesh) {
    build(scene,sphereMesh,cubeMesh,floorMesh);
    restart(scene);
}

void ArenaGame::build(Scene& scene,
                      const Mesh* sphereMesh,
                      const Mesh* cubeMesh,
                      const Mesh* floorMesh) {
    floor_=scene.createEntity("Orb Arena Floor");
    scene.setLocalTransform(
        floor_,scaledAt({0.0f,0.0f,0.0f},{6.3f,1.0f,6.3f}));
    Material floorMaterial=material({0.055f,0.075f,0.105f},0.18f,0.58f);
    floorMaterial.checker=true;
    scene.setRenderer(floor_,{floorMesh,floorMaterial});

    player_=scene.createEntity("Player Orb");
    scene.setRenderer(player_,{
        sphereMesh,
        material({0.08f,0.52f,0.98f},0.48f,0.20f,{0.015f,0.08f,0.22f})
    });
    RigidBodyComponent playerBody;
    playerBody.shape=ColliderShape::Sphere;
    playerBody.radius=0.55f;
    playerBody.mass=1.0f;
    playerBody.groundHeight=0.0f;
    playerBody.restitution=0.16f;
    playerBody.staticFriction=0.92f;
    playerBody.dynamicFriction=0.56f;
    playerBody.continuous=true;
    scene.setRigidBody(player_,playerBody);

    for (std::size_t i=0;i<collectibles_.size();++i) {
        collectibles_[i]=scene.createEntity(
            "Energy Core "+std::to_string(i+1));
        const float hue=static_cast<float>(i)/static_cast<float>(collectibles_.size());
        const glm::vec3 color{
            0.28f+0.42f*hue,
            0.92f-0.22f*hue,
            0.30f+0.55f*(1.0f-hue)
        };
        scene.setRenderer(collectibles_[i],{
            sphereMesh,material(color,0.18f,0.18f,color*1.8f)
        });
    }

    for (std::size_t i=0;i<hazards_.size();++i) {
        hazards_[i]=scene.createEntity(
            "Hunter Orb "+std::to_string(i+1));
        scene.setRenderer(hazards_[i],{
            sphereMesh,
            material({0.96f,0.07f,0.05f},0.28f,0.22f,{0.30f,0.01f,0.0f})
        });
        RigidBodyComponent body;
        body.shape=ColliderShape::Sphere;
        body.radius=0.50f;
        body.mass=0.9f;
        body.gravity=false;
        body.groundHeight=0.0f;
        body.restitution=0.96f;
        body.staticFriction=0.12f;
        body.dynamicFriction=0.06f;
        body.continuous=true;
        scene.setRigidBody(hazards_[i],body);
    }

    const std::array<glm::vec3,wallCount> wallPositions{{
        {-5.75f,1.0f,0.0f},
        { 5.75f,1.0f,0.0f},
        {0.0f,1.0f,-5.75f},
        {0.0f,1.0f, 5.75f}
    }};
    const std::array<glm::vec3,wallCount> wallScales{{
        {0.50f,2.0f,11.5f},
        {0.50f,2.0f,11.5f},
        {11.5f,2.0f,0.50f},
        {11.5f,2.0f,0.50f}
    }};
    const std::array<glm::vec3,wallCount> wallHalfExtents{{
        {0.25f,1.0f,5.75f},
        {0.25f,1.0f,5.75f},
        {5.75f,1.0f,0.25f},
        {5.75f,1.0f,0.25f}
    }};
    for (std::size_t i=0;i<walls_.size();++i) {
        walls_[i]=scene.createEntity("Arena Wall "+std::to_string(i+1));
        scene.setLocalTransform(walls_[i],scaledAt(wallPositions[i],wallScales[i]));
        scene.setRenderer(walls_[i],{
            cubeMesh,
            material({0.12f,0.18f,0.26f},0.62f,0.34f,{0.01f,0.02f,0.04f})
        });
        RigidBodyComponent body;
        body.shape=ColliderShape::Box;
        body.halfExtents=wallHalfExtents[i];
        body.isStatic=true;
        body.gravity=false;
        body.restitution=0.92f;
        body.staticFriction=0.32f;
        body.dynamicFriction=0.20f;
        scene.setRigidBody(walls_[i],body);
    }
}

void ArenaGame::resetTransforms(Scene& scene) {
    scene.setLocalTransform(player_,scaledAt(playerSpawn,glm::vec3(0.55f)));
    RigidBodyComponent playerBody=scene.rigidBody(player_);
    playerBody.velocity=glm::vec3(0.0f);
    playerBody.angularVelocity=glm::vec3(0.0f);
    scene.setRigidBody(player_,playerBody);

    for (std::size_t i=0;i<collectibles_.size();++i) {
        scene.setEnabled(collectibles_[i],true);
        scene.setLocalTransform(
            collectibles_[i],scaledAt(pickupSpawns[i],glm::vec3(0.28f)));
    }

    for (std::size_t i=0;i<hazards_.size();++i) {
        scene.setLocalTransform(
            hazards_[i],scaledAt(hazardSpawns[i],glm::vec3(0.50f)));
        RigidBodyComponent body=scene.rigidBody(hazards_[i]);
        body.velocity=hazardVelocities[i];
        body.angularVelocity=glm::vec3(0.0f);
        scene.setRigidBody(hazards_[i],body);
    }
}

void ArenaGame::restart(Scene& scene) {
    stats_=ArenaGameStats{};
    damageCooldown_=0.0f;
    resetTransforms(scene);
}

void ArenaGame::applyInput(
    Scene& scene,const ArenaGameInput& input,float deltaSeconds) {

    if (input.restart && !restartHeld_) restart(scene);
    restartHeld_=input.restart;
    if (stats_.state!=ArenaGameState::Playing) return;

    glm::vec2 direction(input.moveX,input.moveZ);
    const float length=glm::length(direction);
    if (length>1.0f) direction/=length;

    const float maximumSpeed=input.boost ? 8.6f : 5.8f;
    const glm::vec2 target=direction*maximumSpeed;
    RigidBodyComponent body=scene.rigidBody(player_);
    glm::vec2 current(body.velocity.x,body.velocity.z);
    const glm::vec2 delta=target-current;
    const float deltaLength=glm::length(delta);
    const float maxChange=30.0f*std::clamp(deltaSeconds,0.0f,0.10f);
    if (deltaLength>maxChange && deltaLength>1.0e-6f)
        current+=delta*(maxChange/deltaLength);
    else
        current=target;
    body.velocity.x=current.x;
    body.velocity.z=current.y;
    scene.setRigidBodyVelocity(player_,body.velocity);
}

void ArenaGame::afterPhysics(Scene& scene,float deltaSeconds) {
    const float dt=std::clamp(deltaSeconds,0.0f,0.25f);
    stats_.elapsedSeconds+=dt;
    damageCooldown_=std::max(0.0f,damageCooldown_-dt);

    for (std::size_t i=0;i<collectibles_.size();++i) {
        if (!scene.enabled(collectibles_[i])) continue;
        const float phase=stats_.elapsedSeconds*2.2f+static_cast<float>(i)*1.17f;
        glm::vec3 position=pickupSpawns[i];
        position.y+=0.07f*std::sin(phase);
        glm::mat4 transform=glm::translate(glm::mat4(1.0f),position);
        transform=glm::rotate(
            transform,phase*0.65f,glm::normalize(glm::vec3(0.3f,1.0f,0.2f)));
        transform=glm::scale(transform,glm::vec3(0.28f));
        scene.setLocalTransform(collectibles_[i],transform);
    }

    if (stats_.state!=ArenaGameState::Playing) return;
    const glm::vec3 playerPosition(scene.worldTransform(player_)[3]);

    for (Entity collectible:collectibles_) {
        if (!scene.enabled(collectible)) continue;
        const glm::vec3 position(scene.worldTransform(collectible)[3]);
        if (xzDistance(playerPosition,position)<=0.82f) {
            scene.setEnabled(collectible,false);
            ++stats_.score;
        }
    }
    if (stats_.score>=stats_.targetScore) {
        stats_.state=ArenaGameState::Won;
        scene.setRigidBodyVelocity(player_,glm::vec3(0.0f));
        return;
    }

    if (damageCooldown_<=0.0f) {
        for (Entity hazard:hazards_) {
            const glm::vec3 hazardPosition(scene.worldTransform(hazard)[3]);
            if (glm::distance(playerPosition,hazardPosition)<=1.18f) {
                --stats_.lives;
                damageCooldown_=0.95f;
                if (stats_.lives<=0) {
                    stats_.lives=0;
                    stats_.state=ArenaGameState::Lost;
                    scene.setRigidBodyVelocity(player_,glm::vec3(0.0f));
                } else {
                    scene.setLocalTransform(
                        player_,scaledAt(playerSpawn,glm::vec3(0.55f)));
                    RigidBodyComponent body=scene.rigidBody(player_);
                    body.velocity=glm::vec3(0.0f);
                    body.angularVelocity=glm::vec3(0.0f);
                    scene.setRigidBody(player_,body);
                }
                break;
            }
        }
    }
}

std::string ArenaGame::statusText() const {
    std::ostringstream out;
    if (stats_.state==ArenaGameState::Won)
        out << "YOU WIN";
    else if (stats_.state==ArenaGameState::Lost)
        out << "GAME OVER";
    else
        out << "ORB ARENA";
    out << " | cores " << stats_.score << '/' << stats_.targetScore
        << " | lives " << stats_.lives;
    if (stats_.state==ArenaGameState::Playing)
        out << " | WASD move, Shift boost, R restart";
    else
        out << " | R restart";
    return out.str();
}

} // namespace engine2
