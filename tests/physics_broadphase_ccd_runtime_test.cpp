#include "engine2/Scene.hpp"
#include "engine2/SceneIO.hpp"
#include "engine2/SceneInspector.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <stdexcept>
#include <string>

namespace {
void check(bool ok,const char* message) {
    if (!ok) throw std::runtime_error(message);
}
glm::mat4 at(float x,float y=0,float z=0) {
    return glm::translate(glm::mat4(1.0f),glm::vec3(x,y,z));
}
float x(const engine2::Scene& scene,engine2::Entity entity) {
    return scene.worldTransform(entity)[3][0];
}
struct Pair {
    engine2::Scene scene;
    engine2::Entity obstacle;
    engine2::Entity sphere;
};
Pair makeWorld(engine2::ColliderShape shape,bool ccd,
               float speed=300.0f) {
    using namespace engine2;
    Pair result;
    result.obstacle=result.scene.createEntity("Static obstacle");
    result.sphere=result.scene.createEntity("Fast sphere");
    result.scene.setLocalTransform(result.obstacle,at(0,2,0));
    result.scene.setLocalTransform(result.sphere,at(-2,2,0));
    RigidBodyComponent fixed;
    fixed.shape=shape;
    fixed.isStatic=true;
    fixed.gravity=false;
    fixed.radius=0.1f;
    fixed.halfExtents={0.1f,0.5f,0.5f};
    fixed.restitution=0.8f;
    result.scene.setRigidBody(result.obstacle,fixed);
    RigidBodyComponent ball;
    ball.radius=0.2f;
    ball.gravity=false;
    ball.groundHeight=-100.0f;
    ball.velocity={speed,0,0};
    ball.restitution=0.8f;
    ball.continuous=ccd;
    result.scene.setRigidBody(result.sphere,ball);
    return result;
}
void step(engine2::Scene& scene) {
    (void)scene.advance(engine2::Scene::fixedDeltaSeconds);
}
}

int main() {
    using namespace engine2;
    {
        auto discrete=makeWorld(ColliderShape::Box,false);
        auto continuous=makeWorld(ColliderShape::Box,true);
        step(discrete.scene);
        step(continuous.scene);
        check(x(discrete.scene,discrete.sphere)>2.95f &&
              discrete.scene.stats().ccdHits==0,
              "Discrete sphere should tunnel across thin static AABB in one step");
        check(x(continuous.scene,continuous.sphere)<-0.3f &&
              continuous.scene.rigidBody(continuous.sphere).velocity.x < -239.0f &&
              continuous.scene.stats().ccdHits==1,
              "Opt-in swept sphere CCD did not prevent tunnelling through static AABB");
        check(continuous.scene.stats().broadphasePairs>=1 &&
              continuous.scene.stats().broadphaseCells>=1 &&
              continuous.scene.stats().solverPasses==8,
              "Spatial-grid broadphase / CCD / solver telemetry missing");
        check(std::abs(x(continuous.scene,continuous.obstacle))<0.0001f,
              "Static collision body moved during CCD");
    }
    {
        auto caseSphere=makeWorld(ColliderShape::Sphere,true);
        step(caseSphere.scene);
        check(caseSphere.scene.stats().ccdHits==1 &&
              caseSphere.scene.rigidBody(caseSphere.sphere).velocity.x<-239.0f &&
              x(caseSphere.scene,caseSphere.sphere)<-0.3f,
              "Analytic swept sphere-sphere collision failed");
    }
    {
        auto earliest=makeWorld(ColliderShape::Box,true,420.0f);
        const Entity second=earliest.scene.createEntity("Further obstacle");
        earliest.scene.setLocalTransform(second,at(2,2,0));
        RigidBodyComponent box=*earliest.scene.rigidBodyComponent(earliest.obstacle);
        earliest.scene.setRigidBody(second,box);
        step(earliest.scene);
        check(earliest.scene.stats().ccdHits==1 &&
              earliest.scene.rigidBody(earliest.sphere).velocity.x<-335.0f &&
              x(earliest.scene,earliest.sphere)<-0.3f,
              "CCD must prefer nearest obstacle along the sweep");
    }
    {
        // Same X projection, but widely separated in 3D. The old X-only
        // sweep examined all ~20k pairs; the new grid rejects all of them.
        Scene stress;
        for (int i=0;i<200;++i) {
            const Entity e=stress.createEntity();
            stress.setLocalTransform(e,at(0,10.0f+7.0f*(i%20),7.0f*(i/20)));
            RigidBodyComponent body;
            body.gravity=false;
            body.groundHeight=-100.0f;
            stress.setRigidBody(e,body);
        }
        step(stress);
        check(stress.stats().broadphasePairs==0 &&
              stress.stats().broadphaseTests==0 &&
              stress.stats().broadphaseCells>0,
              "Spatial grid did not reject sparse 3D scene");
    }
    {
        auto source=makeWorld(ColliderShape::Box,true);
        SceneAssetRegistry assets;
        const std::string data=serializeScene(source.scene,assets);
        const Scene restored=deserializeScene(data,assets);
        const Entity e=restored.entities()[1];
        check(restored.rigidBody(e).continuous &&
              serializeScene(restored,assets)==data,
              "CCD flag not persisted by Scene JSON v1");

        // v1.x scene files do not contain the new optional boolean.
        std::string legacy=data;
        const std::string marker="\"continuous\": ";
        for (int i=0;i<2;++i) {
            const std::size_t location=legacy.find(marker);
            check(location!=std::string::npos,"Scene JSON missing continuous flag");
            const std::size_t lineStart=legacy.rfind('\n',location);
            const std::size_t lineEnd=legacy.find('\n',location);
            legacy.erase(lineStart+1,lineEnd-lineStart);
        }
        const Scene compatible=deserializeScene(legacy,assets);
        for (Entity handle:compatible.entities())
            check(!compatible.rigidBody(handle).continuous,
                  "Legacy JSON without CCD flag must use discrete collision");

        Scene editable=makeWorld(ColliderShape::Box,false).scene;
        const std::string patch=R"({"format":"engine2.scene-edits","version":1,"operations":[{"op":"set-body-ccd","entity":1,"continuous":true}]})";
        check(applySceneEdits(editable,patch)==1 &&
              editable.rigidBody(editable.entities()[1]).continuous,
              "Atomic scene-editor CCD toggle not applied");
        check(inspectScene(editable,assets).find("\"continuous\": true")!=std::string::npos,
              "Inspector must report CCD status");
    }
    return 0;
}
