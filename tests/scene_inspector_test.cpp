#include "engine2/SceneInspector.hpp"

#include <json.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {
using Json = nlohmann::json;

void check(bool value, const char* error) {
    if (!value) throw std::runtime_error(error);
}

template<typename F>
void mustFail(F&& fn, const char* error) {
    bool rejected = false;
    try { fn(); } catch (const std::exception&) { rejected = true; }
    check(rejected, error);
}

glm::vec3 location(const engine2::Scene& scene, engine2::Entity entity) {
    return glm::vec3(scene.worldTransform(entity)[3]);
}

bool near(float a, float b, float eps=0.0003f) {
    return std::abs(a-b)<eps;
}

} // namespace

int main() {
    using namespace engine2;

    Scene scene;
    const Entity root=scene.createEntity("Root");
    const Entity child=scene.createEntity("Child");
    const Entity light=scene.createEntity("Lamp");
    const Entity body=scene.createEntity("Body");
    const Entity doomed=scene.createEntity("Temporary");

    glm::mat4 rootModel=glm::translate(glm::mat4(1.0f),glm::vec3(5,0,0));
    rootModel=glm::scale(rootModel,glm::vec3(2.0f));
    scene.setLocalTransform(root,rootModel);
    scene.setLocalTransform(child,glm::translate(glm::mat4(1.0f),glm::vec3(1,2,0)));
    scene.setParent(child,root);
    scene.setLocalTransform(light,glm::translate(glm::mat4(1.0f),glm::vec3(0,0,4)));
    MeshRendererComponent renderer;
    renderer.material.albedo={0.8f,0.6f,0.2f};
    scene.setRenderer(child,renderer);
    PointLight source;
    source.intensity=18.0f;
    scene.setPointLight(light,source);
    RigidBodyComponent rigid;
    rigid.gravity=false;
    scene.setRigidBody(body,rigid);
    SceneAssetRegistry assets;

    Json before=Json::parse(inspectScene(scene,assets));
    check(before["format"]=="engine2.scene-inspection" && before["version"]==1,
          "Inspector schema/version invalid");
    check(before["summary"]["entities"]==5 &&
          before["summary"]["renderables"]==0 &&
          before["summary"]["lights"]==1 &&
          before["summary"]["rigidBodies"]==1,
          "Scene inspector summary incorrect");
    check(before["entities"][1]["parent"]==0 &&
          before["entities"][1]["depth"]==1 &&
          near(before["entities"][1]["worldPosition"][0].get<float>(),7.0f) &&
          near(before["entities"][1]["worldPosition"][1].get<float>(),4.0f),
          "Inspector hierarchy and transformed position incorrect");
    check(before["entities"][1]["renderer"]["mesh"].is_null(),
          "Null renderer mesh must remain null in inspector");

    check(before["entities"][3]["rigidBody"]["angularVelocity"][0]==0.0f,
          "Inspector must expose default angular velocity");

    const std::string edits=R"json({
      "format":"engine2.scene-edits", "version":1,
      "operations":[
        {"op":"rename","entity":0,"name":"Edited root"},
        {"op":"set-position","entity":1,"space":"world","position":[9,6,0]},
        {"op":"set-albedo","entity":1,"color":[0.05,0.9,0.15]},
        {"op":"set-enabled","entity":0,"enabled":false},
        {"op":"set-light-intensity","entity":2,"intensity":47.5},
        {"op":"set-body-velocity","entity":3,"velocity":[-2,3.5,1]},
        {"op":"set-body-angular-velocity","entity":3,
         "angularVelocity":[0,1.25,-0.5]},
        {"op":"reparent","entity":1,"parent":2,"keepWorld":true},
        {"op":"delete","entity":4}
      ]
    })json";
    check(applySceneEdits(scene,edits)==9,"Scene editor lost operations");
    check(scene.name(root)=="Edited root" && scene.stats().entities==4 &&
          !scene.contains(doomed),"Rename/delete failed");
    check(scene.parent(child)==light &&
          near(location(scene,child).x,9.0f) &&
          near(location(scene,child).y,6.0f),
          "World-position edit / keep-world reparent failed");
    check(near(scene.localTransform(child)[3][0],9.0f) &&
          near(scene.localTransform(child)[3][2],-4.0f),
          "Local position of reparented node incorrect");
    check(!scene.enabled(root) &&
          near(scene.rendererComponent(child)->material.albedo.y,0.9f) &&
          near(scene.pointLightComponent(light)->intensity,47.5f) &&
          near(scene.rigidBody(body).velocity.y,3.5f) &&
          near(scene.rigidBody(body).angularVelocity.y,1.25f) &&
          near(scene.rigidBody(body).angularVelocity.z,-0.5f),
          "Component or enabled editing failed");
    Json after=Json::parse(inspectScene(scene,assets));
    check(after["entities"].size()==4 &&
          after["entities"][1]["effectiveEnabled"]==true &&
          after["entities"][0]["enabled"]==false &&
          after["entities"][1]["parent"]==2 &&
          after["entities"][1]["depth"]==1,
          "Inspector did not reflect world hierarchy or inherited visibility");
    check(after["entities"][1]["renderer"]["albedo"][1]==0.9f &&
          after["entities"][2]["pointLight"]["intensity"]==47.5f &&
          near(after["entities"][3]["rigidBody"]["angularVelocity"][1].get<float>(),
               1.25f),
          "Inspector did not include editable component values");

    // An invalid operation *after* valid changes must roll back everything.
    const std::string canonical=serializeScene(scene,assets);
    mustFail([&] {
        (void)applySceneEdits(scene,R"json({
          "format":"engine2.scene-edits","version":1,
          "operations":[
             {"op":"rename","entity":0,"name":"Partial update"},
             {"op":"set-body-velocity","entity":1,"velocity":[0,1,0]}
          ]})json");
    },"Invalid later operation not rejected");
    check(serializeScene(scene,assets)==canonical,"Scene edits must be atomic");

    mustFail([&] {
        (void)applySceneEdits(scene,R"json({
          "format":"engine2.scene-edits","version":1,
          "operations":[{"op":"reparent","entity":2,"parent":1}]})json");
    },"Hierarchy cycles were accepted");
    mustFail([&] {
        (void)applySceneEdits(scene,R"json({
          "format":"engine2.scene-edits","version":1,
          "operations":[{"op":"set-position","entity":1,
                         "space":"galactic","position":[0,0,0]}]})json");
    },"Unknown position space accepted");
    mustFail([&] {
        (void)applySceneEdits(scene,R"json({
          "format":"engine2.scene-edits","version":1,
          "operations":[{"op":"set-albedo","entity":1,"color":[-1,0,0]}]})json");
    },"Negative albedo accepted");
    mustFail([&] {
        (void)applySceneEdits(scene,R"json({
          "format":"engine2.scene-edits","version":1,
          "operations":[{"op":"set-enabled","entity":99,"enabled":true}]})json");
    },"Out-of-range index accepted");
    mustFail([&] {
        (void)applySceneEdits(scene,R"json({
          "format":"engine2.scene-edits","version":17,"operations":[]})json");
    },"Future scene-edit schema accepted");
    mustFail([&] {
        (void)applySceneEdits(scene,R"json({
          "format":"engine2.scene-edits","version":1,
          "operations":[
             {"op":"delete","entity":2},
             {"op":"rename","entity":1,"name":"Deleted child"}
          ]})json");
    },"Reference to a deleted child accepted");
    check(serializeScene(scene,assets)==canonical,
          "Invalid edits mutated the original scene");

    // File entry points, rather than only the in-memory functions.
    const auto editFile=std::filesystem::current_path() / "engine2-inspector-edit.json";
    const auto reportFile=std::filesystem::current_path() / "engine2-inspector-report.json";
    {
        std::ofstream output(editFile);
        output << R"json({"format":"engine2.scene-edits","version":1,
                          "operations":[{"op":"rename","entity":3,
                          "name":"Edited body"}]})json";
    }
    check(applySceneEditsFile(scene,editFile)==1 &&
          scene.name(body)=="Edited body","Editing a JSON file failed");
    saveSceneInspection(scene,assets,reportFile);
    std::ifstream report(reportFile);
    Json parsed=Json::parse(report);
    check(parsed["entities"][3]["name"]=="Edited body",
          "Inspector JSON file was not correctly generated");
    std::filesystem::remove(editFile);
    std::filesystem::remove(reportFile);

    return 0;
}
