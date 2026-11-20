#pragma once

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace engine2 {

enum class ColliderShape : std::uint8_t { Sphere = 0, Box = 1 };

struct ColliderGeometry {
    ColliderShape shape = ColliderShape::Sphere;
    float radius = 0.5f;
    glm::vec3 halfExtents{0.5f};
};

struct Contact {
    bool colliding = false;
    glm::vec3 normal{1.0f, 0.0f, 0.0f}; // unit vector from A toward B
    float penetration = 0.0f;
    glm::vec3 point{0.0f};
};

struct ManifoldPoint {
    glm::vec3 point{0.0f};
    float penetration = 0.0f;
};

struct ContactManifold {
    bool colliding = false;
    glm::vec3 normal{1.0f,0.0f,0.0f}; // unit vector from A toward B
    float penetration = 0.0f;
    std::array<ManifoldPoint,4> points{};
    std::size_t pointCount = 0;
};

struct OrientedBox {
    glm::vec3 center{0.0f};
    glm::mat3 rotation{1.0f}; // orthonormal body axes in columns
    glm::vec3 halfExtents{0.5f};
};

inline glm::vec3 colliderHalfExtents(const ColliderGeometry& geometry) {
    return geometry.shape == ColliderShape::Box
        ? geometry.halfExtents : glm::vec3(geometry.radius);
}

inline glm::vec3 orientedBoxLocalPoint(
    const OrientedBox& box,glm::vec3 worldPoint) {
    const glm::vec3 offset=worldPoint-box.center;
    return {
        glm::dot(offset,box.rotation[0]),
        glm::dot(offset,box.rotation[1]),
        glm::dot(offset,box.rotation[2])
    };
}

inline glm::vec3 orientedBoxWorldPoint(
    const OrientedBox& box,glm::vec3 localPoint) {
    return box.center +
        box.rotation[0]*localPoint.x +
        box.rotation[1]*localPoint.y +
        box.rotation[2]*localPoint.z;
}

inline glm::vec3 orientedBoxFeatureSupport(
    const OrientedBox& box,glm::vec3 direction) {
    glm::vec3 result=box.center;
    constexpr float featureEpsilon=1.0e-6f;
    for (int axis=0;axis<3;++axis) {
        const float projection=glm::dot(direction,box.rotation[axis]);
        if (projection>featureEpsilon)
            result+=box.rotation[axis]*box.halfExtents[axis];
        else if (projection<-featureEpsilon)
            result-=box.rotation[axis]*box.halfExtents[axis];
    }
    return result;
}

inline float orientedBoxProjectedRadius(
    const OrientedBox& box,glm::vec3 axis) {
    return
        box.halfExtents.x*std::abs(glm::dot(axis,box.rotation[0])) +
        box.halfExtents.y*std::abs(glm::dot(axis,box.rotation[1])) +
        box.halfExtents.z*std::abs(glm::dot(axis,box.rotation[2]));
}

inline Contact sphereSphereContact(glm::vec3 a, float radiusA,
                                   glm::vec3 b, float radiusB) {
    const glm::vec3 offset = b - a;
    const float distanceSquared = glm::dot(offset, offset);
    const float radius = radiusA + radiusB;
    if (distanceSquared >= radius * radius) return {};
    const float distance = std::sqrt(distanceSquared);
    const glm::vec3 normal=distance > 1.0e-6f
        ? offset / distance : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 point=0.5f*
        ((a+normal*radiusA)+(b-normal*radiusB));
    return {true,normal,radius-distance,point};
}

// Normal points from the SPHERE toward the BOX so the impulse routine can
// always use one convention. The inside-box branch selects the nearest face
// and uses the physical face point even though the correction normal points
// in the opposite direction.
inline Contact sphereOrientedBoxContact(
    glm::vec3 sphere,float radius,const OrientedBox& box) {
    const glm::vec3 local=orientedBoxLocalPoint(box,sphere);
    const glm::vec3 closest=glm::clamp(
        local,-box.halfExtents,box.halfExtents);
    const glm::vec3 boxPoint=orientedBoxWorldPoint(box,closest);
    const glm::vec3 delta=boxPoint-sphere;
    const float distanceSquared=glm::dot(delta,delta);
    if (distanceSquared>1.0e-12f) {
        if (distanceSquared>=radius*radius) return {};
        const float distance=std::sqrt(distanceSquared);
        const glm::vec3 normal=delta/distance;
        const glm::vec3 spherePoint=sphere+normal*radius;
        return {true,normal,radius-distance,
                0.5f*(spherePoint+boxPoint)};
    }

    const glm::vec3 faceDistance=box.halfExtents-glm::abs(local);
    int axis=0;
    if (faceDistance.y<faceDistance.x) axis=1;
    if (faceDistance.z<faceDistance[axis]) axis=2;
    const float side=local[axis]>=0.0f ? 1.0f : -1.0f;
    glm::vec3 faceLocal=local;
    faceLocal[axis]=side*box.halfExtents[axis];
    const glm::vec3 facePoint=orientedBoxWorldPoint(box,faceLocal);
    const glm::vec3 normal=-side*box.rotation[axis];
    const glm::vec3 spherePoint=sphere-normal*radius;
    return {true,normal,radius+faceDistance[axis],
            0.5f*(spherePoint+facePoint)};
}

inline Contact sphereBoxContact(glm::vec3 sphere, float radius,
                                glm::vec3 box, glm::vec3 halfExtents) {
    return sphereOrientedBoxContact(
        sphere,radius,{box,glm::mat3(1.0f),halfExtents});
}

enum class ObbSatAxisKind : std::uint8_t { FaceA, FaceB, Edge };

struct ObbSatResult {
    bool colliding = false;
    glm::vec3 normal{1.0f,0.0f,0.0f};
    float penetration = 0.0f;
    ObbSatAxisKind kind = ObbSatAxisKind::FaceA;
    int axisA = 0;
    int axisB = 0;
};

inline ObbSatResult orientedBoxSat(
    const OrientedBox& a,const OrientedBox& b) {
    const glm::vec3 delta=b.center-a.center;
    float minimum=std::numeric_limits<float>::infinity();
    ObbSatResult best;

    const auto priority=[](ObbSatAxisKind kind) {
        return kind==ObbSatAxisKind::Edge ? 1 : 0;
    };
    const auto consider=[&](glm::vec3 rawAxis,ObbSatAxisKind kind,
                            int axisA,int axisB) {
        const float squared=glm::dot(rawAxis,rawAxis);
        if (squared<1.0e-10f) return true;
        const glm::vec3 axis=rawAxis/std::sqrt(squared);
        const float radiusA=orientedBoxProjectedRadius(a,axis);
        const float radiusB=orientedBoxProjectedRadius(b,axis);
        const float separation=std::abs(glm::dot(delta,axis));
        const float overlap=radiusA+radiusB-separation;
        if (overlap<=0.0f) return false;

        constexpr float tieEpsilon=1.0e-5f;
        const bool better=overlap<minimum-tieEpsilon ||
            (std::abs(overlap-minimum)<=tieEpsilon &&
             priority(kind)<priority(best.kind));
        if (better) {
            minimum=overlap;
            best.colliding=true;
            best.normal=glm::dot(delta,axis)>=0.0f ? axis : -axis;
            best.penetration=overlap;
            best.kind=kind;
            best.axisA=axisA;
            best.axisB=axisB;
        }
        return true;
    };

    for (int axis=0;axis<3;++axis)
        if (!consider(a.rotation[axis],ObbSatAxisKind::FaceA,axis,-1))
            return {};
    for (int axis=0;axis<3;++axis)
        if (!consider(b.rotation[axis],ObbSatAxisKind::FaceB,-1,axis))
            return {};
    for (int i=0;i<3;++i)
        for (int j=0;j<3;++j)
            if (!consider(glm::cross(a.rotation[i],b.rotation[j]),
                          ObbSatAxisKind::Edge,i,j))
                return {};
    return best;
}

struct ClipPolygon {
    std::array<glm::vec3,8> vertices{};
    std::size_t count = 0;
};

inline ClipPolygon clipPolygonAgainstPlane(
    const ClipPolygon& input,glm::vec3 normal,float offset) {
    ClipPolygon output;
    if (input.count==0) return output;
    constexpr float epsilon=1.0e-6f;

    glm::vec3 previous=input.vertices[input.count-1];
    float previousDistance=glm::dot(normal,previous)-offset;
    bool previousInside=previousDistance<=epsilon;
    for (std::size_t i=0;i<input.count;++i) {
        const glm::vec3 current=input.vertices[i];
        const float currentDistance=glm::dot(normal,current)-offset;
        const bool currentInside=currentDistance<=epsilon;

        if (currentInside!=previousInside) {
            const float denominator=previousDistance-currentDistance;
            const float t=std::abs(denominator)>1.0e-12f
                ? previousDistance/denominator : 0.0f;
            if (output.count<output.vertices.size())
                output.vertices[output.count++]=
                    previous+(current-previous)*glm::clamp(t,0.0f,1.0f);
        }
        if (currentInside && output.count<output.vertices.size())
            output.vertices[output.count++]=current;

        previous=current;
        previousDistance=currentDistance;
        previousInside=currentInside;
    }
    return output;
}

inline void addManifoldPoint(
    ContactManifold& manifold,glm::vec3 point,float penetration) {
    if (penetration<0.0f) return;
    if (manifold.pointCount<manifold.points.size()) {
        manifold.points[manifold.pointCount++]={point,penetration};
        return;
    }

    std::size_t shallowest=0;
    for (std::size_t i=1;i<manifold.points.size();++i)
        if (manifold.points[i].penetration<
            manifold.points[shallowest].penetration)
            shallowest=i;
    if (penetration>manifold.points[shallowest].penetration)
        manifold.points[shallowest]={point,penetration};
}

inline ContactManifold orientedBoxFaceManifold(
    const OrientedBox& reference,const OrientedBox& incident,
    int referenceAxis,glm::vec3 referenceNormal,
    glm::vec3 manifoldNormal,float satPenetration) {
    ContactManifold result;
    result.colliding=true;
    result.normal=manifoldNormal;
    result.penetration=satPenetration;

    const float referenceSide=
        glm::dot(reference.rotation[referenceAxis],referenceNormal)>=0.0f
            ? 1.0f : -1.0f;
    const glm::vec3 referenceFaceCenter=
        reference.center+
        reference.rotation[referenceAxis]*
            (referenceSide*reference.halfExtents[referenceAxis]);

    int incidentAxis=0;
    float incidentAlignment=
        std::abs(glm::dot(incident.rotation[0],referenceNormal));
    for (int axis=1;axis<3;++axis) {
        const float candidate=
            std::abs(glm::dot(incident.rotation[axis],referenceNormal));
        if (candidate>incidentAlignment) {
            incidentAlignment=candidate;
            incidentAxis=axis;
        }
    }
    const float incidentSide=
        glm::dot(incident.rotation[incidentAxis],referenceNormal)>0.0f
            ? -1.0f : 1.0f;
    const glm::vec3 incidentCenter=
        incident.center+
        incident.rotation[incidentAxis]*
            (incidentSide*incident.halfExtents[incidentAxis]);

    int sideAxes[2];
    int sideCount=0;
    for (int axis=0;axis<3;++axis)
        if (axis!=incidentAxis) sideAxes[sideCount++]=axis;
    const glm::vec3 iu=
        incident.rotation[sideAxes[0]]*incident.halfExtents[sideAxes[0]];
    const glm::vec3 iv=
        incident.rotation[sideAxes[1]]*incident.halfExtents[sideAxes[1]];

    ClipPolygon polygon;
    polygon.count=4;
    polygon.vertices[0]=incidentCenter-iu-iv;
    polygon.vertices[1]=incidentCenter+iu-iv;
    polygon.vertices[2]=incidentCenter+iu+iv;
    polygon.vertices[3]=incidentCenter-iu+iv;

    int referenceSideAxes[2];
    sideCount=0;
    for (int axis=0;axis<3;++axis)
        if (axis!=referenceAxis) referenceSideAxes[sideCount++]=axis;

    for (int side=0;side<2;++side) {
        const int axis=referenceSideAxes[side];
        const glm::vec3 direction=reference.rotation[axis];
        const float positive=
            glm::dot(direction,reference.center)+reference.halfExtents[axis];
        polygon=clipPolygonAgainstPlane(polygon,direction,positive);
        const glm::vec3 negativeDirection=-direction;
        const float negative=
            glm::dot(negativeDirection,reference.center)+
            reference.halfExtents[axis];
        polygon=clipPolygonAgainstPlane(
            polygon,negativeDirection,negative);
    }

    constexpr float contactEpsilon=2.0e-4f;
    for (std::size_t i=0;i<polygon.count;++i) {
        const glm::vec3 point=polygon.vertices[i];
        const float separation=
            glm::dot(referenceNormal,point-referenceFaceCenter);
        if (separation>contactEpsilon) continue;
        const float penetration=std::max(0.0f,-separation);
        const glm::vec3 midpoint=
            point-referenceNormal*(0.5f*separation);
        addManifoldPoint(result,midpoint,penetration);
    }
    return result;
}

inline void closestSegmentPoints(
    glm::vec3 p1,glm::vec3 q1,glm::vec3 p2,glm::vec3 q2,
    glm::vec3& first,glm::vec3& second) {
    const glm::vec3 d1=q1-p1;
    const glm::vec3 d2=q2-p2;
    const glm::vec3 r=p1-p2;
    const float a=glm::dot(d1,d1);
    const float e=glm::dot(d2,d2);
    const float f=glm::dot(d2,r);

    float s=0.0f;
    float t=0.0f;
    constexpr float epsilon=1.0e-10f;
    if (a<=epsilon && e<=epsilon) {
        first=p1;
        second=p2;
        return;
    }
    if (a<=epsilon) {
        t=glm::clamp(f/e,0.0f,1.0f);
    } else {
        const float c=glm::dot(d1,r);
        if (e<=epsilon) {
            s=glm::clamp(-c/a,0.0f,1.0f);
        } else {
            const float b=glm::dot(d1,d2);
            const float denominator=a*e-b*b;
            if (std::abs(denominator)>epsilon)
                s=glm::clamp((b*f-c*e)/denominator,0.0f,1.0f);
            t=(b*s+f)/e;
            if (t<0.0f) {
                t=0.0f;
                s=glm::clamp(-c/a,0.0f,1.0f);
            } else if (t>1.0f) {
                t=1.0f;
                s=glm::clamp((b-c)/a,0.0f,1.0f);
            }
        }
    }
    first=p1+d1*s;
    second=p2+d2*t;
}

inline std::pair<glm::vec3,glm::vec3> orientedBoxEdgeSegment(
    const OrientedBox& box,int edgeAxis,glm::vec3 supportDirection) {
    glm::vec3 center=box.center;
    for (int axis=0;axis<3;++axis) {
        if (axis==edgeAxis) continue;
        const float sign=glm::dot(
            supportDirection,box.rotation[axis])>=0.0f ? 1.0f : -1.0f;
        center+=box.rotation[axis]*(sign*box.halfExtents[axis]);
    }
    const glm::vec3 extent=
        box.rotation[edgeAxis]*box.halfExtents[edgeAxis];
    return {center-extent,center+extent};
}

inline ContactManifold orientedBoxManifold(
    const OrientedBox& a,const OrientedBox& b) {
    const ObbSatResult sat=orientedBoxSat(a,b);
    if (!sat.colliding) return {};

    ContactManifold result;
    if (sat.kind==ObbSatAxisKind::FaceA) {
        result=orientedBoxFaceManifold(
            a,b,sat.axisA,sat.normal,sat.normal,sat.penetration);
    } else if (sat.kind==ObbSatAxisKind::FaceB) {
        result=orientedBoxFaceManifold(
            b,a,sat.axisB,-sat.normal,sat.normal,sat.penetration);
    } else {
        result.colliding=true;
        result.normal=sat.normal;
        result.penetration=sat.penetration;
        const auto edgeA=orientedBoxEdgeSegment(a,sat.axisA,sat.normal);
        const auto edgeB=orientedBoxEdgeSegment(b,sat.axisB,-sat.normal);
        glm::vec3 pointA,pointB;
        closestSegmentPoints(
            edgeA.first,edgeA.second,edgeB.first,edgeB.second,
            pointA,pointB);
        addManifoldPoint(
            result,0.5f*(pointA+pointB),sat.penetration);
    }

    if (result.pointCount==0) {
        result.colliding=true;
        result.normal=sat.normal;
        result.penetration=sat.penetration;
        const glm::vec3 pointA=orientedBoxFeatureSupport(a,sat.normal);
        const glm::vec3 pointB=orientedBoxFeatureSupport(b,-sat.normal);
        addManifoldPoint(
            result,0.5f*(pointA+pointB),sat.penetration);
    }
    return result;
}

inline ContactManifold orientedBoxPlaneManifold(
    const OrientedBox& box,glm::vec3 planeNormal,float planeOffset,
    float contactTolerance=0.002f,float featureTolerance=0.002f) {
    ContactManifold result;
    const float normalLength=glm::length(planeNormal);
    if (!(normalLength>1.0e-8f)) return result;
    planeNormal/=normalLength;
    planeOffset/=normalLength;

    std::array<glm::vec3,8> vertices{};
    std::array<float,8> separations{};
    float minimum=std::numeric_limits<float>::infinity();
    std::size_t index=0;
    for (int x=-1;x<=1;x+=2)
        for (int y=-1;y<=1;y+=2)
            for (int z=-1;z<=1;z+=2) {
                const glm::vec3 local(
                    static_cast<float>(x)*box.halfExtents.x,
                    static_cast<float>(y)*box.halfExtents.y,
                    static_cast<float>(z)*box.halfExtents.z);
                const glm::vec3 vertex=orientedBoxWorldPoint(box,local);
                const float separation=
                    glm::dot(planeNormal,vertex)-planeOffset;
                vertices[index]=vertex;
                separations[index]=separation;
                minimum=std::min(minimum,separation);
                ++index;
            }

    if (minimum>contactTolerance) return result;
    result.colliding=true;
    // Convention matches body A -> static plane B.
    result.normal=-planeNormal;
    result.penetration=std::max(0.0f,-minimum);

    const float supportLimit=
        std::min(contactTolerance,minimum+featureTolerance);
    for (std::size_t i=0;i<vertices.size();++i) {
        if (separations[i]>supportLimit) continue;
        const float penetration=std::max(0.0f,-separations[i]);
        // Midpoint between the body vertex and its orthogonal plane
        // projection keeps the lever arm centered inside a small penetration.
        const glm::vec3 point=
            vertices[i]-planeNormal*(0.5f*separations[i]);
        addManifoldPoint(result,point,penetration);
    }

    if (result.pointCount==0) {
        std::size_t lowest=0;
        for (std::size_t i=1;i<separations.size();++i)
            if (separations[i]<separations[lowest]) lowest=i;
        addManifoldPoint(
            result,vertices[lowest],std::max(0.0f,-separations[lowest]));
    }
    return result;
}

inline ContactManifold colliderPlaneManifoldOriented(
    const ColliderGeometry& body,glm::vec3 position,
    const glm::mat3& rotation,glm::vec3 planeNormal,float planeOffset) {
    const float normalLength=glm::length(planeNormal);
    if (!(normalLength>1.0e-8f)) return {};
    const glm::vec3 unitNormal=planeNormal/normalLength;
    const float offset=planeOffset/normalLength;

    if (body.shape==ColliderShape::Box)
        return orientedBoxPlaneManifold(
            {position,rotation,body.halfExtents},unitNormal,offset);

    const float separation=
        glm::dot(unitNormal,position)-body.radius-offset;
    constexpr float contactTolerance=0.002f;
    if (separation>contactTolerance) return {};
    ContactManifold result;
    result.colliding=true;
    result.normal=-unitNormal;
    result.penetration=std::max(0.0f,-separation);
    result.points[0]={
        position-unitNormal*body.radius-
            unitNormal*(0.5f*separation),
        result.penetration
    };
    result.pointCount=1;
    return result;
}

inline ContactManifold singleContactManifold(const Contact& contact) {
    ContactManifold result;
    if (!contact.colliding) return result;
    result.colliding=true;
    result.normal=contact.normal;
    result.penetration=contact.penetration;
    result.points[0]={contact.point,contact.penetration};
    result.pointCount=1;
    return result;
}

inline Contact orientedBoxContact(
    const OrientedBox& a,const OrientedBox& b) {
    const ContactManifold manifold=orientedBoxManifold(a,b);
    if (!manifold.colliding || manifold.pointCount==0) return {};
    glm::vec3 average(0.0f);
    for (std::size_t i=0;i<manifold.pointCount;++i)
        average+=manifold.points[i].point;
    average/=static_cast<float>(manifold.pointCount);
    return {true,manifold.normal,manifold.penetration,average};
}

inline Contact boxBoxContact(glm::vec3 a, glm::vec3 halfA,
                             glm::vec3 b, glm::vec3 halfB) {
    return orientedBoxContact(
        {a,glm::mat3(1.0f),halfA},
        {b,glm::mat3(1.0f),halfB});
}

inline Contact colliderContactOriented(
    const ColliderGeometry& a,glm::vec3 positionA,const glm::mat3& rotationA,
    const ColliderGeometry& b,glm::vec3 positionB,const glm::mat3& rotationB) {
    if (a.shape==ColliderShape::Sphere && b.shape==ColliderShape::Sphere)
        return sphereSphereContact(positionA,a.radius,positionB,b.radius);
    if (a.shape==ColliderShape::Sphere && b.shape==ColliderShape::Box)
        return sphereOrientedBoxContact(
            positionA,a.radius,{positionB,rotationB,b.halfExtents});
    if (a.shape==ColliderShape::Box && b.shape==ColliderShape::Sphere) {
        Contact contact=sphereOrientedBoxContact(
            positionB,b.radius,{positionA,rotationA,a.halfExtents});
        contact.normal=-contact.normal;
        return contact;
    }
    return orientedBoxContact(
        {positionA,rotationA,a.halfExtents},
        {positionB,rotationB,b.halfExtents});
}

inline ContactManifold colliderContactManifoldOriented(
    const ColliderGeometry& a,glm::vec3 positionA,const glm::mat3& rotationA,
    const ColliderGeometry& b,glm::vec3 positionB,const glm::mat3& rotationB) {
    if (a.shape==ColliderShape::Box && b.shape==ColliderShape::Box)
        return orientedBoxManifold(
            {positionA,rotationA,a.halfExtents},
            {positionB,rotationB,b.halfExtents});
    return singleContactManifold(
        colliderContactOriented(
            a,positionA,rotationA,b,positionB,rotationB));
}

inline Contact colliderContact(const ColliderGeometry& a, glm::vec3 positionA,
                               const ColliderGeometry& b, glm::vec3 positionB) {
    return colliderContactOriented(
        a,positionA,glm::mat3(1.0f),
        b,positionB,glm::mat3(1.0f));
}

// Compatibility helper for the sphere-only angular stages and CCD path.
// Discrete OBB contacts use Contact::point directly for both lever arms.
inline glm::vec3 colliderContactLeverArm(
    const ColliderGeometry& body, glm::vec3 position,
    const ColliderGeometry& other, glm::vec3 otherPosition,
    glm::vec3 normal, bool firstBody) {
    if (body.shape != ColliderShape::Sphere) return glm::vec3(0.0f);

    float direction = firstBody ? 1.0f : -1.0f;
    if (other.shape == ColliderShape::Box) {
        const glm::vec3 local = position - otherPosition;
        const bool centerInside =
            std::abs(local.x) <= other.halfExtents.x &&
            std::abs(local.y) <= other.halfExtents.y &&
            std::abs(local.z) <= other.halfExtents.z;
        if (centerInside) direction = -direction;
    }
    return normal * (direction * body.radius);
}

} // namespace engine2
