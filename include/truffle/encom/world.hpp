#pragma once

#include "truffle/base/status.hpp"
#include "truffle/encom/entity.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace truffle::encom {

class World {
public:
    using System = std::function<void(World&, double)>;

    [[nodiscard]] Entity create();
    [[nodiscard]] bool alive(Entity entity) const noexcept;
    [[nodiscard]] base::Status destroy(Entity entity);

    template <typename T, typename... Args>
    T& emplace(Entity entity, Args&&... args) {
        if (!alive(entity)) {
            throw std::invalid_argument("components require a live entity");
        }
        auto& storage = assure_storage<T>();
        auto [it, inserted] = storage.values.try_emplace(
            entity.index, std::forward<Args>(args)...);
        if (!inserted) {
            it->second = T(std::forward<Args>(args)...);
        }
        return it->second;
    }

    template <typename T>
    [[nodiscard]] bool has(Entity entity) const {
        const auto* storage = find_storage<T>();
        return alive(entity) && storage != nullptr &&
               storage->values.contains(entity.index);
    }

    template <typename T>
    [[nodiscard]] T* get(Entity entity) {
        auto* storage = find_storage<T>();
        if (!alive(entity) || storage == nullptr) {
            return nullptr;
        }
        const auto it = storage->values.find(entity.index);
        return it == storage->values.end() ? nullptr : &it->second;
    }

    template <typename T>
    [[nodiscard]] const T* get(Entity entity) const {
        const auto* storage = find_storage<T>();
        if (!alive(entity) || storage == nullptr) {
            return nullptr;
        }
        const auto it = storage->values.find(entity.index);
        return it == storage->values.end() ? nullptr : &it->second;
    }

    template <typename T>
    [[nodiscard]] base::Status remove(Entity entity) {
        auto* storage = find_storage<T>();
        if (!alive(entity)) {
            return base::Status::failure(base::StatusCode::invalid_argument,
                                         "entity is not alive");
        }
        if (storage == nullptr || storage->values.erase(entity.index) == 0) {
            return base::Status::failure(base::StatusCode::unavailable,
                                         "component is not present");
        }
        return base::Status::success();
    }

    template <typename First, typename... Rest, typename Fn>
    void each(Fn&& fn) {
        auto* primary = find_storage<First>();
        if (primary == nullptr) {
            return;
        }

        for (auto& [index, first] : primary->values) {
            const Entity entity{index, generations_[index]};
            if (alive(entity) && (has<Rest>(entity) && ...)) {
                std::invoke(fn, entity, first, *get<Rest>(entity)...);
            }
        }
    }

    void run(const std::vector<System>& systems, double delta_seconds);

private:
    struct IStorage {
        virtual ~IStorage() = default;
        virtual void erase(std::uint32_t entity_index) = 0;
    };

    template <typename T>
    struct Storage final : IStorage {
        std::unordered_map<std::uint32_t, T> values;

        void erase(std::uint32_t entity_index) override {
            values.erase(entity_index);
        }
    };

    template <typename T>
    [[nodiscard]] Storage<T>& assure_storage() {
        const auto key = std::type_index(typeid(T));
        const auto [it, inserted] =
            storages_.try_emplace(key, std::make_unique<Storage<T>>());
        return *static_cast<Storage<T>*>(it->second.get());
    }

    template <typename T>
    [[nodiscard]] Storage<T>* find_storage() {
        const auto it = storages_.find(std::type_index(typeid(T)));
        return it == storages_.end() ? nullptr
                                     : static_cast<Storage<T>*>(it->second.get());
    }

    template <typename T>
    [[nodiscard]] const Storage<T>* find_storage() const {
        const auto it = storages_.find(std::type_index(typeid(T)));
        return it == storages_.end()
                   ? nullptr
                   : static_cast<const Storage<T>*>(it->second.get());
    }

    std::vector<std::uint32_t> generations_;
    std::vector<bool> alive_;
    std::vector<std::uint32_t> freeIndices_;
    std::unordered_map<std::type_index, std::unique_ptr<IStorage>> storages_;
};

} // namespace truffle::encom
