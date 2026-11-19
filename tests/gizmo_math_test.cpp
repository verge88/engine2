#include "engine2/GizmoMath.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <stdexcept>

namespace {
void check(bool valid, const char* message) {
    if (!valid) throw std::runtime_error(message);
}
bool near(float a,float b,float tolerance=0.002f) {
    return std::abs(a-b)<tolerance;
}
}

int main() {
    using namespace engine2;
    const glm::mat4 view = glm::lookAt(
        glm::vec3(0,3,8),glm::vec3(0,0,0),glm::vec3(0,1,0));
    const glm::mat4 projection =
        glm::perspective(glm::radians(60.0f),1280.0f/720.0f,0.1f,150.0f);
    const auto matrix=projection*view;
    const glm::vec2 size(1280,720);
    const auto center=projectGizmoPoint(matrix,{0,0,0},size);
    check(center.has_value() && near(center->x,640) &&
          near(center->y,360),"Projected viewport origin should be centered");
    const auto x=projectGizmoAxis(matrix,{0,0,0},{1,0,0},size,1.0f);
    const auto y=projectGizmoAxis(matrix,{0,0,0},{0,1,0},size,1.0f);
    check(x && y && x->tip.x>x->origin.x && y->tip.y<y->origin.y,
          "World X/Y gizmo directions projected incorrectly");
    const glm::vec2 motion=x->tip-x->origin;
    check(near(gizmoAxisDragDistance(*x,motion),0.5f) &&
          near(gizmoAxisDragDistance(*x,-motion),-0.5f),
          "Gizmo drag must move along world axis and clamp fast motion");
    check(near(gizmoAxisDragDistance(*x,motion*0.25f),0.25f),
          "Gizmo drag is not scaled to world units");
    check(std::abs(gizmoAxisDragDistance(*x,{0,20}))<0.001f,
          "Motion perpendicular to an X axis should have no effect");
    const glm::vec2 point=x->origin+0.5f*motion;
    check(gizmoSegmentDistance(point,*x)<0.001f &&
          gizmoSegmentDistance(point+glm::vec2(0,20),*x)>15.0f,
          "Gizmo hit-testing segment distance incorrect");

    const glm::mat4 away=glm::lookAt(
        glm::vec3(0,0,8),glm::vec3(0,0,9),glm::vec3(0,1,0));
    check(!projectGizmoPoint(projection*away,{0,0,0},size),
          "Gizmo behind the camera must be rejected");
    check(!projectGizmoAxis(matrix,{0,0,0},{1,0,0},{0,0}),
          "Invalid viewport must not project gizmo");
    check(!projectGizmoAxis(matrix,{0,0,0},{1,0,0},size,0),
          "Zero-length gizmo axis must be rejected");
    return 0;
}
