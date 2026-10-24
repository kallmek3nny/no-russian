#pragma once
// TCore memory (Section 15: TCore owns memory, jobs, logging, random, serialization; Section 23
// budgets). Linear bump arenas with rewindable markers for per-frame scratch such as chunk
// meshing, culling lists and UI layout. Allocation never throws: a full arena returns nullptr and
// counts the failure, so soak tests (Section 19) flag scratch overruns instead of crashing.
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace lt::core {

inline constexpr bool isPowerOfTwo(std::size_t v) noexcept { return v != 0 && (v & (v - 1)) == 0; }

inline constexpr std::uintptr_t alignUp(std::uintptr_t v, std::size_t align) noexcept {
    return (v + (align - 1)) & ~static_cast<std::uintptr_t>(align - 1);
}

// Bump allocator over one owned block. Objects created here must be trivially destructible,
// because reset() and rewind() reclaim memory without running destructors.
class LinearArena {
public:
    using Marker = std::size_t;

    explicit LinearArena(std::size_t capacity)
        : buffer_(capacity ? std::make_unique<std::byte[]>(capacity) : nullptr),
          capacity_(capacity) {}
    LinearArena(const LinearArena&) = delete;
    LinearArena& operator=(const LinearArena&) = delete;
    LinearArena(LinearArena&&) noexcept = default;
    LinearArena& operator=(LinearArena&&) noexcept = default;

    void* allocate(std::size_t size, std::size_t align = alignof(std::max_align_t)) noexcept {
        if (!buffer_ || !isPowerOfTwo(align)) {
            ++failures_;
            return nullptr;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(buffer_.get());
        const auto start = static_cast<std::size_t>(alignUp(base + used_, align) - base);
        if (start > capacity_ || size > capacity_ - start) {
            ++failures_;
            return nullptr;
        }
        used_ = start + size;
        if (used_ > peak_) peak_ = used_;
        return buffer_.get() + start;
    }

    template <class T, class... Args>
    T* create(Args&&... args) {
        static_assert(std::is_trivially_destructible_v<T>, "arena objects are never destroyed");
        void* p = allocate(sizeof(T), alignof(T));
        return p ? ::new (p) T(std::forward<Args>(args)...) : nullptr;
    }

    // Value-initialised array of n elements; nullptr (and a counted failure) when it cannot fit.
    template <class T>
    T* createArray(std::size_t n) {
        static_assert(std::is_trivially_destructible_v<T>, "arena objects are never destroyed");
        if (n > capacity_ / sizeof(T)) {
            ++failures_;
            return nullptr;
        }
        void* p = allocate(sizeof(T) * n, alignof(T));
        if (!p) return nullptr;
        T* first = static_cast<T*>(p);
        for (std::size_t i = 0; i < n; ++i) ::new (static_cast<void*>(first + i)) T();
        return first;
    }

    Marker mark() const noexcept { return used_; }
    void rewind(Marker m) noexcept { if (m <= used_) used_ = m; }
    void reset() noexcept { used_ = 0; }

    std::size_t used() const noexcept { return used_; }
    std::size_t capacity() const noexcept { return capacity_; }
    std::size_t remaining() const noexcept { return buffer_ ? capacity_ - used_ : 0; }
    std::size_t peak() const noexcept { return peak_; }
    std::uint64_t failures() const noexcept { return failures_; }

private:
    std::unique_ptr<std::byte[]> buffer_;
    std::size_t capacity_ = 0;
    std::size_t used_ = 0;
    std::size_t peak_ = 0;
    std::uint64_t failures_ = 0;
};

// Rewinds an arena to where it was when the scope opened (nested scratch within one frame).
class ArenaScope {
public:
    explicit ArenaScope(LinearArena& arena) noexcept : arena_(arena), marker_(arena.mark()) {}
    ~ArenaScope() { arena_.rewind(marker_); }
    ArenaScope(const ArenaScope&) = delete;
    ArenaScope& operator=(const ArenaScope&) = delete;

private:
    LinearArena& arena_;
    LinearArena::Marker marker_;
};

}  // namespace lt::core
