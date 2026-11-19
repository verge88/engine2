#pragma once

#include "engine2/Scene.hpp"

#include <array>
#include <string>

namespace engine2 {

enum class ArenaGameState {
    Playing,
    Won,
    Lost
};

struct ArenaGameInput {
    float moveX = 0.0f;
    float moveZ = 0.0f;
    bool boost = false;
    bool restart = false;
};

struct ArenaGameStats {
    int score = 0;
    int targetScore = 5;
    int lives = 3;
    ArenaGameState state = ArenaGameState::Playing;
    float elapsedSeconds = 0.0f;
};

class ArenaGame {
public:
    static constexpr std::size_t collectibleCount = 5;
    static constexpr std::size_t hazardCount = 2;
    static constexpr std::size_t wallCount = 4;

    ArenaGame(Scene& scene,
              const Mesh* sphereMesh,
              const Mesh* cubeMesh,
              const Mesh* floorMesh);

    void applyInput(Scene& scene, const ArenaGameInput& input, float deltaSeconds);
    void afterPhysics(Scene& scene, float deltaSeconds);
    void restart(Scene& scene);

    [[nodiscard]] const ArenaGameStats& stats() const { return stats_; }
    [[nodiscard]] Entity player() const { return player_; }
    [[nodiscard]] const std::array<Entity, collectibleCount>& collectibles() const {
        return collectibles_;
    }
    [[nodiscard]] const std::array<Entity, hazardCount>& hazards() const {
        return hazards_;
    }
    [[nodiscard]] std::string statusText() const;

private:
    void build(Scene& scene,
               const Mesh* sphereMesh,
               const Mesh* cubeMesh,
               const Mesh* floorMesh);
    void resetTransforms(Scene& scene);

    Entity floor_{};
    Entity player_{};
    std::array<Entity, collectibleCount> collectibles_{};
    std::array<Entity, hazardCount> hazards_{};
    std::array<Entity, wallCount> walls_{};
    ArenaGameStats stats_{};
    float damageCooldown_ = 0.0f;
    bool restartHeld_ = false;
};

} // namespace engine2
