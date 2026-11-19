#include "engine2/GizmoMath.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <stdexcept>

namespace {

void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
bool close(float a, float b, float epsilon = 0.0006f) {
    return std::abs(a-b) < epsilon;
}
bool closeMat(const glm::mat4& a, const glm::mat4& b) {
    for (int column=0; column<4; ++column)
        for (int row=0; row<4; ++row)
            if (!close(a[column][row],b[column][row])) return false;
    return true;
}
template<typename Fn>
void mustReject(Fn fn,const char* message) {
    bool rejected=false;
    try {fn();} catch (const std::invalid_argument&) {rejected=true;}
    check(rejected,message);
}
}

int main() {
    using namespace engine2;

    const glm::mat4 parent =
        glm::translate(glm::mat4(1.0f),glm::vec3(3,2,-1)) *
        glm::rotate(glm::mat4(1.0f),glm::radians(35.0f),
                    glm::vec3(0,0,1)) *
        glm::scale(glm::mat4(1.0f),glm::vec3(2,1.5f,1));
    const glm::mat4 local =
        glm::translate(glm::mat4(1.0f),glm::vec3(0.5f,0,0)) *
        glm::rotate(glm::mat4(1.0f),glm::radians(15.0f),
                    glm::vec3(0,1,0)) *
        glm::scale(glm::mat4(1.0f),glm::vec3(0.8f,1.1f,1.2f));
    const glm::mat4 oldWorld=parent*local;

    const glm::mat4 moved=applyGizmoTransform(
        local,parent,GizmoMode::Translate,0,1.25f);
    const glm::vec3 position(oldWorld[3]);
    const glm::vec3 movedPosition((parent*moved)[3]);
    check(close(movedPosition.x,position.x+1.25f) &&
          close(movedPosition.y,position.y) &&
          close(movedPosition.z,position.z),
          "Translation under a scaled and rotated parent must use world axes");
    for(int i=0;i<3;++i)
        for(int j=0;j<4;++j)
            check(close(local[i][j],moved[i][j]),
                  "Gizmo translation must preserve local rotation/scale");

    const glm::mat4 snapped=applyGizmoTransform(
        local,parent,GizmoMode::Translate,0,0.26f,true,0.5f);
    const float expectedX=std::round((position.x+0.26f)/0.5f)*0.5f;
    check(close((parent*snapped)[3].x,expectedX),
          "World grid snapping must quantize world position");
    const glm::mat4 movedY=applyGizmoTransform(
        local,parent,GizmoMode::Translate,1,-2.0f);
    check(close((parent*movedY)[3].y,position.y-2.0f),
          "Y-axis world translation did not work");

    const float quarterTurn=glm::half_pi<float>();
    const glm::mat4 turned=applyGizmoTransform(
        local,parent,GizmoMode::Rotate,1,quarterTurn);
    const glm::mat4 expectedWorld=
        glm::translate(glm::mat4(1.0f),position) *
        glm::rotate(glm::mat4(1.0f),quarterTurn,glm::vec3(0,1,0)) *
        glm::translate(glm::mat4(1.0f),-position) * oldWorld;
    check(closeMat(parent*turned,expectedWorld),
          "World-axis rotation under arbitrary parent incorrect");
    check(close((parent*turned)[3].x,position.x) &&
          close((parent*turned)[3].z,position.z),
          "Rotating around pivot moved object origin");

    const glm::mat4 snappedRotation=applyGizmoTransform(
        local,parent,GizmoMode::Rotate,2,glm::radians(21.0f),true);
    const glm::mat4 expectedSnapRot=applyGizmoTransform(
        local,parent,GizmoMode::Rotate,2,glm::radians(15.0f));
    check(closeMat(snappedRotation,expectedSnapRot),
          "Rotation angle should snap to 15 degree increments");

    const glm::mat4 scaled=applyGizmoTransform(
        local,parent,GizmoMode::Scale,1,0.5f);
    const glm::mat4 expectedLocal=local *
        glm::scale(glm::mat4(1.0f),glm::vec3(1,1.5f,1));
    check(closeMat(scaled,expectedLocal) &&
          close(scaled[3].x,local[3].x),
          "Scaling should use local basis without changing position");

    const glm::mat4 snappedScale=applyGizmoTransform(
        local,parent,GizmoMode::Scale,0,0.19f,true);
    check(closeMat(snappedScale,local*
        glm::scale(glm::mat4(1.0f),glm::vec3(1.25f,1,1))),
        "Scaling should snap to 0.25 multiplicative steps");
    const glm::mat4 minimumScale=applyGizmoTransform(
        local,parent,GizmoMode::Scale,2,-900.0f);
    check(closeMat(minimumScale,local*
        glm::scale(glm::mat4(1.0f),glm::vec3(1,1,0.1f))),
        "Scale must remain positive and bounded");

    check(close(gizmoSnap(1.24f,0.5f),1.0f) &&
          close(gizmoSnap(-1.26f,0.5f),-1.5f),
          "Positive/negative grid snapping failed");
    const auto ringX=gizmoRingPoint({1,2,3},0,0,2);
    const auto ringY=gizmoRingPoint({1,2,3},1,glm::half_pi<float>(),2);
    const auto ringZ=gizmoRingPoint({1,2,3},2,0,2);
    check(close(ringX.y,4) && close(ringY.z,5) &&
          close(ringZ.x,3), "Projected rotation ring basis invalid");

    check(close(gizmoPointerAngleDelta({50,50},{70,50},{70,70}),
                glm::quarter_pi<float>()),
          "Pointer rotation angular delta clamp incorrect");
    check(close(gizmoPointerAngleDelta({50,50},{70,70},{70,50}),
                -glm::quarter_pi<float>()),
          "Pointer rotation negative delta clamp incorrect");
    check(close(gizmoPointerAngleDelta({50,50},{50,50},{70,70}),0),
          "At center, angle should be zero");

    const GizmoAxisProjection projected{{600,360},{720,360},1};
    check(close(gizmoFullAxisDrag(projected,{60,0}),0.5f) &&
          close(gizmoFullAxisDrag(projected,{-120,0}),-1.0f) &&
          close(gizmoFullAxisDrag(projected,{0,60}),0),
          "Full axis screen drag mapping invalid");

    glm::mat4 singular(1.0f);
    singular[0][0]=0;
    mustReject([&] {
        (void)applyGizmoTransform(local,singular,GizmoMode::Rotate,1,1);
    },"Rotation beneath singular parent must be rejected");
    mustReject([&] {
        (void)applyGizmoTransform(local,singular,GizmoMode::Translate,0,1);
    },"Translation beneath singular parent must be rejected");
    mustReject([&] {
        (void)applyGizmoTransform(local,parent,GizmoMode::Rotate,5,1);
    },"Invalid gizmo axis must be rejected");
    mustReject([&] {
        (void)applyGizmoTransform(local,parent,GizmoMode::Translate,0,1,true,0);
    },"Grid step of zero must be rejected");
    return 0;
}
