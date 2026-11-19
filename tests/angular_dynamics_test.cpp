#include "engine2/SceneIO.hpp"

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <stdexcept>
#include <string>

namespace {
void require(bool okay,const char* message) {
    if (!okay) throw std::runtime_error(message);
}
bool near(float a,float b,float epsilon=0.002f) {
    return std::abs(a-b)<epsilon;
}
void expectFailure(auto&& operation,const char* message) {
    bool rejected=false;
    try { operation(); } catch (const std::exception&) { rejected=true; }
    require(rejected,message);
}
void eraseJsonArrayField(std::string& json,const std::string& field) {
    const std::string key="\""+field+"\"";
    const std::size_t keyPos=json.find(key);
    if (keyPos==std::string::npos)
        throw std::runtime_error("Serialized JSON field missing");
    const std::size_t lineStart=json.rfind('\n',keyPos);
    const std::size_t arrayStart=json.find('[',keyPos);
    const std::size_t arrayEnd=json.find(']',arrayStart);
    if (arrayStart==std::string::npos || arrayEnd==std::string::npos)
        throw std::runtime_error("Serialized JSON array malformed");
    std::size_t lineEnd=json.find('\n',arrayEnd);
    if (lineEnd==std::string::npos) lineEnd=json.size()-1;
    json.erase(lineStart==std::string::npos ? 0 : lineStart+1,
               lineEnd-(lineStart==std::string::npos ? 0 : lineStart+1)+1);
}
}

int main() {
    using namespace engine2;

    Scene scene;
    const Entity spinner=scene.createEntity("Spinning sphere");
    glm::mat4 transform=glm::translate(
        glm::mat4(1.0f),glm::vec3(3.0f,4.0f,5.0f));
    transform=glm::scale(transform,glm::vec3(2.0f,3.0f,4.0f));
    scene.setLocalTransform(spinner,transform);

    RigidBodyComponent body;
    body.shape=ColliderShape::Sphere;
    body.radius=0.5f;
    body.gravity=false;
    body.groundHeight=-100.0f;
    body.velocity={1.0f,0.0f,0.0f};
    body.angularVelocity={0.0f,0.0f,glm::pi<float>()};
    scene.setRigidBody(spinner,body);

    // 30 fixed steps = 0.5 seconds. pi rad/s therefore rotates the visual
    // basis by +90 degrees around world Z while linear motion advances +0.5 X.
    for (int i=0;i<30;++i)
        (void)scene.advance(Scene::fixedDeltaSeconds);

    const glm::mat4 world=scene.worldTransform(spinner);
    const glm::vec3 position(world[3]);
    require(near(position.x,3.5f) && near(position.y,4.0f) &&
            near(position.z,5.0f),
            "Angular integration changed the linear trajectory");
    const glm::vec3 basisX(world[0]);
    const glm::vec3 basisY(world[1]);
    const glm::vec3 basisZ(world[2]);
    require(near(basisX.x,0.0f,0.004f) && near(basisX.y,2.0f,0.004f) &&
            near(basisY.x,-3.0f,0.004f) && near(basisY.y,0.0f,0.004f),
            "Sphere angular velocity did not integrate to a 90 degree rotation");
    require(near(glm::length(basisX),2.0f) &&
            near(glm::length(basisY),3.0f) &&
            near(glm::length(basisZ),4.0f),
            "Angular integration must preserve authored visual scale");
    require(near(scene.rigidBody(spinner).angularVelocity.z,glm::pi<float>()),
            "Free-spin angular velocity changed without torque/damping");

    SceneAssetRegistry assets;
    const std::string serialized=serializeScene(scene,assets);
    Scene restored=deserializeScene(serialized,assets);
    const Entity restoredSpinner=restored.entities().at(0);
    require(near(restored.rigidBody(restoredSpinner).angularVelocity.z,
                 glm::pi<float>()),
            "Angular velocity was not restored from scene JSON");

    // v1 files saved before angular dynamics have no field and must load with
    // exactly zero spin rather than requiring a schema version bump.
    std::string legacy=serialized;
    eraseJsonArrayField(legacy,"angularVelocity");
    Scene old=deserializeScene(legacy,assets);
    require(glm::length(old.rigidBody(old.entities().at(0)).angularVelocity)
                <1.0e-7f,
            "Legacy scene without angularVelocity did not default to zero");

    // Rotating AABBs would visually diverge from their axis-aligned collider,
    // so reject them until OBB support exists. Static bodies do not integrate.
    {
        Scene invalid;
        const Entity box=invalid.createEntity("box");
        RigidBodyComponent spinningBox;
        spinningBox.shape=ColliderShape::Box;
        spinningBox.angularVelocity={0,1,0};
        expectFailure([&] { invalid.setRigidBody(box,spinningBox); },
                      "AABB angular velocity must be rejected before OBB support");
    }
    {
        Scene invalid;
        const Entity fixed=invalid.createEntity("static sphere");
        RigidBodyComponent spinningStatic;
        spinningStatic.shape=ColliderShape::Sphere;
        spinningStatic.isStatic=true;
        spinningStatic.angularVelocity={0,1,0};
        expectFailure([&] { invalid.setRigidBody(fixed,spinningStatic); },
                      "Static body angular velocity must be rejected");
    }

    return 0;
}
