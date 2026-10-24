#pragma once
// Legend Engine T - Milestone B: autonomous event lifecycle (design doc section 5 / 21).
// Lifecycle: rumour -> visible -> active -> resolved -> aftermath. Aftermath persists.
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace lt::world {

enum class EventState : std::uint8_t { Rumour = 0, Visible, Active, Resolved, Aftermath };
enum class EventOutcome : std::uint8_t { None = 0, Success, Failure };

inline const char* toString(EventState s) {
    switch (s) {
        case EventState::Rumour: return "rumour";
        case EventState::Visible: return "visible";
        case EventState::Active: return "active";
        case EventState::Resolved: return "resolved";
        case EventState::Aftermath: return "aftermath";
    }
    return "unknown";
}

// Static definition matching the Event record schema.
struct EventDef {
    std::string id;                    // e.g. event.caravan_under_siege
    std::vector<std::string> biomes;   // requirements.biome
    std::string faction;               // requirements.faction
    std::vector<std::string> stages;   // authored stages (informational)
    std::string failureResult;         // e.g. road_bandit_checkpoint
    std::string successResult;         // e.g. travelling_vendor_and_research_note
    std::uint32_t cooldownHours = 0;

    bool compatible(const std::string& biome, const std::string& fac) const {
        bool biomeOk = biomes.empty();
        for (const auto& b : biomes) if (b == biome) { biomeOk = true; break; }
        return biomeOk && (faction.empty() || faction == fac);
    }
};

// Runtime instance; plain data so it survives chunk unload/reload via serialize().
struct EventInstance {
    std::string defId;
    std::int32_t regionX = 0, regionZ = 0;
    EventState state = EventState::Rumour;
    EventOutcome outcome = EventOutcome::None;
    std::string aftermath;          // persisted consequence (vendor, ruin, checkpoint...)
    std::uint64_t stateSinceHour = 0;

    // Advance one lifecycle step. Returns false on an illegal transition.
    bool advance(std::uint64_t nowHour) {
        if (state == EventState::Active || state == EventState::Aftermath) return false; // Active needs resolve()
        state = static_cast<EventState>(static_cast<int>(state) + 1);
        stateSinceHour = nowHour;
        return true;
    }

    // Resolve an active event; records outcome. Player need not be present.
    bool resolve(const EventDef& def, bool success, std::uint64_t nowHour) {
        if (state != EventState::Active) return false;
        outcome = success ? EventOutcome::Success : EventOutcome::Failure;
        state = EventState::Resolved;
        stateSinceHour = nowHour;
        aftermath = success ? def.successResult : def.failureResult;
        return true;
    }

    // Resolved -> Aftermath (persists permanently).
    bool settle(std::uint64_t nowHour) {
        if (state != EventState::Resolved) return false;
        state = EventState::Aftermath;
        stateSinceHour = nowHour;
        return true;
    }

    bool cooldownElapsed(const EventDef& def, std::uint64_t nowHour) const {
        return state == EventState::Aftermath && nowHour >= stateSinceHour + def.cooldownHours;
    }

    std::string serialize() const {
        std::ostringstream o;
        o << defId << ' ' << regionX << ' ' << regionZ << ' ' << int(state) << ' ' << int(outcome) << ' '
          << stateSinceHour << ' ' << (aftermath.empty() ? std::string("-") : aftermath);
        return o.str();
    }

    static std::optional<EventInstance> deserialize(const std::string& s) {
        std::istringstream in(s);
        EventInstance e;
        int st = 0, oc = 0;
        if (!(in >> e.defId >> e.regionX >> e.regionZ >> st >> oc >> e.stateSinceHour >> e.aftermath)) return std::nullopt;
        if (st < 0 || st > 4 || oc < 0 || oc > 2) return std::nullopt;
        e.state = static_cast<EventState>(st);
        e.outcome = static_cast<EventOutcome>(oc);
        if (e.aftermath == "-") e.aftermath.clear();
        return e;
    }
};

// Per-region event set with save/load round trip.
class EventLedger {
public:
    EventInstance& spawn(const EventDef& def, std::int32_t rx, std::int32_t rz, std::uint64_t nowHour) {
        EventInstance e; e.defId = def.id; e.regionX = rx; e.regionZ = rz; e.stateSinceHour = nowHour;
        events_.push_back(e);
        return events_.back();
    }
    std::vector<EventInstance>& events() { return events_; }
    const std::vector<EventInstance>& events() const { return events_; }

    std::string save() const {
        std::string out;
        for (const auto& e : events_) { out += e.serialize(); out += '\n'; }
        return out;
    }
    bool load(const std::string& data) {
        std::vector<EventInstance> loaded;
        std::istringstream in(data);
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            auto e = EventInstance::deserialize(line);
            if (!e) return false;
            loaded.push_back(*e);
        }
        events_ = std::move(loaded);
        return true;
    }

private:
    std::vector<EventInstance> events_;
};

} // namespace lt::world
