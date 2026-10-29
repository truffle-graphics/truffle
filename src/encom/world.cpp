#include "truffle/encom/world.hpp"

namespace truffle::encom {

Entity World::create() {
    if (!freeIndices_.empty()) {
        const auto index = freeIndices_.back();
        freeIndices_.pop_back();
        alive_[index] = true;
        return Entity{index, generations_[index]};
    }

    const auto index = static_cast<std::uint32_t>(generations_.size());
    generations_.push_back(0);
    alive_.push_back(true);
    return Entity{index, 0};
}

bool World::alive(Entity entity) const noexcept {
    return entity.valid() && entity.index < alive_.size() &&
           alive_[entity.index] && generations_[entity.index] == entity.generation;
}

base::Status World::destroy(Entity entity) {
    if (!alive(entity)) {
        return base::Status::failure(base::StatusCode::invalid_argument,
                                     "entity is not alive");
    }

    alive_[entity.index] = false;
    ++generations_[entity.index];
    freeIndices_.push_back(entity.index);
    for (auto& [_, storage] : storages_) {
        storage->erase(entity.index);
    }
    return base::Status::success();
}

void World::run(const std::vector<System>& systems, double delta_seconds) {
    for (const auto& system : systems) {
        system(*this, delta_seconds);
    }
}

} // namespace truffle::encom
