#pragma once
// TCore input abstraction (Sections 17, 22): semantic actions decoupled from
// devices, default desktop/gamepad bindings, per-frame edge state, and touch
// layout validation (9dp minimum targets, never covering the HUD).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace lt::input {

enum class Action : std::uint8_t {
    Move, Look, MoveForward, MoveBack, MoveLeft, MoveRight,
    AttackLight, AttackHeavy, Guard, Dodge, Jump, Cast, Interact, LockOn, QuickItem, Menu, Pause,
    Mine, Build, Scan, Shrine, Flask, Infuse, ConstructCommand, LeylineTravel,
    Inventory, Journal, Save, Load,
    Count
};

inline constexpr int kActionCount = static_cast<int>(Action::Count);

inline constexpr std::array<std::string_view, kActionCount> kActionNames{
    "move", "look", "move_forward", "move_back", "move_left", "move_right",
    "attack_light", "attack_heavy", "guard", "dodge", "jump", "cast", "interact", "lock_on",
    "quick_item", "menu", "pause",
    "mine", "build", "scan", "shrine", "flask", "infuse", "construct_command", "leyline_travel",
    "inventory", "journal", "save", "load"};

constexpr std::string_view actionName(Action a) noexcept {
    const int i = static_cast<int>(a);
    return i >= 0 && i < kActionCount ? kActionNames[static_cast<std::size_t>(i)] : std::string_view{};
}

inline std::optional<Action> actionFromName(std::string_view name) noexcept {
    for (int i = 0; i < kActionCount; ++i) {
        if (kActionNames[static_cast<std::size_t>(i)] == name) return static_cast<Action>(i);
    }
    return std::nullopt;
}

enum class Device : std::uint8_t { Keyboard, Mouse, Gamepad, Touch };

struct Binding {
    Device device = Device::Keyboard;
    std::uint16_t code = 0;
    friend constexpr bool operator==(const Binding&, const Binding&) = default;
};

namespace keys {
inline constexpr std::uint16_t kSpace = 32;
inline constexpr std::uint16_t kEscape = 27;
} // namespace keys

namespace mouse {
inline constexpr std::uint16_t kLeft = 0;
inline constexpr std::uint16_t kRight = 1;
inline constexpr std::uint16_t kMiddle = 2;
} // namespace mouse

namespace pad {
enum Button : std::uint16_t {
    A, B, X, Y, LeftBumper, RightBumper, LeftTrigger, RightTrigger, Start, Select, LeftStick, RightStick
};
} // namespace pad

// Keyboard codes are upper-case ASCII for letters and digits.
constexpr Binding keyboard(char c) noexcept {
    const char up = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
    return {Device::Keyboard, static_cast<std::uint16_t>(static_cast<unsigned char>(up))};
}
constexpr Binding keyCode(std::uint16_t code) noexcept { return {Device::Keyboard, code}; }
constexpr Binding mouseButton(std::uint16_t b) noexcept { return {Device::Mouse, b}; }
constexpr Binding gamepad(pad::Button b) noexcept {
    return {Device::Gamepad, static_cast<std::uint16_t>(b)};
}

class ActionMap {
public:
    void bind(Action a, Binding b) {
        for (const auto& e : entries_) {
            if (e.binding == b && e.action == a) return;
        }
        entries_.push_back({b, a});
    }

    void unbind(Binding b) {
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                      [&](const Entry& e) { return e.binding == b; }),
                       entries_.end());
    }

    std::vector<Action> actionsFor(Binding b) const {
        std::vector<Action> out;
        for (const auto& e : entries_) {
            if (e.binding == b) out.push_back(e.action);
        }
        return out;
    }

    std::vector<Binding> bindingsFor(Action a) const {
        std::vector<Binding> out;
        for (const auto& e : entries_) {
            if (e.action == a) out.push_back(e.binding);
        }
        return out;
    }

    std::size_t size() const noexcept { return entries_.size(); }

    // Section 22 desktop defaults; mouse movement drives the analog Look axis.
    static ActionMap desktopDefaults() {
        ActionMap m;
        m.bind(Action::MoveForward, keyboard('W'));
        m.bind(Action::MoveBack, keyboard('S'));
        m.bind(Action::MoveLeft, keyboard('A'));
        m.bind(Action::MoveRight, keyboard('D'));
        m.bind(Action::AttackLight, mouseButton(mouse::kLeft));
        m.bind(Action::Guard, mouseButton(mouse::kRight));
        m.bind(Action::Dodge, keyboard('Q'));
        m.bind(Action::Jump, keyCode(keys::kSpace));
        m.bind(Action::Cast, keyboard('E'));
        m.bind(Action::Mine, keyboard('R'));
        m.bind(Action::Interact, keyboard('R'));
        m.bind(Action::Build, keyboard('B'));
        m.bind(Action::Scan, keyboard('V'));
        m.bind(Action::Shrine, keyboard('F'));
        m.bind(Action::Flask, keyboard('H'));
        m.bind(Action::Infuse, keyboard('C'));
        m.bind(Action::ConstructCommand, keyboard('G'));
        m.bind(Action::LeylineTravel, keyboard('P'));
        m.bind(Action::LockOn, keyboard('T'));
        m.bind(Action::Inventory, keyboard('I'));
        m.bind(Action::Journal, keyboard('J'));
        m.bind(Action::Save, keyboard('K'));
        m.bind(Action::Load, keyboard('L'));
        m.bind(Action::Menu, keyCode(keys::kEscape));
        m.bind(Action::Pause, keyCode(keys::kEscape));
        for (char c = '1'; c <= '9'; ++c) m.bind(Action::QuickItem, keyboard(c));
        return m;
    }

    // Controller defaults; the sticks drive the analog Move/Look axes.
    static ActionMap gamepadDefaults() {
        ActionMap m;
        m.bind(Action::AttackLight, gamepad(pad::RightBumper));
        m.bind(Action::AttackHeavy, gamepad(pad::RightTrigger));
        m.bind(Action::Guard, gamepad(pad::LeftBumper));
        m.bind(Action::Cast, gamepad(pad::LeftTrigger));
        m.bind(Action::Jump, gamepad(pad::A));
        m.bind(Action::Dodge, gamepad(pad::B));
        m.bind(Action::Interact, gamepad(pad::X));
        m.bind(Action::QuickItem, gamepad(pad::Y));
        m.bind(Action::LockOn, gamepad(pad::RightStick));
        m.bind(Action::Menu, gamepad(pad::Select));
        m.bind(Action::Pause, gamepad(pad::Start));
        return m;
    }

private:
    struct Entry {
        Binding binding;
        Action action;
    };
    std::vector<Entry> entries_;
};

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

// Per-frame action state. Call beginFrame() once per tick before feeding events.
// Analog values persist until set again.
class InputState {
public:
    explicit InputState(const ActionMap& map) : map_(&map) {}

    void beginFrame() noexcept {
        pressed_.fill(false);
        released_.fill(false);
    }

    void onButton(Binding b, bool isDown) {
        const auto it = std::find(held_.begin(), held_.end(), b);
        if (isDown == (it != held_.end())) return; // key repeat or stray release
        if (isDown) {
            held_.push_back(b);
        } else {
            held_.erase(it);
        }
        for (Action a : map_->actionsFor(b)) {
            const std::size_t i = idx(a);
            if (isDown) {
                if (count_[i]++ == 0) pressed_[i] = true;
                if (a == Action::QuickItem && b.device == Device::Keyboard && b.code >= '1' &&
                    b.code <= '9') {
                    quickSlot_ = b.code - '1';
                }
            } else if (count_[i] > 0 && --count_[i] == 0) {
                released_[i] = true;
            }
        }
    }

    // Used on focus loss / Android background so no action stays stuck down.
    void releaseAll() {
        const auto held = held_;
        for (const Binding& b : held) onButton(b, false);
    }

    void setAxis(Action a, Vec2 v) noexcept { analog_[idx(a)] = v; }

    // Move falls back to the digital WASD composite when no analog value is set.
    Vec2 axis(Action a) const noexcept {
        const Vec2 v = analog_[idx(a)];
        if (v.x != 0.0f || v.y != 0.0f || a != Action::Move) return v;
        Vec2 d{(down(Action::MoveRight) ? 1.0f : 0.0f) - (down(Action::MoveLeft) ? 1.0f : 0.0f),
               (down(Action::MoveForward) ? 1.0f : 0.0f) - (down(Action::MoveBack) ? 1.0f : 0.0f)};
        const float len = std::sqrt(d.x * d.x + d.y * d.y);
        if (len > 1.0f) {
            d.x /= len;
            d.y /= len;
        }
        return d;
    }

    bool down(Action a) const noexcept { return count_[idx(a)] > 0; }
    bool pressed(Action a) const noexcept { return pressed_[idx(a)]; }
    bool released(Action a) const noexcept { return released_[idx(a)]; }
    int quickSlot() const noexcept { return quickSlot_; }

private:
    static std::size_t idx(Action a) noexcept { return static_cast<std::size_t>(a); }

    const ActionMap* map_;
    std::vector<Binding> held_;
    std::array<int, kActionCount> count_{};
    std::array<bool, kActionCount> pressed_{};
    std::array<bool, kActionCount> released_{};
    std::array<Vec2, kActionCount> analog_{};
    int quickSlot_ = -1;
};

// ---- Touch layout (Sections 17, 22) ----

inline constexpr float kMinTouchTargetDp = 9.0f;

struct Rect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    constexpr bool intersects(const Rect& o) const noexcept {
        return x < o.x + o.w && o.x < x + w && y < o.y + o.h && o.y < y + h;
    }
};

struct TouchButton {
    Action action = Action::AttackLight;
    Rect rectDp;
    bool visible = true;
};

struct TouchIssue {
    Action action = Action::AttackLight;
    enum Kind : std::uint8_t { TooSmall, CoversHud } kind = TooSmall;
};

// Left movement stick, right camera pad, and a bottom-right action cluster.
inline std::vector<TouchButton> defaultTouchLayout(float widthDp, float heightDp) {
    constexpr float kButton = 56.0f;
    constexpr float kStep = 64.0f;
    std::vector<TouchButton> out;
    out.push_back({Action::Move, {16.0f, heightDp - 176.0f, 160.0f, 160.0f}});
    out.push_back({Action::Look, {widthDp * 0.5f, 80.0f, widthDp * 0.5f - 8.0f, heightDp - 216.0f}});
    const Action cluster[] = {Action::AttackLight, Action::Guard,    Action::Dodge,     Action::Jump,
                              Action::Cast,        Action::Interact, Action::QuickItem, Action::LockOn};
    const float x0 = widthDp - 4.0f * kStep;
    const float y0 = heightDp - 2.0f * kStep;
    for (int k = 0; k < 8; ++k) {
        out.push_back({cluster[k],
                       {x0 + static_cast<float>(k % 4) * kStep, y0 + static_cast<float>(k / 4) * kStep,
                        kButton, kButton}});
    }
    return out;
}

// scale is the user's button-size multiplier; hidden buttons are ignored.
inline std::vector<TouchIssue> validateTouchLayout(const std::vector<TouchButton>& buttons, float scale,
                                                   const std::vector<Rect>& hud) {
    std::vector<TouchIssue> issues;
    for (const auto& b : buttons) {
        if (!b.visible) continue;
        const Rect r{b.rectDp.x, b.rectDp.y, b.rectDp.w * scale, b.rectDp.h * scale};
        if (std::min(r.w, r.h) < kMinTouchTargetDp) issues.push_back({b.action, TouchIssue::TooSmall});
        for (const auto& h : hud) {
            if (r.intersects(h)) {
                issues.push_back({b.action, TouchIssue::CoversHud});
                break;
            }
        }
    }
    return issues;
}

} // namespace lt::input
