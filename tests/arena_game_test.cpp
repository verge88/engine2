#include "engine2/ArenaGame.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <stdexcept>

namespace {
void require(bool okay,const char* message) {
    if (!okay) throw std::runtime_error(message);
}

glm::mat4 playerAt(glm::vec3 position) {
    glm::mat4 transform=glm::translate(glm::mat4(1.0f),position);
    return glm::scale(transform,glm::vec3(0.55f));
}

glm::vec3 positionOf(const engine2::Scene& scene,engine2::Entity entity) {
    return glm::vec3(scene.worldTransform(entity)[3]);
}
}

int main() {
    using namespace engine2;

    Scene scene;
    ArenaGame game(scene,nullptr,nullptr,nullptr);

    require(game.stats().state==ArenaGameState::Playing &&
            game.stats().score==0 &&
            game.stats().targetScore==5 &&
            game.stats().lives==3,
            "Orb Arena initial state is incorrect");
    require(scene.stats().entities==13,
            "Orb Arena should create floor, player, pickups, hazards and walls");

    ArenaGameInput right;
    right.moveX=1.0f;
    game.applyInput(scene,right,0.1f);
    const glm::vec3 steered=scene.rigidBody(game.player()).velocity;
    require(steered.x>2.9f && steered.x<3.1f &&
            std::abs(steered.z)<0.001f,
            "Arena input acceleration is not deterministic");

    // Teleport onto every pickup. The gameplay layer should disable collected
    // entities, count them once, and transition to Won at the fifth core.
    for (std::size_t i=0;i<game.collectibles().size();++i) {
        const glm::vec3 core=positionOf(scene,game.collectibles()[i]);
        scene.setLocalTransform(game.player(),playerAt({core.x,0.55f,core.z}));
        game.afterPhysics(scene,0.016f);
        require(game.stats().score==static_cast<int>(i+1),
                "Pickup collection did not increment score exactly once");
        require(!scene.enabled(game.collectibles()[i]),
                "Collected energy core stayed enabled");
    }
    require(game.stats().state==ArenaGameState::Won,
            "Collecting every core did not win the game");
    require(glm::length(scene.rigidBody(game.player()).velocity)<0.001f,
            "Winning should stop player movement");

    // Restart is edge-triggered and restores every pickup, score and life.
    ArenaGameInput restart;
    restart.restart=true;
    game.applyInput(scene,restart,0.016f);
    require(game.stats().state==ArenaGameState::Playing &&
            game.stats().score==0 && game.stats().lives==3,
            "Restart did not restore the game state");
    for (Entity core:game.collectibles())
        require(scene.enabled(core),"Restart did not restore a collected core");

    // A hunter collision removes one life and respawns the player. The damage
    // cooldown prevents repeated loss until enough game time has elapsed.
    const glm::vec3 hunter=positionOf(scene,game.hazards()[0]);
    scene.setLocalTransform(game.player(),playerAt(hunter));
    game.afterPhysics(scene,0.016f);
    require(game.stats().lives==2,
            "Hunter contact did not remove one life");
    game.afterPhysics(scene,0.016f);
    require(game.stats().lives==2,
            "Damage cooldown did not suppress repeated contact damage");

    // Lose the remaining lives after expiring cooldown between deliberate hits.
    for (int expected=1;expected>=0;--expected) {
        for (int i=0;i<4;++i) game.afterPhysics(scene,0.25f);
        const glm::vec3 currentHunter=positionOf(scene,game.hazards()[0]);
        scene.setLocalTransform(game.player(),playerAt(currentHunter));
        game.afterPhysics(scene,0.016f);
        require(game.stats().lives==expected,
                "Hunter collision did not decrement the expected life");
    }
    require(game.stats().state==ArenaGameState::Lost,
            "Zero lives did not transition to Game Over");
    require(game.statusText().find("GAME OVER")!=std::string::npos,
            "Game-over HUD text is missing");

    // Release/repress restart to prove the latch behaves like a key edge.
    restart.restart=false;
    game.applyInput(scene,restart,0.016f);
    restart.restart=true;
    game.applyInput(scene,restart,0.016f);
    require(game.stats().state==ArenaGameState::Playing &&
            game.stats().lives==3 && game.stats().score==0,
            "Restart after Game Over failed");

    return 0;
}
