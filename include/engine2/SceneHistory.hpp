#pragma once

#include "engine2/Scene.hpp"

#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace engine2 {

// In-memory, bounded editor history. Snapshots preserve generation-stamped
// entity handles, parent relations, complete component data and asset refs.
// It is intentionally NOT a runtime save file or a persistent undo journal.
class SceneHistory {
public:
    explicit SceneHistory(std::size_t capacity = 64) : capacity_(capacity) {
        if (capacity_ == 0 || capacity_ > 1024)
            throw std::invalid_argument("Scene history capacity must be 1..1024");
    }

    [[nodiscard]] bool canUndo() const { return !undo_.empty(); }
    [[nodiscard]] bool canRedo() const { return !redo_.empty(); }
    [[nodiscard]] std::size_t undoCount() const { return undo_.size(); }
    [[nodiscard]] std::size_t redoCount() const { return redo_.size(); }

    // A checkpoint is the state BEFORE the edit. One checkpoint per drag
    // gesture gives one undo operation regardless of the number of frames.
    void checkpoint(const Scene& before) {
        push(undo_, before);
        redo_.clear();
    }

    // Execute a validated edit against a detached copy. If it throws, the
    // original scene and both history stacks are completely unchanged.
    template<class Edit>
    void perform(Scene& scene, Edit&& edit) {
        Scene next = scene;
        std::forward<Edit>(edit)(next);
        checkpoint(scene);
        scene = std::move(next);
    }

    bool undo(Scene& scene) {
        if (undo_.empty()) return false;
        push(redo_, scene);
        scene = std::move(undo_.back());
        undo_.pop_back();
        return true;
    }

    bool redo(Scene& scene) {
        if (redo_.empty()) return false;
        push(undo_, scene);
        scene = std::move(redo_.back());
        redo_.pop_back();
        return true;
    }

    void clear() {
        undo_.clear();
        redo_.clear();
    }

private:
    void push(std::vector<Scene>& stack, const Scene& value) {
        if (stack.size() >= capacity_) stack.erase(stack.begin());
        stack.push_back(value);
    }

    std::size_t capacity_ = 64;
    std::vector<Scene> undo_;
    std::vector<Scene> redo_;
};

} // namespace engine2
