#pragma once
// Legend Engine T - Milestone B: one research chain (design doc section 8).
// Scan -> extract aspects -> solve node-link research -> record in Thaumonomicon.
// Plain data so progress survives save/reload via serialize()/deserialize().
#include <array>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace lt::world {

enum class Aspect : std::uint8_t { Motion = 0, Form, Life, Decay, Light, Memory, Count };
constexpr std::size_t kPrimalAspectCount = static_cast<std::size_t>(Aspect::Count);

inline const char* toString(Aspect a) {
    switch (a) {
        case Aspect::Motion: return "motion";
        case Aspect::Form: return "form";
        case Aspect::Life: return "life";
        case Aspect::Decay: return "decay";
        case Aspect::Light: return "light";
        case Aspect::Memory: return "memory";
        default: return "unknown";
    }
}

using AspectPool = std::array<std::uint32_t, kPrimalAspectCount>;

// A scannable material or creature and the aspects it yields once.
struct ScanTarget {
    std::string id;   // e.g. material.glass_shard
    AspectPool yield{};
};

// Node-link research: every required link (a pair of aspects) must be placed.
struct AspectLink {
    Aspect a = Aspect::Motion;
    Aspect b = Aspect::Motion;
    bool operator==(const AspectLink& o) const {
        return (a == o.a && b == o.b) || (a == o.b && b == o.a);
    }
};

struct ResearchNode {
    std::string id;                          // e.g. research.flux_condenser
    std::vector<std::string> prerequisites;  // node ids that must be complete
    AspectPool cost{};                       // aspects consumed to begin the puzzle
    std::vector<AspectLink> requiredLinks;   // links the player must connect
    std::string unlock;                      // device/attachment unlocked
};

enum class ResearchError : std::uint8_t {
    None = 0, UnknownNode, AlreadyComplete, MissingPrerequisite, InsufficientAspects,
    NotStarted, InvalidLink, PuzzleIncomplete
};

class ResearchChain {
public:
    void addTarget(ScanTarget t) { targets_.push_back(std::move(t)); }
    void addNode(ResearchNode n) { nodes_.push_back(std::move(n)); }

    // Scanning yields aspects once per target; returns false for unknown/repeat scans.
    bool scan(const std::string& targetId) {
        if (contains(scanned_, targetId)) return false;
        for (const auto& t : targets_) {
            if (t.id != targetId) continue;
            for (std::size_t i = 0; i < kPrimalAspectCount; ++i) pool_[i] += t.yield[i];
            scanned_.push_back(targetId);
            return true;
        }
        return false;
    }

    ResearchError begin(const std::string& nodeId) {
        const ResearchNode* n = find(nodeId);
        if (!n) return ResearchError::UnknownNode;
        if (contains(completed_, nodeId)) return ResearchError::AlreadyComplete;
        if (contains(started_, nodeId)) return ResearchError::None;
        for (const auto& p : n->prerequisites)
            if (!contains(completed_, p)) return ResearchError::MissingPrerequisite;
        for (std::size_t i = 0; i < kPrimalAspectCount; ++i)
            if (pool_[i] < n->cost[i]) return ResearchError::InsufficientAspects;
        for (std::size_t i = 0; i < kPrimalAspectCount; ++i) pool_[i] -= n->cost[i];
        started_.push_back(nodeId);
        return ResearchError::None;
    }

    ResearchError placeLink(const std::string& nodeId, AspectLink link) {
        const ResearchNode* n = find(nodeId);
        if (!n) return ResearchError::UnknownNode;
        if (!contains(started_, nodeId)) return ResearchError::NotStarted;
        bool required = false;
        for (const auto& r : n->requiredLinks) if (r == link) { required = true; break; }
        if (!required) return ResearchError::InvalidLink;
        for (const auto& p : placed_) if (p.node == nodeId && p.link == link) return ResearchError::None;
        placed_.push_back({nodeId, link});
        return ResearchError::None;
    }

    // Completes the node and records it in the Thaumonomicon when every link is placed.
    ResearchError solve(const std::string& nodeId) {
        const ResearchNode* n = find(nodeId);
        if (!n) return ResearchError::UnknownNode;
        if (contains(completed_, nodeId)) return ResearchError::AlreadyComplete;
        if (!contains(started_, nodeId)) return ResearchError::NotStarted;
        for (const auto& r : n->requiredLinks) {
            bool found = false;
            for (const auto& p : placed_) if (p.node == nodeId && p.link == r) { found = true; break; }
            if (!found) return ResearchError::PuzzleIncomplete;
        }
        completed_.push_back(nodeId);
        if (!n->unlock.empty()) thaumonomicon_.push_back(n->unlock);
        return ResearchError::None;
    }

    bool isComplete(const std::string& nodeId) const { return contains(completed_, nodeId); }
    bool isUnlocked(const std::string& unlock) const { return contains(thaumonomicon_, unlock); }
    std::uint32_t aspect(Aspect a) const { return pool_[static_cast<std::size_t>(a)]; }
    const std::vector<std::string>& thaumonomicon() const { return thaumonomicon_; }
    const AspectPool& aspectPool() const { return pool_; }

    // Section 8 crafting loop: extracted aspects fuel infusions and Taumium refinement.
    bool canAfford(const AspectPool& cost) const {
        for (std::size_t i = 0; i < kPrimalAspectCount; ++i)
            if (pool_[i] < cost[i]) return false;
        return true;
    }

    // Consumes the whole cost or nothing; false leaves the pool untouched.
    bool spend(const AspectPool& cost) {
        if (!canAfford(cost)) return false;
        for (std::size_t i = 0; i < kPrimalAspectCount; ++i) pool_[i] -= cost[i];
        return true;
    }

    // Aspects returned by extraction devices or salvaged from failed infusions.
    void grant(const AspectPool& amount) {
        for (std::size_t i = 0; i < kPrimalAspectCount; ++i) pool_[i] += amount[i];
    }

    // Records a field-test or infusion result in the Thaumonomicon. Entries are stable,
    // whitespace-free keys so the research_v1 text format stays parseable; empty,
    // whitespace-bearing or duplicate entries are rejected.
    bool record(const std::string& entry) {
        if (entry.empty() || contains(thaumonomicon_, entry)) return false;
        for (char c : entry)
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f') return false;
        thaumonomicon_.push_back(entry);
        return true;
    }

    // Progress only (definitions are authored data, reloaded separately).
    std::string serialize() const {
        std::ostringstream out;
        out << "research_v1\n";
        for (auto v : pool_) out << v << ' ';
        out << '\n';
        writeList(out, scanned_);
        writeList(out, started_);
        writeList(out, completed_);
        writeList(out, thaumonomicon_);
        out << placed_.size() << '\n';
        for (const auto& p : placed_)
            out << p.node << ' ' << int(p.link.a) << ' ' << int(p.link.b) << '\n';
        return out.str();
    }

    bool deserialize(const std::string& data) {
        std::istringstream in(data);
        std::string header;
        if (!(in >> header) || header != "research_v1") return false;
        AspectPool pool{};
        for (auto& v : pool) if (!(in >> v)) return false;
        std::vector<std::string> scanned, started, completed, book;
        if (!readList(in, scanned) || !readList(in, started) || !readList(in, completed) ||
            !readList(in, book)) return false;
        std::size_t count = 0;
        if (!(in >> count)) return false;
        std::vector<PlacedLink> placed;
        for (std::size_t i = 0; i < count; ++i) {
            PlacedLink p; int a = 0, b = 0;
            if (!(in >> p.node >> a >> b)) return false;
            if (a < 0 || b < 0 || a >= int(kPrimalAspectCount) || b >= int(kPrimalAspectCount)) return false;
            p.link = {static_cast<Aspect>(a), static_cast<Aspect>(b)};
            placed.push_back(std::move(p));
        }
        pool_ = pool; scanned_ = std::move(scanned); started_ = std::move(started);
        completed_ = std::move(completed); thaumonomicon_ = std::move(book); placed_ = std::move(placed);
        return true;
    }

private:
    struct PlacedLink { std::string node; AspectLink link; };

    const ResearchNode* find(const std::string& id) const {
        for (const auto& n : nodes_) if (n.id == id) return &n;
        return nullptr;
    }
    static bool contains(const std::vector<std::string>& v, const std::string& s) {
        for (const auto& x : v) if (x == s) return true;
        return false;
    }
    static void writeList(std::ostringstream& out, const std::vector<std::string>& v) {
        out << v.size();
        for (const auto& s : v) out << ' ' << s;
        out << '\n';
    }
    static bool readList(std::istringstream& in, std::vector<std::string>& v) {
        std::size_t n = 0;
        if (!(in >> n)) return false;
        v.resize(n);
        for (auto& s : v) if (!(in >> s)) return false;
        return true;
    }

    std::vector<ScanTarget> targets_;
    std::vector<ResearchNode> nodes_;
    AspectPool pool_{};
    std::vector<std::string> scanned_, started_, completed_, thaumonomicon_;
    std::vector<PlacedLink> placed_;
};

} // namespace lt::world
