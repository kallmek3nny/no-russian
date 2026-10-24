#pragma once
// TEntity ECS (Section 15): generational entity handles, sparse-set component
// pools, and stable save identities that survive save/reload.
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lt::ecs {

struct Entity {
    std::uint32_t index = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t generation = 0;
    friend bool operator==(const Entity&, const Entity&) = default;
};
inline constexpr Entity kNullEntity{};

// Persistent identity written to saves; 0 is reserved for "none".
using SaveId = std::uint64_t;

class IPool {
public:
    virtual ~IPool() = default;
    virtual void remove(std::uint32_t index) = 0;
    virtual bool has(std::uint32_t index) const = 0;
};

template <class T>
class Pool final : public IPool {
public:
    template <class... Args>
    T& emplace(std::uint32_t idx, Args&&... args) {
        if (idx >= sparse_.size()) sparse_.resize(static_cast<std::size_t>(idx) + 1, kEmpty);
        if (sparse_[idx] != kEmpty) {
            dense_[sparse_[idx]] = T{std::forward<Args>(args)...};
            return dense_[sparse_[idx]];
        }
        sparse_[idx] = static_cast<std::uint32_t>(dense_.size());
        owners_.push_back(idx);
        dense_.push_back(T{std::forward<Args>(args)...});
        return dense_.back();
    }

    bool has(std::uint32_t idx) const override {
        return idx < sparse_.size() && sparse_[idx] != kEmpty;
    }

    T* get(std::uint32_t idx) { return has(idx) ? &dense_[sparse_[idx]] : nullptr; }

    // Swap-and-pop keeps storage dense.
    void remove(std::uint32_t idx) override {
        if (!has(idx)) return;
        const std::uint32_t slot = sparse_[idx];
        const std::uint32_t last = static_cast<std::uint32_t>(dense_.size() - 1);
        if (slot != last) {
            dense_[slot] = std::move(dense_[last]);
            owners_[slot] = owners_[last];
            sparse_[owners_[slot]] = slot;
        }
        dense_.pop_back();
        owners_.pop_back();
        sparse_[idx] = kEmpty;
    }

    std::size_t size() const noexcept { return dense_.size(); }
    const std::vector<std::uint32_t>& owners() const noexcept { return owners_; }

private:
    static constexpr std::uint32_t kEmpty = 0xFFFFFFFFu;
    std::vector<std::uint32_t> sparse_;
    std::vector<std::uint32_t> owners_;
    std::vector<T> dense_;
};

class Registry {
public:
    Entity create() { return createWithSaveId(nextSaveId_); }

    // Used when loading a save so entities keep their persistent identity.
    Entity createWithSaveId(SaveId id) {
        if (id == 0 || bySave_.count(id)) return kNullEntity;
        if (id >= nextSaveId_) nextSaveId_ = id + 1;
        std::uint32_t idx;
        if (!free_.empty()) {
            idx = free_.back();
            free_.pop_back();
        } else {
            idx = static_cast<std::uint32_t>(generations_.size());
            generations_.push_back(0);
            saveIds_.push_back(0);
        }
        saveIds_[idx] = id;
        bySave_[id] = idx;
        ++alive_;
        return {idx, generations_[idx]};
    }

    bool alive(Entity e) const noexcept {
        return e.index < generations_.size() && generations_[e.index] == e.generation &&
               saveIds_[e.index] != 0;
    }

    bool destroy(Entity e) {
        if (!alive(e)) return false;
        for (auto& [type, pool] : pools_) pool->remove(e.index);
        bySave_.erase(saveIds_[e.index]);
        saveIds_[e.index] = 0;
        ++generations_[e.index];
        free_.push_back(e.index);
        --alive_;
        return true;
    }

    SaveId saveId(Entity e) const noexcept { return alive(e) ? saveIds_[e.index] : 0; }

    Entity findBySaveId(SaveId id) const noexcept {
        auto it = bySave_.find(id);
        if (it == bySave_.end()) return kNullEntity;
        return {it->second, generations_[it->second]};
    }

    std::size_t aliveCount() const noexcept { return alive_; }

    template <class T, class... Args>
    T* emplace(Entity e, Args&&... args) {
        if (!alive(e)) return nullptr;
        return &pool<T>().emplace(e.index, std::forward<Args>(args)...);
    }

    template <class T>
    T* get(Entity e) {
        return alive(e) ? pool<T>().get(e.index) : nullptr;
    }

    template <class T>
    bool has(Entity e) {
        return alive(e) && pool<T>().has(e.index);
    }

    template <class T>
    void remove(Entity e) {
        if (alive(e)) pool<T>().remove(e.index);
    }

    // Visits every entity owning T. Iterates a snapshot so fn may add/remove components.
    template <class T, class Fn>
    void each(Fn&& fn) {
        auto& p = pool<T>();
        const std::vector<std::uint32_t> owners = p.owners();
        for (std::uint32_t idx : owners) {
            if (T* c = p.get(idx)) fn(Entity{idx, generations_[idx]}, *c);
        }
    }

private:
    template <class T>
    Pool<T>& pool() {
        auto& slot = pools_[std::type_index(typeid(T))];
        if (!slot) slot = std::make_unique<Pool<T>>();
        return static_cast<Pool<T>&>(*slot);
    }

    std::vector<std::uint32_t> generations_;
    std::vector<SaveId> saveIds_;
    std::vector<std::uint32_t> free_;
    std::unordered_map<SaveId, std::uint32_t> bySave_;
    std::unordered_map<std::type_index, std::unique_ptr<IPool>> pools_;
    SaveId nextSaveId_ = 1;
    std::size_t alive_ = 0;
};

} // namespace lt::ecs
