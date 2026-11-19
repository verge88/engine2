#include "engine2/ContinuousCollision.hpp"

#include <glm/geometric.hpp>

#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace {
void expect(bool okay,const char* message) {
    if (!okay) throw std::runtime_error(message);
}
bool near(float a,float b,float tolerance=0.0004f) {
    return std::abs(a-b)<tolerance;
}
float distanceToBox(glm::vec3 point,glm::vec3 half) {
    return glm::length(point-glm::clamp(point,-half,half));
}
}

int main() {
    using namespace engine2;
    const ColliderGeometry box{ColliderShape::Box,0.5f,glm::vec3(0.5f)};
    constexpr float radius=0.3f;
    const auto face=sweepSphereStatic(
        {-2,0,0},{2,0,0},radius,box,{0,0,0});
    expect(face && near(face->fraction,0.3f) &&
           face->feature==SweptContactFeature::Face &&
           near(face->normal.x,1.0f) && near(face->normal.y,0.0f),
           "Exact rounded box must preserve direct face CCD");

    const auto edge=sweepSphereStatic(
        {-2,0.7f,0},{2,0.7f,0},radius,box,{0,0,0});
    const float edgeEntry=(-0.5f-std::sqrt(radius*radius-0.2f*0.2f)+2.0f)/4;
    expect(edge && near(edge->fraction,edgeEntry) &&
           edge->feature==SweptContactFeature::Edge &&
           near(edge->normal.x,std::sqrt(0.05f)/radius) &&
           near(edge->normal.y,-0.2f/radius),
           "Swept sphere must hit cylindrical edge with diagonal normal");

    const auto corner=sweepSphereStatic(
        {-2,0.7f,0.7f},{2,0.7f,0.7f},radius,box,{0,0,0});
    expect(corner && near(corner->fraction,0.35f) &&
           corner->feature==SweptContactFeature::Corner &&
           near(corner->normal.x,1.0f/3.0f) &&
           near(corner->normal.y,-2.0f/3.0f) &&
           near(corner->normal.z,-2.0f/3.0f),
           "Swept sphere must hit true spherical corner with 3D normal");

    // Expanded box would return a face hit (x=-0.8) here, even though the
    // sphere is too far from the actual rounded corner to touch anything.
    expect(!sweepSphereStatic(
        {-2,0.79f,0.79f},{2,0.79f,0.79f},radius,box,{0,0,0}),
        "False AABB corner hit must be rejected");
    expect(!sweepSphereStatic(
        {-2,0.81f,0},{2,0.81f,0},radius,box,{0,0,0}),
        "False AABB edge hit must be rejected");

    // Entry into the expanded box is BEFORE true edge/corner impact.
    expect(edge->fraction>0.3f && corner->fraction>edge->fraction,
           "Edge/corner CCD must not collide at an expanded slab face");

    const auto negative=sweepSphereStatic(
        {2,-0.7f,0},{-2,-0.7f,0},radius,box,{0,0,0});
    expect(negative && negative->feature==SweptContactFeature::Edge &&
           negative->normal.x < -0.7f && negative->normal.y > 0.6f,
           "Signed edge normals must point from moving sphere to box");

    // Exact touching without inward movement must not create an impulse.
    expect(!sweepSphereStatic(
        {-0.8f,0,0},{-2,0,0},radius,box,{0,0,0}),
        "Outward-moving touching sphere must not collide");
    const auto inward=sweepSphereStatic(
        {-0.8f,0,0},{0,0,0},radius,box,{0,0,0});
    expect(inward && near(inward->fraction,0) &&
           inward->feature==SweptContactFeature::Face,
           "Exact face touching with inward movement needs immediate TOI");
    expect(!sweepSphereStatic(
        {0,0,0},{2,0,0},radius,box,{0,0,0}),
        "Sphere already inside box must use discrete contact solver");
    expect(!sweepSphereStatic(
        {-2,0,0},{-2,0,0},radius,box,{0,0,0}),
        "Stationary sphere cannot produce CCD contact");

    // Exercise 361 axial rays across all face/edge/corner Voronoi regions.
    // Independent closed-form reference: projecting (y,z) to the box's
    // cross-section gives the true x coordinate of the first contact.
    std::size_t hits=0,misses=0,faces=0,edges=0,corners=0;
    for (int yi=-9;yi<=9;++yi) {
        for (int zi=-9;zi<=9;++zi) {
            const float y=static_cast<float>(yi)*0.09f;
            const float z=static_cast<float>(zi)*0.09f;
            const float dy=std::max(std::abs(y)-0.5f,0.0f);
            const float dz=std::max(std::abs(z)-0.5f,0.0f);
            const float yz=std::sqrt(dy*dy+dz*dz);
            const auto result=sweepSphereRoundedBox(
                {-2,y,z},{2,y,z},radius,{0,0,0},{0.5f,0.5f,0.5f});
            if (yz >= radius) {
                expect(!result,"Rounded-box CCD falsely hit distant edge/corner");
                ++misses;
                continue;
            }
            const float entryX=-0.5f-
                std::sqrt(radius*radius-yz*yz);
            const float fraction=(entryX+2.0f)/4.0f;
            expect(result && near(result->fraction,fraction,0.0001f),
                   "CCD TOI differs from closed-form rounded-box entry time");
            const glm::vec3 impact(-2+4*result->fraction,y,z);
            expect(near(distanceToBox(impact,{0.5f,0.5f,0.5f}),radius,0.0002f),
                   "CCD contact point does not lie on exact rounded surface");
            expect(glm::dot(glm::vec3(4,0,0),result->normal)>0,
                   "CCD normal does not oppose approaching motion");
            const int outside=int(dy>0)+int(dz>0);
            const auto feature=outside==0 ? SweptContactFeature::Face :
                outside==1 ? SweptContactFeature::Edge :
                             SweptContactFeature::Corner;
            expect(result->feature==feature,
                   "Rounded-box collision feature classification incorrect");
            if (outside==0) ++faces;
            else if (outside==1) ++edges;
            else ++corners;
            ++hits;
        }
    }
    expect(hits>100 && misses>30 && faces>0 && edges>0 && corners>0,
           "Rounded-box grid test did not cover all feature categories");

    // Large swept displacement still finds the narrow corner at the
    // correct normalized time rather than jumping over the volume.
    const auto fast=sweepSphereStatic(
        {-500,0.7f,0.7f},{500,0.7f,0.7f},radius,box,{0,0,0});
    expect(fast && near(fast->fraction,499.4f/1000.0f,1.0e-6f),
           "High-speed exact corner CCD tunnelling regression");
    return 0;
}
