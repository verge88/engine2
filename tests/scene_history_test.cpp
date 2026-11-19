#include "engine2/SceneHistory.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <stdexcept>
#include <string>

namespace {
void check(bool value, const char* error) {
    if (!value) throw std::runtime_error(error);
}
float x(const engine2::Scene& scene, engine2::Entity id) {
    return scene.worldTransform(id)[3][0];
}
bool near(float a, float b) { return std::abs(a-b) < 0.00001f; }
} // namespace

int main() {
    using namespace engine2;
    Scene scene;
    const Entity parent = scene.createEntity("parent");
    const Entity child = scene.createEntity("child");
    const Entity light = scene.createEntity("light");
    scene.setParent(child, parent);
    SceneHistory history(3);
    check(!history.canUndo() && !history.undo(scene) && !history.redo(scene),
          "Empty history must not accept undo/redo");

    history.perform(scene,[&](Scene& state) {
        state.setLocalTransform(parent,
            glm::translate(glm::mat4(1.0f), glm::vec3(4,0,0)));
        state.setName(parent,"edited parent");
    });
    check(history.undoCount()==1 && !history.canRedo() &&
          near(x(scene,child),4.0f) && scene.name(parent)=="edited parent",
          "Hierarchy transform edit incorrect");

    history.perform(scene,[&](Scene& state) {
        state.setEnabled(child,false);
        PointLight lamp;
        lamp.intensity=44.0f;
        state.setPointLight(light,lamp);
    });
    check(history.undoCount()==2 && !scene.enabled(child) &&
          scene.pointLightComponent(light)->intensity==44,
          "Component history mutation incorrect");

    check(history.undo(scene) && scene.enabled(child) &&
          scene.pointLightComponent(light)==nullptr &&
          history.redoCount()==1, "Undo did not recover prior components");
    check(history.undo(scene) && near(x(scene,child),0) &&
          scene.name(parent)=="parent", "Undo did not restore transforms");
    check(history.redo(scene) && near(x(scene,child),4) &&
          history.canUndo(), "Redo failed");
    history.perform(scene,[&](Scene& state) {
        state.destroyEntity(child);
    });
    check(!scene.contains(child) && !history.canRedo(),
          "New edits must invalidate redo stack");
    check(history.undo(scene) && scene.contains(child) &&
          scene.parent(child)==parent,
          "Undo deletion must recover entity handle and hierarchy");
    check(history.redo(scene) && !scene.contains(child),
          "Redo deletion must remove entity");
    check(history.undo(scene) && scene.contains(child),
          "Undo should recover entity after redo");
    const std::size_t undoBefore = history.undoCount();
    bool rejected = false;
    try {
        history.perform(scene,[&](Scene& state) {
            state.setName(parent,"SHOULD NOT COMMIT");
            state.setParent(parent,child); // cycle
        });
    } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected && scene.name(parent)=="edited parent" &&
          history.undoCount()==undoBefore, "Failed transaction mutated history");

    // A continuous axis drag checkpoints only once before incremental edits.
    const Scene beforeDrag = scene;
    for (int i=0;i<10;++i) {
        if (i==0) history.checkpoint(beforeDrag);
        auto mat=scene.localTransform(parent);
        mat[3].x += 0.1f;
        scene.setLocalTransform(parent,mat);
    }
    check(near(x(scene,child),5.0f) &&
          history.undo(scene) && near(x(scene,child),4.0f),
          "Drag gesture must undo all intermediate frames in one step");

    history.clear();
    check(!history.canUndo() && !history.canRedo(),
          "Clearing editor history must empty both stacks");

    // Capacity discards the oldest snapshot, not the newest action.
    SceneHistory bounded(2);
    for(int i=0;i<4;++i) {
        bounded.perform(scene,[&](Scene& state) {
            state.setName(parent, "step "+std::to_string(i));
        });
    }
    check(bounded.undoCount()==2 && bounded.undo(scene) &&
          scene.name(parent)=="step 2", "Bounded history first undo failed");
    check(bounded.undo(scene) && scene.name(parent)=="step 1" &&
          !bounded.undo(scene), "Bounded history discarded wrong snapshot");
    check(bounded.redo(scene) && scene.name(parent)=="step 2" &&
          bounded.redo(scene) && scene.name(parent)=="step 3",
          "Bounded redo stack incorrect");
    return 0;
}
