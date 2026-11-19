#pragma once

#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace engine2 {

struct ContactMaterial {
    float restitution = 0.35f;
    float staticFriction = 0.0f;
    float dynamicFriction = 0.0f;
};

struct ContactImpulseState {
    float accumulatedNormal = 0.0f;
    glm::vec3 accumulatedTangent{0.0f};
    float reboundTarget = 0.0f;
    bool initialized = false;
    bool persistentEligible = false;
};

inline glm::vec3 contactPointVelocity(
    glm::vec3 velocity, glm::vec3 angularVelocity, glm::vec3 leverArm) {
    return velocity + glm::cross(angularVelocity,leverArm);
}

inline float directionalInverseMassTensor(
    float invMassA,float invMassB,
    const glm::mat3& invInertiaA,const glm::mat3& invInertiaB,
    glm::vec3 leverArmA,glm::vec3 leverArmB,glm::vec3 direction) {
    const glm::vec3 angularA=glm::cross(leverArmA,direction);
    const glm::vec3 angularB=glm::cross(leverArmB,direction);
    return invMassA+invMassB+
        glm::dot(angularA,invInertiaA*angularA)+
        glm::dot(angularB,invInertiaB*angularB);
}

inline float directionalInverseMass(
    float invMassA, float invMassB,
    float invInertiaA, float invInertiaB,
    glm::vec3 leverArmA, glm::vec3 leverArmB,
    glm::vec3 direction) {
    return directionalInverseMassTensor(
        invMassA,invMassB,
        glm::mat3(invInertiaA),glm::mat3(invInertiaB),
        leverArmA,leverArmB,direction);
}

inline void applyContactImpulseTensor(
    glm::vec3& velocityA,glm::vec3& angularVelocityA,
    glm::vec3& velocityB,glm::vec3& angularVelocityB,
    float invMassA,float invMassB,
    const glm::mat3& invInertiaA,const glm::mat3& invInertiaB,
    glm::vec3 leverArmA,glm::vec3 leverArmB,glm::vec3 impulse) {

    velocityA-=impulse*invMassA;
    velocityB+=impulse*invMassB;
    angularVelocityA-=invInertiaA*glm::cross(leverArmA,impulse);
    angularVelocityB+=invInertiaB*glm::cross(leverArmB,impulse);
}

inline void applyContactImpulse(
    glm::vec3& velocityA, glm::vec3& angularVelocityA,
    glm::vec3& velocityB, glm::vec3& angularVelocityB,
    float invMassA, float invMassB,
    float invInertiaA, float invInertiaB,
    glm::vec3 leverArmA, glm::vec3 leverArmB,
    glm::vec3 impulse) {
    applyContactImpulseTensor(
        velocityA,angularVelocityA,velocityB,angularVelocityB,
        invMassA,invMassB,
        glm::mat3(invInertiaA),glm::mat3(invInertiaB),
        leverArmA,leverArmB,impulse);
}

inline void warmStartContactImpulseTensor(
    glm::vec3& velocityA,glm::vec3& angularVelocityA,
    glm::vec3& velocityB,glm::vec3& angularVelocityB,
    float invMassA,float invMassB,
    const glm::mat3& invInertiaA,const glm::mat3& invInertiaB,
    glm::vec3 leverArmA,glm::vec3 leverArmB,
    glm::vec3 normal,ContactImpulseState& state) {

    if (!state.initialized || state.accumulatedNormal<=0.0f) return;
    const glm::vec3 tangent=
        state.accumulatedTangent-
        glm::dot(state.accumulatedTangent,normal)*normal;
    state.accumulatedTangent=tangent;
    const glm::vec3 impulse=state.accumulatedNormal*normal+tangent;
    applyContactImpulseTensor(
        velocityA,angularVelocityA,velocityB,angularVelocityB,
        invMassA,invMassB,invInertiaA,invInertiaB,
        leverArmA,leverArmB,impulse);
}

inline void warmStartContactImpulseAngular(
    glm::vec3& velocityA, glm::vec3& angularVelocityA,
    glm::vec3& velocityB, glm::vec3& angularVelocityB,
    float invMassA, float invMassB,
    float invInertiaA, float invInertiaB,
    glm::vec3 leverArmA, glm::vec3 leverArmB,
    glm::vec3 normal, ContactImpulseState& state) {
    warmStartContactImpulseTensor(
        velocityA,angularVelocityA,velocityB,angularVelocityB,
        invMassA,invMassB,
        glm::mat3(invInertiaA),glm::mat3(invInertiaB),
        leverArmA,leverArmB,normal,state);
}

inline void solveContactImpulseTensor(
    glm::vec3& velocityA,glm::vec3& angularVelocityA,
    glm::vec3& velocityB,glm::vec3& angularVelocityB,
    float invMassA,float invMassB,
    const glm::mat3& invInertiaA,const glm::mat3& invInertiaB,
    glm::vec3 leverArmA,glm::vec3 leverArmB,glm::vec3 normal,
    const ContactMaterial& materialA,const ContactMaterial& materialB,
    ContactImpulseState& state) {

    const float normalInvMass=directionalInverseMassTensor(
        invMassA,invMassB,invInertiaA,invInertiaB,
        leverArmA,leverArmB,normal);
    if (normalInvMass<=0.0f) return;

    const glm::vec3 pointA=contactPointVelocity(
        velocityA,angularVelocityA,leverArmA);
    const glm::vec3 pointB=contactPointVelocity(
        velocityB,angularVelocityB,leverArmB);
    if (!state.initialized) {
        const float initial=glm::dot(pointB-pointA,normal);
        const float restitution=std::min(
            materialA.restitution,materialB.restitution);
        state.reboundTarget=initial<-0.5f ? -initial*restitution : 0.0f;
        state.persistentEligible=initial>=-0.5f;
        state.initialized=true;
    }

    const glm::vec3 currentA=contactPointVelocity(
        velocityA,angularVelocityA,leverArmA);
    const glm::vec3 currentB=contactPointVelocity(
        velocityB,angularVelocityB,leverArmB);
    const float closing=glm::dot(currentB-currentA,normal);
    const float wanted=(state.reboundTarget-closing)/normalInvMass;
    const float newNormal=std::max(state.accumulatedNormal+wanted,0.0f);
    const float appliedNormal=newNormal-state.accumulatedNormal;
    state.accumulatedNormal=newNormal;
    applyContactImpulseTensor(
        velocityA,angularVelocityA,velocityB,angularVelocityB,
        invMassA,invMassB,invInertiaA,invInertiaB,
        leverArmA,leverArmB,appliedNormal*normal);

    const float staticMu=std::sqrt(
        materialA.staticFriction*materialB.staticFriction);
    const float dynamicMu=std::sqrt(
        materialA.dynamicFriction*materialB.dynamicFriction);
    if (staticMu==0.0f && dynamicMu==0.0f) return;

    const glm::vec3 frictionPointA=contactPointVelocity(
        velocityA,angularVelocityA,leverArmA);
    const glm::vec3 frictionPointB=contactPointVelocity(
        velocityB,angularVelocityB,leverArmB);
    const glm::vec3 relative=frictionPointB-frictionPointA;
    const glm::vec3 tangentSpeed=
        relative-glm::dot(relative,normal)*normal;
    const float tangentMagnitude=glm::length(tangentSpeed);
    glm::vec3 proposed=state.accumulatedTangent;
    if (tangentMagnitude>=1.0e-8f) {
        const glm::vec3 tangentDirection=tangentSpeed/tangentMagnitude;
        const float tangentInvMass=directionalInverseMassTensor(
            invMassA,invMassB,invInertiaA,invInertiaB,
            leverArmA,leverArmB,tangentDirection);
        if (tangentInvMass<=0.0f) return;
        proposed-=tangentSpeed/tangentInvMass;
    }

    const float magnitude=glm::length(proposed);
    const float staticLimit=staticMu*newNormal;
    if (magnitude>staticLimit && magnitude>1.0e-8f)
        proposed*=dynamicMu*newNormal/magnitude;
    const glm::vec3 frictionImpulse=proposed-state.accumulatedTangent;
    state.accumulatedTangent=proposed;
    applyContactImpulseTensor(
        velocityA,angularVelocityA,velocityB,angularVelocityB,
        invMassA,invMassB,invInertiaA,invInertiaB,
        leverArmA,leverArmB,frictionImpulse);
}

inline void solveContactImpulseAngular(
    glm::vec3& velocityA, glm::vec3& angularVelocityA,
    glm::vec3& velocityB, glm::vec3& angularVelocityB,
    float invMassA, float invMassB,
    float invInertiaA, float invInertiaB,
    glm::vec3 leverArmA, glm::vec3 leverArmB,
    glm::vec3 normal,
    const ContactMaterial& materialA, const ContactMaterial& materialB,
    ContactImpulseState& state) {
    solveContactImpulseTensor(
        velocityA,angularVelocityA,velocityB,angularVelocityB,
        invMassA,invMassB,
        glm::mat3(invInertiaA),glm::mat3(invInertiaB),
        leverArmA,leverArmB,normal,materialA,materialB,state);
}

inline void warmStartContactImpulse(
    glm::vec3& velocityA, glm::vec3& velocityB,
    float invMassA, float invMassB, glm::vec3 normal,
    ContactImpulseState& state) {
    glm::vec3 angularA(0.0f),angularB(0.0f);
    warmStartContactImpulseAngular(
        velocityA,angularA,velocityB,angularB,
        invMassA,invMassB,0.0f,0.0f,
        glm::vec3(0.0f),glm::vec3(0.0f),normal,state);
}

inline void solveContactImpulse(
    glm::vec3& velocityA, glm::vec3& velocityB,
    float invMassA, float invMassB, glm::vec3 normal,
    const ContactMaterial& materialA, const ContactMaterial& materialB,
    ContactImpulseState& state) {
    glm::vec3 angularA(0.0f),angularB(0.0f);
    solveContactImpulseAngular(
        velocityA,angularA,velocityB,angularB,
        invMassA,invMassB,0.0f,0.0f,
        glm::vec3(0.0f),glm::vec3(0.0f),normal,
        materialA,materialB,state);
}

inline void solveFloorImpactTensor(
    glm::vec3& velocity,glm::vec3& angularVelocity,
    float invMass,const glm::mat3& invInertia,glm::vec3 leverArm,
    const ContactMaterial& body) {
    if (invMass<=0.0f) return;

    const glm::vec3 up(0.0f,1.0f,0.0f);
    const glm::vec3 pointBefore=
        contactPointVelocity(velocity,angularVelocity,leverArm);
    const float normalSpeed=glm::dot(pointBefore,up);
    if (normalSpeed>=0.0f) return;

    const float normalInvMass=directionalInverseMassTensor(
        invMass,0.0f,invInertia,glm::mat3(0.0f),
        leverArm,glm::vec3(0.0f),up);
    if (normalInvMass<=0.0f) return;

    const float impactSpeed=-normalSpeed;
    float rebound=impactSpeed*body.restitution;
    if (rebound<0.25f) rebound=0.0f;
    const float normalImpulse=(rebound-normalSpeed)/normalInvMass;
    const glm::vec3 normalVector=up*normalImpulse;
    velocity+=normalVector*invMass;
    angularVelocity+=invInertia*glm::cross(leverArm,normalVector);

    const glm::vec3 pointAfter=
        contactPointVelocity(velocity,angularVelocity,leverArm);
    const glm::vec3 tangent=
        pointAfter-glm::dot(pointAfter,up)*up;
    const float tangentSpeed=glm::length(tangent);
    if (tangentSpeed<1.0e-8f) return;

    const glm::vec3 tangentDirection=tangent/tangentSpeed;
    const float tangentInvMass=directionalInverseMassTensor(
        invMass,0.0f,invInertia,glm::mat3(0.0f),
        leverArm,glm::vec3(0.0f),tangentDirection);
    if (tangentInvMass<=0.0f) return;

    glm::vec3 impulse=-tangent/tangentInvMass;
    const float staticLimit=body.staticFriction*normalImpulse;
    const float desiredMagnitude=glm::length(impulse);
    if (desiredMagnitude>staticLimit)
        impulse=-tangentDirection*(body.dynamicFriction*normalImpulse);

    velocity+=impulse*invMass;
    angularVelocity+=invInertia*glm::cross(leverArm,impulse);
}

inline void solveFloorImpactAngular(
    glm::vec3& velocity, glm::vec3& angularVelocity,
    float invMass, float invInertia, float sphereRadius,
    const ContactMaterial& body) {
    solveFloorImpactTensor(
        velocity,angularVelocity,invMass,glm::mat3(invInertia),
        glm::vec3(0.0f,-sphereRadius,0.0f),body);
}

inline void solveFloorImpact(glm::vec3& velocity,
                             const ContactMaterial& body) {
    glm::vec3 angularVelocity(0.0f);
    solveFloorImpactAngular(
        velocity,angularVelocity,1.0f,0.0f,0.0f,body);
}

} // namespace engine2
