#include "engine2/ScenePicking.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <stdexcept>

namespace {
void check(bool okay, const char* message) {
    if (!okay) throw std::runtime_error(message);
}
bool near(float a, float b, float epsilon=0.002f) {
    return std::abs(a-b)<epsilon;
}
template<typename Fn>
void rejects(Fn&& fn, const char* message) {
    bool failed=false;
    try {fn();} catch(const std::invalid_argument&) {failed=true;}
    check(failed,message);
}
}

int main() {
    using namespace engine2;
    const glm::vec2 size(1280,720);
    const auto vp=glm::perspective(glm::radians(55.0f),1280.0f/720.0f,
                                   0.1f,150.0f) *
                  glm::lookAt(glm::vec3(0,0,8),glm::vec3(0,0,0),
                              glm::vec3(0,1,0));
    const auto center=viewportPickRay(vp,{640,360},size);
    check(center && near(center->origin.z,7.9f) &&
          near(center->direction.x,0.0f) &&
          near(center->direction.z,-1.0f),
          "Viewport unprojection failed at image center");
    check(!viewportPickRay(vp,{-1,10},size) &&
          !viewportPickRay(vp,{1280,10},size) &&
          !viewportPickRay(vp,{120,720},size) &&
          !viewportPickRay(vp,{120,10},{0,0}),
          "Out-of-range viewport pixels must be rejected");

    const Bounds cube{glm::vec3(-0.5f),glm::vec3(0.5f)};
    const auto first=intersectPickBounds(
        *center,cube,glm::mat4(1.0f));
    check(first && near(*first,7.4f),
          "Nearest face intersection distance wrong");

    glm::mat4 rotatedScaled=glm::translate(
        glm::mat4(1.0f),glm::vec3(0,0,-4));
    rotatedScaled=glm::rotate(rotatedScaled,glm::radians(37.0f),
                             glm::vec3(0,1,0));
    rotatedScaled=glm::scale(rotatedScaled,glm::vec3(3,2,0.8f));
    const auto transformedHit=intersectPickBounds(
        *center,cube,rotatedScaled);
    check(transformedHit && *transformedHit > *first,
          "Rotation/non-uniform scale transformed ray intersection failed");

    glm::mat4 behind=glm::translate(glm::mat4(1.0f),
                                    glm::vec3(0,0,13));
    check(!intersectPickBounds(*center,cube,behind),
          "Picking a collider behind near clipping plane is invalid");
    glm::mat4 singular(1.0f);
    singular[0][0]=0;
    check(!intersectPickBounds(*center,cube,singular),
          "Singular mesh transform must be skipped");
    const Bounds offAxis{glm::vec3(10,10,10),glm::vec3(11,11,11)};
    check(!intersectPickBounds(*center,offAxis,glm::mat4(1.0f)),
          "Off-axis object falsely selected");
    const Bounds flat{glm::vec3(-2,0,-2),glm::vec3(2,0,2)};
    const PickRay downward{{0,3,0},{0,-1,0}};
    check(near(*intersectPickBounds(downward,flat,glm::mat4(1.0f)),3.0f),
          "Zero-thickness ground plane cannot be picked");

    Scene scene;
    const Entity root=scene.createEntity("root");
    const Entity child=scene.createEntity("child");
    const Entity sibling=scene.createEntity("sibling");
    const Entity independent=scene.createEntity("independent");
    scene.setParent(child,root);
    SceneSelection selection;
    selection.pick(scene,root);
    check(selection.size()==1 && selection.primary()==root,
          "Single click selection failed");
    selection.pick(scene,child,true);
    selection.pick(scene,sibling,true);
    selection.pick(scene,independent,true);
    check(selection.size()==4 && selection.primary()==independent,
          "Multi-selection additive click failed");
    const auto roots=selection.roots(scene);
    check(roots.size()==3 && roots[0]==root &&
          roots[1]==sibling && roots[2]==independent,
          "Ancestor/child must not be transformed twice in group drag");
    selection.pick(scene,sibling,true);
    check(selection.size()==3 && !selection.contains(sibling) &&
          selection.primary()==independent,
          "Ctrl/Shift toggle failed");
    selection.pick(scene,{},true);
    check(selection.size()==3,"Empty additive click should preserve selection");
    selection.pick(scene,{},false);
    check(selection.size()==0 && !selection.primary(),
          "Empty viewport click must clear selection");

    selection.pick(scene,child);
    scene.destroyEntity(root);
    selection.prune(scene);
    check(selection.size()==0 && !selection.primary(),
          "Deleting parent must invalidate selected descendant");
    rejects([&]{selection.pick(scene,child);},
            "Stale handle should never be accepted for selection");

    const Entity reused=scene.createEntity("new child slot");
    check(reused.index==root.index && reused.generation!=root.generation,
          "Expected generation-stamped slot reuse");
    selection.pick(scene,reused);
    check(selection.size()==1 && selection.primary()==reused,
          "Selection must accept new generation handle");
    return 0;
}
