#include "engine2/TrianglePicking.hpp"
#include "engine2/ScenePicking.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace {
void check(bool okay, const char* message) {
    if (!okay) throw std::runtime_error(message);
}
bool close(float a,float b,float tol=0.0005f) {
    return std::abs(a-b)<tol;
}
template<typename F>
void fails(F&& callback,const char* message) {
    bool rejected=false;
    try {callback();} catch(const std::invalid_argument&) {rejected=true;}
    check(rejected,message);
}
}

int main() {
    using namespace engine2;

    // A triangular silhouette occupies ONLY half of its rectangular AABB.
    // The v1.8 box picker falsely hit at x=+0.9,y=+0.9 while the precise
    // raycast must reject that empty space.
    const std::vector<glm::vec3> vertices{
        {-1.0f,-1.0f,0.0f},{1.0f,-1.0f,0.0f},{-1.0f,1.0f,0.0f}
    };
    const std::vector<std::uint32_t> indices{0,1,2};
    const PickRay hit{{-0.5f,-0.5f,3.0f},{0.0f,0.0f,-1.0f}};
    const PickRay empty{{0.9f,0.9f,3.0f},{0.0f,0.0f,-1.0f}};
    const Bounds bounds{{-1.0f,-1.0f,0.0f},{1.0f,1.0f,0.0f}};
    check(intersectPickBounds(empty,bounds,glm::mat4(1.0f)).has_value(),
          "Expected AABB false positive in a triangle's empty corner");
    const auto distance=intersectPickTriangles(
        vertices,indices,hit.origin,hit.direction);
    check(distance && close(*distance,3.0f),
          "Real triangle ray hit failed");
    check(!intersectPickTriangles(
          vertices,indices,empty.origin,empty.direction),
          "Precise triangle picking must reject empty AABB corner");
    check(close(*intersectPickTriangles(
          vertices,indices,{0,-0.5f,-3},{0,0,1}),3.0f),
          "Triangle picking must be two sided");
    check(!intersectPickTriangles(vertices,indices,
                                 {-0.5f,-0.5f,-3},{0,0,-1}),
          "Triangle behind ray origin falsely selected");
    check(!intersectPickTriangle(
          {0,0,3},{0,0,-1},{0,0,0},{0,0,0},{1,1,0}),
          "Degenerate triangle should be ignored");
    check(!intersectPickTriangle(
          {0,0,3},{1,0,0},vertices[0],vertices[1],vertices[2]),
          "Parallel triangle ray falsely selected");

    // The value t stays in world ray units even when local ray direction
    // was transformed by an inverse nonuniform model matrix.
    const glm::mat4 transform=
        glm::translate(glm::mat4(1.0f),glm::vec3(0,0,-2)) *
        glm::scale(glm::mat4(1.0f),glm::vec3(2,3,0.5f));
    const glm::mat4 inverse=glm::inverse(transform);
    const glm::vec3 worldOrigin(-1.0f,-1.5f,3.0f);
    const glm::vec3 localOrigin(inverse*glm::vec4(worldOrigin,1));
    const glm::vec3 localDirection(inverse*glm::vec4(0,0,-1,0));
    const auto transformed=intersectPickTriangles(
        vertices,indices,localOrigin,localDirection);
    check(transformed && close(*transformed,5.0f),
          "Ray distance must be invariant under nonuniform transform");

    // The closest triangle wins regardless of the source index order.
    const std::vector<glm::vec3> layers{
        {-1,-1,-3},{1,-1,-3},{-1,1,-3},
        {-1,-1,0},{1,-1,0},{-1,1,0}
    };
    check(close(*intersectPickTriangles(
        layers,{0,1,2,3,4,5},hit.origin,hit.direction),3.0f),
        "Triangle soup must return closest hit, not first triangle");
    fails([&]{(void)intersectPickTriangles(vertices,{0,1},
                                         hit.origin,hit.direction);},
          "Malformed triangle list must be rejected");
    fails([&]{(void)intersectPickTriangles(vertices,{0,1,99},
                                         hit.origin,hit.direction);},
          "Out-of-range triangle indices must be rejected");

    // Marquee projection uses the object's transformed 8 AABB corners.
    const glm::vec2 viewport(1280,720);
    const auto vp=glm::perspective(glm::radians(60.0f),1280.0f/720.0f,
                                    0.1f,150.0f) *
        glm::lookAt(glm::vec3(0,0,8),glm::vec3(0,0,0),glm::vec3(0,1,0));
    const Bounds unit{{-0.5f,-0.5f,-0.5f},{0.5f,0.5f,0.5f}};
    const auto center=projectPickBounds(unit,glm::mat4(1.0f),vp,viewport);
    check(center && center->min.x<640 && center->max.x>640 &&
          center->min.y<360 && center->max.y>360,
          "Projected mesh bounds must enclose viewport center");
    check(pickRectanglesOverlap(*center,{{600,320},{680,400}}) &&
          !pickRectanglesOverlap(*center,{{50,50},{100,100}}),
          "Marquee overlap hit test incorrect");
    const glm::mat4 elsewhere=glm::translate(
        glm::mat4(1.0f),glm::vec3(100,0,0));
    const auto shifted=projectPickBounds(unit,elsewhere,vp,viewport);
    check(shifted && shifted->min.x>center->max.x,
          "Marquee bounds must use model transform");
    check(!projectPickBounds(unit,glm::mat4(1.0f),vp,{0,0}),
          "Invalid marquee projection viewport accepted");

    Scene scene;
    const Entity a=scene.createEntity("a");
    const Entity b=scene.createEntity("b");
    const Entity c=scene.createEntity("c");
    SceneSelection selection;
    selection.selectMany(scene,{a,b});
    check(selection.size()==2 && selection.primary()==b,
          "Initial marquee group selection failed");
    selection.selectMany(scene,{a,c},true);
    check(selection.size()==3 && selection.primary()==c,
          "Additive marquee must unite, not toggle existing members");
    selection.selectMany(scene,{b,b,c},false);
    check(selection.size()==2 && selection.primary()==c &&
          !selection.contains(a), "Replacing marquee selection failed");
    scene.destroyEntity(c);
    selection.prune(scene);
    check(selection.size()==1 && selection.primary()==b,
          "Marquee selection retained invalid generation handle");
    fails([&]{selection.selectMany(scene,{c},true);},
          "Marquee must reject stale entity handles");
    return 0;
}
