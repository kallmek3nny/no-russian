// Milestone A input tests: semantic actions, default bindings, edge state,
// WASD/analog axes, quick belt, rebinding, gamepad coverage, touch layout rules.
#include <algorithm>
#include <vector>

#include "TestHarness.h"
#include "tcore/Input.h"

using namespace lt::input;

int main() {
    TEST_SECTION("action names");
    bool names = true;
    for (int i = 0; i < kActionCount; ++i) {
        const auto a = static_cast<Action>(i);
        names = names && !actionName(a).empty() && actionFromName(actionName(a)) == a;
    }
    CHECK(names);
    CHECK(!actionFromName("teleport").has_value());
    CHECK(actionName(Action::AttackLight) == "attack_light");
    CHECK(actionName(Action::LockOn) == "lock_on");

    TEST_SECTION("desktop defaults");
    auto has = [](const std::vector<Action>& v, Action a) {
        return std::find(v.begin(), v.end(), a) != v.end();
    };
    const ActionMap desk = ActionMap::desktopDefaults();
    CHECK(has(desk.actionsFor(mouseButton(mouse::kLeft)), Action::AttackLight));
    CHECK(has(desk.actionsFor(mouseButton(mouse::kRight)), Action::Guard));
    CHECK(has(desk.actionsFor(keyboard('q')), Action::Dodge));
    CHECK(has(desk.actionsFor(keyCode(keys::kSpace)), Action::Jump));
    CHECK(has(desk.actionsFor(keyboard('E')), Action::Cast));
    CHECK(has(desk.actionsFor(keyboard('R')), Action::Mine));
    CHECK(has(desk.actionsFor(keyboard('T')), Action::LockOn));
    CHECK(has(desk.actionsFor(keyboard('K')), Action::Save));
    CHECK(has(desk.actionsFor(keyboard('L')), Action::Load));
    CHECK(has(desk.actionsFor(keyboard('3')), Action::QuickItem));

    TEST_SECTION("edge state");
    InputState in(desk);
    in.beginFrame();
    in.onButton(keyboard('Q'), true);
    CHECK(in.pressed(Action::Dodge) && in.down(Action::Dodge));
    in.onButton(keyboard('Q'), true);
    in.beginFrame();
    CHECK(!in.pressed(Action::Dodge) && in.down(Action::Dodge));
    in.onButton(keyboard('Q'), false);
    CHECK(in.released(Action::Dodge) && !in.down(Action::Dodge));

    TEST_SECTION("move axis");
    in.beginFrame();
    in.onButton(keyboard('W'), true);
    in.onButton(keyboard('D'), true);
    Vec2 m = in.axis(Action::Move);
    CHECK(m.x > 0.70f && m.x < 0.71f && m.y > 0.70f && m.y < 0.71f);
    in.setAxis(Action::Move, {-0.5f, 0.0f});
    m = in.axis(Action::Move);
    CHECK(m.x == -0.5f && m.y == 0.0f);

    TEST_SECTION("quick belt");
    in.onButton(keyboard('4'), true);
    CHECK(in.pressed(Action::QuickItem) && in.quickSlot() == 3);

    TEST_SECTION("release all");
    in.beginFrame();
    in.releaseAll();
    CHECK(!in.down(Action::MoveForward) && in.released(Action::MoveForward));
    CHECK(!in.down(Action::QuickItem));

    TEST_SECTION("rebinding");
    ActionMap custom = desk;
    custom.unbind(keyboard('Q'));
    custom.bind(Action::Dodge, keyboard('X'));
    CHECK(custom.actionsFor(keyboard('Q')).empty());
    CHECK(has(custom.actionsFor(keyboard('X')), Action::Dodge));
    const auto before = custom.size();
    custom.bind(Action::Dodge, keyboard('X'));
    CHECK(custom.size() == before);

    TEST_SECTION("gamepad coverage");
    const ActionMap pad = ActionMap::gamepadDefaults();
    const Action semantic[] = {Action::AttackLight, Action::AttackHeavy, Action::Guard, Action::Dodge,
                               Action::Jump,        Action::Cast,        Action::Interact, Action::LockOn,
                               Action::QuickItem,   Action::Menu,        Action::Pause};
    bool covered = true;
    for (Action a : semantic) covered = covered && !pad.bindingsFor(a).empty();
    CHECK(covered);

    TEST_SECTION("touch layout");
    const Rect hud{0.0f, 0.0f, 240.0f, 72.0f};
    auto touch = defaultTouchLayout(640.0f, 360.0f);
    CHECK(touch.size() == 10);
    CHECK(validateTouchLayout(touch, 1.0f, {hud}).empty());
    CHECK(!validateTouchLayout(touch, 0.1f, {hud}).empty());
    touch[2].rectDp = {10.0f, 10.0f, 56.0f, 56.0f};
    const auto issues = validateTouchLayout(touch, 1.0f, {hud});
    CHECK(issues.size() == 1 && issues[0].kind == TouchIssue::CoversHud);
    touch[2].visible = false;
    CHECK(validateTouchLayout(touch, 1.0f, {hud}).empty());

    return ::lt::test::finish("input");
}
