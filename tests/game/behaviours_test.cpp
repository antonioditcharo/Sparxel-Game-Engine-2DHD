// Behaviours on their own, without a project: blocking bodies, trigger areas and
// interaction, driven by an input state the test sets directly.

#include "engine/game/combat.h"
#include "engine/game/runtime.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace game = engine::game;
namespace input = engine::input;
namespace reflect = engine::reflect;
namespace scene2d = engine::scene2d;
namespace world = engine::world;

namespace {

struct Fixture {
    Fixture() {
        game::register_types(registry);
        REQUIRE(registry.errors().empty());
        w = std::make_unique<world::World>(registry);
        REQUIRE(scene2d::register_components(*w));
        REQUIRE(game::register_behaviour_components(*w));
        hero = place("hero", 0.0, 0.0);
        w->add<game::TopDownMover>(hero);
    }

    world::Entity place(const char* name, double x, double y) const {
        const world::Entity e = w->create(name);
        scene2d::Transform2D* at = w->add<scene2d::Transform2D>(e);
        at->x = x;
        at->y = y;
        return e;
    }

    /// Runs `steps` steps of 1/60 s with the given move actions held.
    void walk(int steps, std::initializer_list<input::Key> keys) {
        for (int i = 0; i < steps; ++i) {
            state.begin_step();
            for (const input::Key k :
                 {input::Key::Left, input::Key::Right, input::Key::Up, input::Key::Down}) {
                bool down = false;
                for (const input::Key held : keys) {
                    down = down || held == k;
                }
                state.set(input::Binding::key(k), down);
            }
            game::move_characters(*w, state, 1.0 / 60.0, 16);
        }
    }

    [[nodiscard]] scene2d::Transform2D& at(world::Entity e) const {
        scene2d::Transform2D* t = w->get<scene2d::Transform2D>(e);
        REQUIRE(t != nullptr);
        return *t;
    }

    reflect::Registry registry;
    std::unique_ptr<world::World> w;
    input::InputState state{input::InputMap::rpg_preset()};
    world::Entity hero;
};

} // namespace

TEST_CASE("Boxes overlap only by more than an edge, and stop at each other") {
    const scene2d::Box a{0.0, 0.0, 1.0, 1.0};
    CHECK(scene2d::overlaps(a, scene2d::Box{0.5, 0.5, 1.5, 1.5}));
    CHECK_FALSE(scene2d::overlaps(a, scene2d::Box{1.0, 0.0, 2.0, 1.0})); // sharing an edge
    CHECK_FALSE(scene2d::overlaps(a, scene2d::Box{1.0 - 1e-12, 0.0, 2.0, 1.0}));
    const std::vector<scene2d::Box> crates{
        {3.0, 0.0, 4.0, 1.0}, {-3.0, 0.0, -2.0, 1.0}, {0.0, 5.0, 1.0, 6.0}};
    using scene2d::Axis;
    CHECK(scene2d::sweep_boxes(a, Axis::X, 5.0, crates) == 2.0);
    CHECK(scene2d::sweep_boxes(a, Axis::X, -5.0, crates) == -2.0);
    CHECK(scene2d::sweep_boxes(a, Axis::Y, 10.0, crates) == 4.0);
    CHECK(scene2d::sweep_boxes(a, Axis::Y, -10.0, crates) == -10.0);
    CHECK(scene2d::sweep_boxes(a, Axis::X, 1.0, crates) == 1.0);
    // Already overlapping: it does not block, so things can separate.
    CHECK(scene2d::sweep_boxes(scene2d::Box{2.5, 0.0, 3.5, 1.0}, Axis::X, 5.0, crates) == 5.0);
    // Touching on the other axis only: passes by.
    CHECK(scene2d::sweep_boxes(scene2d::Box{0.0, 1.0, 1.0, 2.0}, Axis::X, 5.0, crates) == 5.0);
}

TEST_CASE("Solid bodies stop movers, but not their own") {
    Fixture f;
    const world::Entity villager = f.place("villager", 2.0, 0.0);
    f.w->add<game::Body>(villager); // 0.75 wide: from 1.625
    f.w->add<game::Body>(f.hero);   // the hero's own body never blocks it

    f.walk(60, {input::Key::Right});                // a second at 4 tiles per second would reach 4
    CHECK(f.at(f.hero).x == doctest::Approx(1.25)); // hitbox right edge on 1.625
    CHECK(f.at(villager).x == 2.0);

    // A disabled body lets the mover through.
    f.w->get<game::Body>(villager)->enabled = false;
    f.walk(15, {input::Key::Right});
    CHECK(f.at(f.hero).x == doctest::Approx(2.25));
}

TEST_CASE("Trigger areas report movers walking in and out, once each") {
    Fixture f;
    const world::Entity door = f.place("door", 5.0, 0.0); // 1 x 1: x 4.5 to 5.5
    f.w->add<game::Trigger>(door);
    const world::Entity guard = f.place("guard", 5.0, 0.0); // a second mover, already inside
    f.w->add<game::TopDownMover>(guard)->enabled = false;

    std::vector<game::TriggerEvent> events = game::update_triggers(*f.w);
    REQUIRE(events.size() == 1);
    CHECK(events[0].trigger == door);
    CHECK(events[0].who == guard);
    CHECK(events[0].entered);
    CHECK(game::update_triggers(*f.w).empty()); // nothing changed

    // The hero's hitbox (0.75 wide) reaches 4.5 once x > 4.125, and leaves past 5.875.
    f.at(f.hero).x = 4.0;
    CHECK(game::update_triggers(*f.w).empty());
    f.at(f.hero).x = 4.2;
    events = game::update_triggers(*f.w);
    REQUIRE(events.size() == 1);
    CHECK(events[0].who == f.hero);
    CHECK(events[0].entered);
    f.at(f.hero).x = 6.0;
    f.at(guard).x = 9.0;
    events = game::update_triggers(*f.w);
    REQUIRE(events.size() == 2); // both left: exits sorted by mover GUID
    CHECK_FALSE(events[0].entered);
    CHECK_FALSE(events[1].entered);
    CHECK(f.w->guid(events[0].who) < f.w->guid(events[1].who));

    // Disabling the area makes everyone inside leave.
    f.at(f.hero).x = 5.0;
    REQUIRE(game::update_triggers(*f.w).size() == 1);
    f.w->get<game::Trigger>(door)->enabled = false;
    events = game::update_triggers(*f.w);
    REQUIRE(events.size() == 1);
    CHECK_FALSE(events[0].entered);
    CHECK(f.w->get<game::Trigger>(door)->inside.empty());
}

TEST_CASE("Interact reaches the nearest interactable the mover faces") {
    const Fixture f;
    const world::Entity sign = f.place("sign", 1.0, 0.0); // 1 x 1: x 0.5 to 1.5
    f.w->add<game::Interactable>(sign);
    game::TopDownMover* mover = f.w->get<game::TopDownMover>(f.hero);

    mover->facing = game::Facing::Right; // reach: hitbox moved 0.5 right, x 0.125 to 0.875
    CHECK(game::find_interaction(*f.w, f.hero) == sign);
    mover->facing = game::Facing::Left;
    CHECK_FALSE(game::find_interaction(*f.w, f.hero).is_valid());
    mover->facing = game::Facing::Up; // hitbox y -0.5 to 0, moved up: y -1 to -0.5
    CHECK_FALSE(game::find_interaction(*f.w, f.hero).is_valid());

    // The nearer of two wins; disabled ones and the mover itself are skipped.
    const world::Entity chest = f.place("chest", 0.0, -0.75);
    f.w->add<game::Interactable>(chest); // y -1.75 to -0.75: overlaps the reach upwards
    CHECK(game::find_interaction(*f.w, f.hero) == chest);
    mover->facing = game::Facing::Right;
    const world::Entity near_sign = f.place("near_sign", 0.75, 0.0);
    f.w->add<game::Interactable>(near_sign);
    CHECK(game::find_interaction(*f.w, f.hero) == near_sign);
    f.w->get<game::Interactable>(near_sign)->enabled = false;
    CHECK(game::find_interaction(*f.w, f.hero) == sign);
    f.w->add<game::Interactable>(f.hero);
    CHECK(game::find_interaction(*f.w, f.hero) == sign);
    CHECK_FALSE(game::find_interaction(*f.w, sign).is_valid()); // not a mover

    const reflect::TypeInfo* trigger = f.registry.find_type<game::Trigger>();
    REQUIRE(trigger != nullptr);
    CHECK(trigger->meta.display_name == "Trigger Area");
    CHECK(trigger->find_event("entered") != nullptr);
    CHECK(f.registry.find_type<game::Interactable>()->find_event("interacted") != nullptr);
    CHECK(f.registry.find_type<game::Body>()->meta.display_name == "Solid Body");
}

TEST_CASE("Falling and jumping, one step at a time") {
    // A floor at y = 0 below a ceiling at y = -10: `move` goes as far as it can.
    double y = 0.0;
    double ceiling = -10.0;
    const auto move = [&](double wanted) {
        const double to = std::clamp(y + wanted, ceiling, 0.0);
        const double moved = to - y;
        y = to;
        return moved;
    };
    constexpr double dt = 1.0 / 60.0;
    game::Jumping jumping; // gravity 40, height 3
    game::fall(jumping, false, dt, move);
    CHECK(jumping.on_ground); // pressed against the floor
    CHECK(jumping.fall_speed == 0.0);
    CHECK(y == 0.0);

    // A jump rises its height (less half a step's rise, for whole steps), then lands.
    game::fall(jumping, true, dt, move);
    CHECK_FALSE(jumping.on_ground);
    CHECK(jumping.fall_speed < 0.0);
    double highest = 0.0;
    int steps = 1;
    while (!jumping.on_ground && steps < 600) {
        game::fall(jumping, true, dt, move); // held: no jumping again in the air
        highest = std::min(highest, y);
        ++steps;
    }
    CAPTURE(highest);
    CHECK(highest < -2.8);
    CHECK(highest > -3.0);
    CHECK(y == 0.0);
    CHECK(jumping.fall_speed == 0.0);
    // Up and down in about 2 * sqrt(2 h / g) = 0.77 seconds.
    CHECK(steps >= 44);
    CHECK(steps <= 48);

    // A ceiling 1 tile up stops the rise; it falls back from there.
    ceiling = -1.0;
    game::fall(jumping, true, dt, move);
    while (jumping.fall_speed < 0.0) {
        game::fall(jumping, false, dt, move);
    }
    CHECK(y == -1.0); // bumped its head
    CHECK_FALSE(jumping.on_ground);
    for (int i = 0; i < 60 && !jumping.on_ground; ++i) {
        game::fall(jumping, false, dt, move);
    }
    CHECK(jumping.on_ground);
    CHECK(y == 0.0);

    // From high up, it falls no faster than its fastest fall.
    ceiling = -1000.0;
    y = -500.0;
    jumping.on_ground = false;
    for (int i = 0; i < 120; ++i) {
        game::fall(jumping, false, dt, move);
    }
    CHECK(jumping.fall_speed == 20.0);
    // No gravity, or gravity that is not a number: it stays where it is.
    jumping.fall_speed = 0.0;
    jumping.gravity = std::numeric_limits<double>::quiet_NaN();
    const double before = y;
    game::fall(jumping, false, dt, move);
    CHECK(y == before);
}

TEST_CASE("Jumping: walking left and right, landing on solid tiles and bodies, and jumping") {
    Fixture f;
    f.w->add<game::Jumping>(f.hero);
    // Only now: adding a component moves the entity's others in memory.
    game::TopDownMover* mover = f.w->get<game::TopDownMover>(f.hero);
    mover->hitbox_height = 1.0; // the whole character, seen from the side
    // A solid floor whose top is y = 4, from x = -5 to 15.
    const world::Entity floor = f.place("floor", -5.0, 4.0);
    scene2d::Tilemap* tiles = f.w->add<scene2d::Tilemap>(floor);
    tiles->solid = true;
    tiles->tiles = "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0";
    const auto run = [&](int steps, std::initializer_list<input::Key> keys) {
        for (int i = 0; i < steps; ++i) {
            f.state.begin_step();
            for (const input::Key k : {input::Key::Left, input::Key::Right, input::Key::Up,
                                       input::Key::Down, input::Key::Space}) {
                bool down = false;
                for (const input::Key held : keys) {
                    down = down || held == k;
                }
                f.state.set(input::Binding::key(k), down);
            }
            game::move_characters(*f.w, f.state, 1.0 / 60.0, 16);
        }
    };
    const game::Jumping& jumping = *f.w->get<game::Jumping>(f.hero);

    // It falls from y = 0 and lands with its feet on the floor.
    run(60, {});
    CHECK(f.at(f.hero).y == 4.0);
    CHECK(jumping.on_ground);
    CHECK(f.at(f.hero).x == 0.0);
    // Down does nothing; Right walks right only, facing right.
    run(30, {input::Key::Down});
    CHECK(f.at(f.hero).y == 4.0);
    run(30, {input::Key::Right});
    CHECK(f.at(f.hero).x == doctest::Approx(2.0)); // half a second at 4 tiles a second
    CHECK(f.at(f.hero).y == 4.0);
    CHECK(mover->facing == game::Facing::Right);
    // Space jumps: it rises off the floor, then lands again.
    run(1, {});
    run(1, {input::Key::Space});
    CHECK(f.at(f.hero).y < 4.0);
    CHECK_FALSE(jumping.on_ground);
    run(10, {input::Key::Space});
    const double up_high = f.at(f.hero).y;
    CHECK(up_high < 2.5);
    run(60, {});
    CHECK(f.at(f.hero).y == 4.0);
    CHECK(jumping.on_ground);
    // Up jumps too.
    run(1, {input::Key::Up});
    CHECK(f.at(f.hero).y < 4.0);
    run(60, {});
    // A Solid Body is something to stand on.
    const world::Entity crate = f.place("crate", 2.0, 4.0);
    game::Body* box = f.w->add<game::Body>(crate);
    box->width = 1.0;
    box->height = 1.0; // its top is y = 3
    run(1, {input::Key::Space});
    run(60, {});
    CHECK(f.at(f.hero).y == 3.0);
    CHECK(jumping.on_ground);
    // Walking off it, it falls back to the floor.
    run(30, {input::Key::Right});
    run(30, {});
    CHECK(f.at(f.hero).y == 4.0);
    // Disabled, it does not walk or jump, but it still falls.
    mover->enabled = false;
    f.at(f.hero).y = 1.0;
    run(60, {input::Key::Space, input::Key::Left});
    CHECK(f.at(f.hero).y == 4.0);
    CHECK(f.at(f.hero).x == doctest::Approx(4.0));
}

namespace {

/// A room 10 tiles by 7 inside a wall a tile thick, with a wall down the middle (column 5)
/// from the top to row 4, so the way between the halves goes round its bottom.
world::Entity build_room(Fixture& f) {
    const world::Entity walls = f.place("walls", 0.0, 0.0);
    scene2d::Tilemap* tiles = f.w->add<scene2d::Tilemap>(walls);
    REQUIRE(tiles != nullptr);
    tiles->solid = true;
    tiles->tiles = "0,0,0,0,0,0,0,0,0,0,0,0\n"
                   "0,,,,,0,,,,,,0\n"
                   "0,,,,,0,,,,,,0\n"
                   "0,,,,,0,,,,,,0\n"
                   "0,,,,,0,,,,,,0\n"
                   "0,,,,,,,,,,,0\n"
                   "0,,,,,,,,,,,0\n"
                   "0,0,0,0,0,0,0,0,0,0,0,0";
    return walls;
}

/// Steps the walkers and the movers together, `steps` of 1/60 s, no keys held. Returns
/// who arrived, in order.
std::vector<world::Entity> run(Fixture& f, int steps, bool paused = false) {
    std::vector<world::Entity> arrived;
    for (int i = 0; i < steps; ++i) {
        f.state.begin_step();
        for (const world::Entity e : game::steer_walkers(*f.w, 1.0 / 60.0, 16, paused)) {
            arrived.push_back(e);
        }
        game::move_characters(*f.w, f.state, 1.0 / 60.0, 16);
    }
    return arrived;
}

} // namespace

TEST_CASE("Paths go round walls, tile by tile, never across a corner") {
    Fixture f;
    build_room(f);
    const game::TopDownMover mover;
    // From the left half to the right half: round the bottom of the middle wall.
    const auto way = game::find_path(*f.w, mover, f.hero, 2.5, 2.75, 8.5, 2.75, 16);
    REQUIRE(way.has_value());
    const std::vector<std::array<double, 2>> points =
        way.value_or(std::vector<std::array<double, 2>>{});
    REQUIRE(points.size() >= 2);
    CHECK(points.back() == std::array<double, 2>{8.5, 2.75});
    bool below_the_wall = false;
    for (const auto& [px, py] : points) {
        below_the_wall = below_the_wall || py > 5.0;
        // No point is inside a wall.
        CHECK_FALSE(scene2d::solid_at(*f.w, game::hitbox(mover, px, py), 16));
    }
    CHECK(below_the_wall);
    // Already there: no walking. Into a wall, or past the search: no way.
    CHECK(game::find_path(*f.w, mover, f.hero, 2.5, 2.75, 2.5, 2.75, 16)
              .value_or(std::vector<std::array<double, 2>>{{0.0, 0.0}})
              .empty());
    CHECK_FALSE(game::find_path(*f.w, mover, f.hero, 2.5, 2.75, 5.5, 2.75, 16).has_value());
    CHECK_FALSE(game::find_path(*f.w, mover, f.hero, 2.5, 2.75, 500.5, 2.75, 16).has_value());
    // A Solid Body in the gap closes it; the body to ignore (who is followed) does not.
    const world::Entity crate = f.place("crate", 5.5, 6.75);
    f.w->add<game::Body>(crate)->height = 2.0;
    const world::Entity crate2 = f.place("crate2", 5.5, 5.75);
    f.w->add<game::Body>(crate2);
    CHECK_FALSE(game::find_path(*f.w, mover, f.hero, 2.5, 2.75, 8.5, 2.75, 16).has_value());
    f.w->get<game::Body>(crate2)->enabled = false;
    CHECK_FALSE(game::find_path(*f.w, mover, f.hero, 2.5, 2.75, 8.5, 2.75, 16).has_value());
    CHECK(game::find_path(*f.w, mover, f.hero, 2.5, 2.75, 8.5, 2.75, 16, crate).has_value());
}

TEST_CASE("A Walker sent somewhere walks there round walls and says it arrived") {
    Fixture f;
    build_room(f);
    f.at(f.hero) = scene2d::Transform2D{2.5, 2.75};
    const world::Entity guard = f.place("guard", 2.5, 4.75);
    REQUIRE(f.w->add<game::TopDownMover>(guard) != nullptr);
    game::Walker* walker = f.w->add<game::Walker>(guard);
    REQUIRE(walker != nullptr);
    walker->walk_to(8.5, 2.75);
    // The keys move the hero, never the walker.
    const std::vector<world::Entity> arrived = run(f, 5 * 60);
    REQUIRE(arrived.size() == 1);
    CHECK(arrived.front() == guard);
    CHECK(f.at(guard).x == doctest::Approx(8.5).epsilon(0.001));
    CHECK(f.at(guard).y == doctest::Approx(2.75).epsilon(0.001));
    CHECK(f.w->get<game::Walker>(guard)->mode == game::WalkMode::Stand);
    // The hero, not the walker, is the player, whoever comes first.
    CHECK(game::player_position(*f.w) == std::array<double, 2>{2.5, 2.75});
    // A goal inside a wall: it stays where it is.
    f.w->get<game::Walker>(guard)->walk_to(5.5, 2.75);
    CHECK(run(f, 30).empty());
    CHECK(f.at(guard).x == doctest::Approx(8.5).epsilon(0.001));
}

TEST_CASE("Walkers patrol their route, follow, wander near home, and stand while paused") {
    Fixture f;
    build_room(f);
    const world::Entity guard = f.place("guard", 1.5, 1.75);
    f.w->add<game::TopDownMover>(guard);
    game::Walker* walker = f.w->add<game::Walker>(guard);
    REQUIRE(walker != nullptr);
    walker->mode = game::WalkMode::Patrol;
    walker->route = "3.5, 1.75; 3.5,4.75 ; not a point; 1.5,4.75";
    walker->rest = 0.25;
    // Round the route: each point in turn, resting at each.
    std::vector<std::array<double, 2>> visited;
    for (int i = 0; i < 8 * 60; ++i) {
        run(f, 1);
        const std::array<double, 2> here{f.at(guard).x, f.at(guard).y};
        for (const std::array<double, 2> point :
             {std::array<double, 2>{3.5, 1.75}, std::array<double, 2>{3.5, 4.75},
              std::array<double, 2>{1.5, 4.75}}) {
            if (std::abs(here[0] - point[0]) < 0.03 && std::abs(here[1] - point[1]) < 0.03 &&
                (visited.empty() || visited.back() != point)) {
                visited.push_back(point);
            }
        }
    }
    REQUIRE(visited.size() >= 4);
    CHECK(visited[0] == std::array<double, 2>{3.5, 1.75});
    CHECK(visited[1] == std::array<double, 2>{3.5, 4.75});
    CHECK(visited[2] == std::array<double, 2>{1.5, 4.75});
    CHECK(visited[3] == std::array<double, 2>{3.5, 1.75});

    // Following: it comes round the wall to the hero, and stops near.
    f.at(f.hero) = scene2d::Transform2D{9.5, 1.75};
    f.w->get<game::Walker>(guard)->follow("hero");
    run(f, 6 * 60);
    const double near = std::hypot(f.at(guard).x - 9.5, f.at(guard).y - 1.75);
    CHECK(near <= 1.5 + 0.1);
    CHECK(near > 0.5);

    // Paused (a message open): nobody walks.
    f.at(f.hero) = scene2d::Transform2D{9.5, 6.75};
    const scene2d::Transform2D before = f.at(guard);
    run(f, 60, true);
    CHECK(f.at(guard).x == before.x);
    CHECK(f.at(guard).y == before.y);

    // Wandering stays near home, and replays the same every run.
    f.w->get<game::Walker>(guard)->wander();
    f.w->get<game::Walker>(guard)->wander_radius = 2.0;
    run(f, 1);
    const double home_x = f.w->get<game::Walker>(guard)->home_x;
    const double home_y = f.w->get<game::Walker>(guard)->home_y;
    double farthest = 0.0;
    std::vector<double> track;
    for (int i = 0; i < 10 * 60; ++i) {
        run(f, 1);
        farthest = std::max(farthest, std::hypot(f.at(guard).x - home_x, f.at(guard).y - home_y));
        track.push_back(f.at(guard).x);
    }
    CHECK(farthest > 0.5);
    CHECK(farthest <= 2.0 * std::sqrt(2.0) + 0.1);
}

TEST_CASE("Walkers do not set off trigger areas or interact") {
    const Fixture f;
    const world::Entity guard = f.place("guard", 0.0, 0.0);
    f.w->add<game::TopDownMover>(guard);
    f.w->add<game::Walker>(guard)->mode = game::WalkMode::Stand;
    f.at(f.hero) = scene2d::Transform2D{10.0, 10.0};
    const world::Entity door = f.place("door", 0.0, 0.0);
    f.w->add<game::Trigger>(door);
    CHECK(game::update_triggers(*f.w).empty());
    const world::Entity sign = f.place("sign", 0.0, 0.5);
    f.w->add<game::Interactable>(sign);
    CHECK_FALSE(game::find_interaction(*f.w, guard).is_valid());
    // Turned off, it is a mover like any other.
    f.w->get<game::Walker>(guard)->enabled = false;
    CHECK(game::update_triggers(*f.w).size() == 1);
}

TEST_CASE("The camera follows, smooths, keeps a dead zone, looks ahead and shakes") {
    Fixture f;
    game::CameraObject camera;
    // By default it is exactly on the player, from the first step.
    f.at(f.hero) = scene2d::Transform2D{3.0, 4.0};
    CHECK(game::camera_view(camera, *f.w, 0) == std::array<double, 2>{3.0, 4.0});
    game::update_camera(camera, *f.w, 1.0 / 60.0);
    CHECK(camera.placed);
    f.at(f.hero).x = 5.0;
    game::update_camera(camera, *f.w, 1.0 / 60.0);
    CHECK(camera.x == 5.0);

    // Smoothing: it closes part of the gap each step, the same however steps are cut.
    camera.smoothing = 0.2;
    f.at(f.hero).x = 7.0;
    game::update_camera(camera, *f.w, 0.1);
    const double after_one = camera.x;
    CHECK(after_one > 5.0);
    CHECK(after_one < 7.0);
    game::CameraObject twice = camera;
    twice.x = 5.0;
    game::update_camera(twice, *f.w, 0.05);
    game::update_camera(twice, *f.w, 0.05);
    CHECK(twice.x == doctest::Approx(after_one));
    for (int i = 0; i < 120; ++i) {
        game::update_camera(camera, *f.w, 1.0 / 60.0);
    }
    CHECK(camera.x == doctest::Approx(7.0).epsilon(0.001));

    // A dead zone of 2 tiles: moving within a tile of the middle leaves the camera.
    camera.smoothing = 0.0;
    camera.dead_zone = 2.0;
    f.at(f.hero).x = 7.8;
    game::update_camera(camera, *f.w, 1.0 / 60.0);
    CHECK(camera.x == doctest::Approx(7.0));
    f.at(f.hero).x = 9.0;
    game::update_camera(camera, *f.w, 1.0 / 60.0);
    CHECK(camera.x == doctest::Approx(8.0)); // the target on the zone's edge
    camera.dead_zone = 0.0;

    // Look ahead: the way the player walks, turning smoothly.
    camera.look_ahead = 2.0;
    f.walk(60, {engine::input::Key::Right});
    for (int i = 0; i < 60; ++i) {
        game::update_camera(camera, *f.w, 1.0 / 60.0);
        f.walk(1, {engine::input::Key::Right});
    }
    CHECK(camera.ahead_x == doctest::Approx(2.0));
    CHECK(camera.x == doctest::Approx(f.at(f.hero).x + 2.0).epsilon(0.01));
    camera.look_ahead = 0.0;

    // Someone else, then a fixed place, then the player again.
    const world::Entity elder = f.place("elder", -4.0, 1.0);
    camera.follow_entity("elder");
    camera.ahead_x = 0.0;
    game::update_camera(camera, *f.w, 1.0 / 60.0);
    CHECK(camera.x == -4.0);
    CHECK(camera.y == 1.0);
    static_cast<void>(elder);
    camera.look_at(10.0, 10.0);
    game::update_camera(camera, *f.w, 1.0 / 60.0);
    CHECK(camera.x == 10.0);
    camera.follow_player();
    game::update_camera(camera, *f.w, 1.0 / 60.0);
    CHECK(camera.x == f.at(f.hero).x);

    // Shaking: around where it is, fading out, the same for the same step.
    camera.shake(0.5, 1.0);
    const std::array<double, 2> shaken = game::camera_view(camera, *f.w, 7);
    CHECK(shaken == game::camera_view(camera, *f.w, 7));
    CHECK(std::abs(shaken[0] - camera.x) <= 0.5);
    CHECK(game::camera_view(camera, *f.w, 7) != game::camera_view(camera, *f.w, 8));
    for (int i = 0; i < 61; ++i) {
        game::update_camera(camera, *f.w, 1.0 / 60.0);
    }
    CHECK(game::camera_view(camera, *f.w, 7) == std::array<double, 2>{camera.x, camera.y});
    camera.shake(-1.0, 1.0); // nonsense is ignored
    CHECK(camera.shake_left == 0.0);
}

TEST_CASE("Encounter Areas start battles as the player walks inside them") {
    const Fixture f;
    const world::Entity grass = f.place("grass", 5.0, 5.0);
    game::EncounterArea* area = f.w->add<game::EncounterArea>(grass);
    REQUIRE(area != nullptr);
    area->groups = "slime, slime | bat";
    area->steps_min = 3.0;
    area->steps_max = 3.0;
    // Outside: nothing however far.
    f.at(f.hero) = scene2d::Transform2D{20.0, 20.0};
    CHECK_FALSE(game::update_encounters(*f.w, 100.0).has_value());
    // Inside: after 3 tiles walked, a battle against one of its groups.
    f.at(f.hero) = scene2d::Transform2D{5.0, 4.0};
    CHECK_FALSE(game::update_encounters(*f.w, 1.0).has_value());
    CHECK_FALSE(game::update_encounters(*f.w, 1.5).has_value());
    const std::optional<std::string> enemies = game::update_encounters(*f.w, 0.5);
    REQUIRE(enemies.has_value());
    if (!enemies) {
        return;
    }
    const bool known = *enemies == "slime, slime " || *enemies == " bat";
    CHECK(known);
    // Counting starts again; off, none.
    CHECK_FALSE(game::update_encounters(*f.w, 2.0).has_value());
    f.w->get<game::EncounterArea>(grass)->enabled = false;
    CHECK_FALSE(game::update_encounters(*f.w, 10.0).has_value());
}

TEST_CASE("Enemies on the Map: walking into one starts its battle, once, until beaten") {
    const Fixture f;
    const world::Entity slime = f.place("slime", 5.0, 5.0);
    game::EnemyOnMap* enemy = f.w->add<game::EnemyOnMap>(slime);
    REQUIRE(enemy != nullptr);
    enemy->enemies = "slime";
    f.at(f.hero) = scene2d::Transform2D{1.0, 1.0};
    CHECK_FALSE(game::update_enemies_on_map(*f.w).has_value());
    // Walking in: once, not again while standing in it.
    f.at(f.hero) = scene2d::Transform2D{5.2, 4.8};
    CHECK(game::update_enemies_on_map(*f.w) == slime);
    CHECK_FALSE(game::update_enemies_on_map(*f.w).has_value());
    // Out and in again: again.
    f.at(f.hero) = scene2d::Transform2D{1.0, 1.0};
    CHECK_FALSE(game::update_enemies_on_map(*f.w).has_value());
    f.at(f.hero) = scene2d::Transform2D{5.0, 5.0};
    CHECK(game::update_enemies_on_map(*f.w) == slime);
    // Beaten: never.
    f.at(f.hero) = scene2d::Transform2D{1.0, 1.0};
    static_cast<void>(game::update_enemies_on_map(*f.w));
    f.w->get<game::EnemyOnMap>(slime)->beaten = true;
    f.at(f.hero) = scene2d::Transform2D{5.0, 5.0};
    CHECK_FALSE(game::update_enemies_on_map(*f.w).has_value());
}

TEST_CASE("Platformer jumping: coyote time, an early press, and a tap that only hops") {
    double y = 0.0;
    double floor_at = 0.0;
    const auto move = [&](double wanted) {
        const double to = std::min(y + wanted, floor_at);
        const double moved = to - y;
        y = to;
        return moved;
    };
    constexpr double dt = 1.0 / 60.0;
    const auto rise = [&](game::Jumping& jumping, bool hold) {
        game::fall(jumping, game::JumpInput{true, hold}, dt, move);
        double highest = y;
        for (int i = 0; i < 600 && !jumping.on_ground; ++i) {
            game::fall(jumping, game::JumpInput{false, hold}, dt, move);
            highest = std::min(highest, y);
        }
        return -highest;
    };
    game::Jumping jumping; // jump 3, a short jump 1
    game::fall(jumping, game::JumpInput{}, dt, move);
    REQUIRE(jumping.on_ground);
    // Held: the full height; tapped: about the short one.
    CHECK(rise(jumping, true) > 2.8);
    const double hop = rise(jumping, false);
    CHECK(hop > 0.8);
    CHECK(hop < 1.2);

    // Walked off a ledge: a press within coyote time still leaps; once only.
    floor_at = 100.0; // nothing below now
    game::fall(jumping, game::JumpInput{}, dt, move);
    game::fall(jumping, game::JumpInput{}, dt, move);
    CHECK_FALSE(jumping.on_ground);
    const double before = y;
    game::fall(jumping, game::JumpInput{true, true}, dt, move);
    CHECK(jumping.fall_speed < 0.0); // leapt
    CHECK(y < before);
    for (int i = 0; i < 30; ++i) {
        game::fall(jumping, game::JumpInput{}, dt, move);
    }
    const double falling = jumping.fall_speed;
    game::fall(jumping, game::JumpInput{true, true}, dt, move);
    CHECK(jumping.fall_speed > falling); // no second leap in the air

    // Late, after coyote time: no leap.
    game::Jumping late;
    y = 0.0;
    floor_at = 0.0;
    game::fall(late, game::JumpInput{}, dt, move);
    floor_at = 100.0;
    for (int i = 0; i < 20; ++i) { // a third of a second in the air
        game::fall(late, game::JumpInput{}, dt, move);
    }
    game::fall(late, game::JumpInput{true, true}, dt, move);
    CHECK(late.fall_speed > 0.0);

    // Pressed just before landing: it leaps as it lands.
    game::Jumping early;
    y = -0.05;
    floor_at = 0.0;
    early.fall_speed = 3.0;                                   // falling onto the floor
    game::fall(early, game::JumpInput{true, true}, dt, move); // not landed yet
    for (int i = 0; i < 3 && early.fall_speed >= 0.0; ++i) {
        game::fall(early, game::JumpInput{false, true}, dt, move);
    }
    CHECK(early.fall_speed < 0.0); // leapt on landing
}

TEST_CASE("Moving platforms: back and forth, carrying what stands on them") {
    const Fixture f;
    // A platform 2 tiles wide, its top at y = 4.5, going 3 tiles right at 1 a second, with
    // a stop of half a second at each end.
    const world::Entity lift = f.place("lift", 5.0, 5.0);
    f.w->add<game::Body>(lift)->width = 2.0;
    game::MovingPlatform* platform = f.w->add<game::MovingPlatform>(lift);
    platform->move_x = 3.0;
    platform->speed = 1.0;
    platform->wait = 0.5;
    // The hero stands on it (its feet on the top); a second mover stands beside it.
    f.w->get<scene2d::Transform2D>(f.hero)->x = 5.0;
    f.w->get<scene2d::Transform2D>(f.hero)->y = 4.5;
    const world::Entity bystander = f.place("bystander", 8.0, 5.0);
    f.w->add<game::TopDownMover>(bystander);
    constexpr std::uint32_t ppu = 16;
    for (int i = 0; i < 60; ++i) {
        game::move_platforms(*f.w, 1.0 / 60.0, ppu);
    }
    CHECK(f.w->get<scene2d::Transform2D>(lift)->x == doctest::Approx(6.0));
    CHECK(f.w->get<scene2d::Transform2D>(f.hero)->x == doctest::Approx(6.0)); // carried
    CHECK(f.w->get<scene2d::Transform2D>(bystander)->x == 8.0);               // not on it
    // To the end (3 tiles), a stop, and back: home after 3 + 0.5 + 3 + 0.5 = 7 seconds.
    for (int i = 0; i < 60 * 6; ++i) {
        game::move_platforms(*f.w, 1.0 / 60.0, ppu);
    }
    CHECK(f.w->get<scene2d::Transform2D>(lift)->x == doctest::Approx(5.0).epsilon(0.001));
    CHECK(f.w->get<scene2d::Transform2D>(f.hero)->x == doctest::Approx(5.0).epsilon(0.001));
    // Switched off: it stays.
    platform->enabled = false;
    game::move_platforms(*f.w, 1.0, ppu);
    CHECK(f.w->get<scene2d::Transform2D>(lift)->x == doctest::Approx(5.0).epsilon(0.001));
}

TEST_CASE("Ladders: climbed with up and down, never fallen from, leapt off with jump") {
    Fixture f;
    game::TopDownMover* mover = f.w->get<game::TopDownMover>(f.hero);
    mover->hitbox_height = 1.0;
    game::Jumping* jumping = f.w->add<game::Jumping>(f.hero);
    // A floor at y = 4, and a ladder 3 tiles tall standing on it at x = 0 (its top y = 1).
    const world::Entity floor = f.place("floor", -5.0, 4.0);
    scene2d::Tilemap* tiles = f.w->add<scene2d::Tilemap>(floor);
    tiles->solid = true;
    tiles->tiles = "0,0,0,0,0,0,0,0,0,0";
    const world::Entity ladder = f.place("ladder", 0.0, 4.0);
    f.w->add<game::Ladder>(ladder);
    const auto run = [&](int steps, std::initializer_list<input::Key> keys) {
        for (int i = 0; i < steps; ++i) {
            f.state.begin_step();
            for (const input::Key k : {input::Key::Left, input::Key::Right, input::Key::Up,
                                       input::Key::Down, input::Key::Space}) {
                bool down = false;
                for (const input::Key held : keys) {
                    down = down || held == k;
                }
                f.state.set(input::Binding::key(k), down);
            }
            game::move_characters(*f.w, f.state, 1.0 / 60.0, 16);
        }
    };
    f.at(f.hero).y = 4.0;
    run(5, {});
    REQUIRE(jumping->on_ground);
    // Up climbs, at 3 tiles a second, and does not jump.
    run(30, {input::Key::Up});
    CHECK(jumping->climbing);
    CHECK(f.at(f.hero).y == doctest::Approx(2.5));
    // Let go: it stays there, not falling.
    run(30, {});
    CHECK(f.at(f.hero).y == doctest::Approx(2.5));
    // Up to the top, no higher.
    run(60, {input::Key::Up});
    CHECK(f.at(f.hero).y == doctest::Approx(1.0).epsilon(0.01));
    CHECK(jumping->climbing);
    // Down to the floor, standing on it.
    run(120, {input::Key::Down});
    CHECK(f.at(f.hero).y == 4.0);
    CHECK(jumping->on_ground);
    // Climbing, then jump: it leaps off and falls back.
    run(20, {input::Key::Up});
    run(1, {input::Key::Space});
    CHECK_FALSE(jumping->climbing);
    CHECK(jumping->fall_speed < 0.0); // leaping
    // Walked off sideways, it is off the ladder and falls to the floor.
    run(40, {input::Key::Right});
    run(120, {});
    CHECK_FALSE(jumping->climbing);
    CHECK(f.at(f.hero).y == 4.0);
}

TEST_CASE("Slopes: walked up and down, the feet on the surface, onto the ground above") {
    Fixture f;
    f.w->add<game::Jumping>(f.hero);
    game::TopDownMover* mover = f.w->get<game::TopDownMover>(f.hero);
    mover->hitbox_height = 1.0;
    // The ground's top at y = 4; a slope rising right in column 3 (from y 4 to 3), then
    // higher ground (top y = 3) in columns 4 and 5.
    const world::Entity map = f.place("ground", 0.0, 0.0);
    scene2d::Tilemap* tiles = f.w->add<scene2d::Tilemap>(map);
    tiles->solid = true;
    tiles->tiles = ",,,,,\n,,,,,\n,,,,,\n,,,1,0,0\n0,0,0,0,0,0";
    tiles->shapes = "1 slope_right";
    const auto run = [&](int steps, std::initializer_list<input::Key> keys) {
        for (int i = 0; i < steps; ++i) {
            f.state.begin_step();
            for (const input::Key k : {input::Key::Left, input::Key::Right}) {
                bool down = false;
                for (const input::Key held : keys) {
                    down = down || held == k;
                }
                f.state.set(input::Binding::key(k), down);
            }
            game::move_characters(*f.w, f.state, 1.0 / 60.0, 16);
        }
    };
    CHECK(scene2d::slope_floor(*f.w, 3.25, 3.0, 4.0, 16).value_or(0.0) == doctest::Approx(3.75));
    f.at(f.hero).x = 1.0;
    f.at(f.hero).y = 4.0;
    run(5, {});
    CHECK(f.at(f.hero).y == 4.0);
    // Right, up the slope, and on along the higher ground.
    const game::Jumping& jumping = *f.w->get<game::Jumping>(f.hero);
    run(60, {input::Key::Right}); // 4 tiles a second: to x = 5
    CHECK(f.at(f.hero).x > 4.5);
    CHECK(f.at(f.hero).y == doctest::Approx(3.0));
    CHECK(jumping.on_ground);
    // Back left, down the slope, never in the air on the way.
    bool always_down = true;
    for (int i = 0; i < 40; ++i) {
        run(1, {input::Key::Left});
        always_down = always_down && jumping.on_ground;
    }
    CHECK(always_down);
    CHECK(f.at(f.hero).x < 2.5);
    CHECK(f.at(f.hero).y == doctest::Approx(4.0));
}

TEST_CASE("Health: hits hurt by tag, keep it safe a moment, knock it back, and defeat it") {
    Fixture f;
    f.at(f.hero) = scene2d::Transform2D{3.0, 3.0};
    f.w->add<scene2d::Sprite>(f.hero);
    game::Health* hp = f.w->add<game::Health>(f.hero);
    REQUIRE(hp != nullptr);
    scene2d::Sprite* look = f.w->get<scene2d::Sprite>(f.hero); // after adding: it moves
    REQUIRE(look != nullptr);
    hp->hurt_by = "Enemy";
    const world::Entity slime = f.place("slime", 2.0, 3.0);
    f.w->add<game::Tags>(slime)->tags = "enemy, small";
    f.w->add<game::Health>(slime)->damage = 2.0;
    const world::Entity rock = f.place("rock", 4.0, 3.0);

    // A rock is not an enemy: nothing.
    const std::array<scene2d::HitContact, 1> by_rock{scene2d::HitContact{rock, f.hero}};
    CHECK(game::apply_hits(*f.w, by_rock).empty());
    CHECK(hp->health == 3.0);

    // The slime: 2 taken, safe, pushed right, away from it.
    const std::array<scene2d::HitContact, 1> by_slime{scene2d::HitContact{slime, f.hero}};
    const std::vector<game::Hurt> hurts = game::apply_hits(*f.w, by_slime);
    REQUIRE(hurts.size() == 1);
    CHECK(hurts[0].target == f.hero);
    CHECK(hurts[0].attacker == slime);
    CHECK(hurts[0].amount == 2.0);
    CHECK_FALSE(hurts[0].defeated);
    CHECK(hp->health == 1.0);
    CHECK(hp->safe == doctest::Approx(0.8));
    CHECK(hp->flash_time == doctest::Approx(0.12));
    CHECK(game::apply_hits(*f.w, by_slime).empty()); // safe
    // While pushed the keys do not move it.
    f.walk(1, {input::Key::Left});
    CHECK(f.at(f.hero).x == 3.0);
    bool blinked = false;
    game::update_health(*f.w, 1.0 / 60.0, 16);
    CHECK(look->flash == doctest::Approx((0.12 - (1.0 / 60.0)) / 0.12));
    for (int i = 1; i < 9; ++i) {
        game::update_health(*f.w, 1.0 / 60.0, 16);
        blinked = blinked || look->tint_a < 255;
    }
    CHECK(look->flash == 0.0); // over in 0.12 s
    CHECK(f.at(f.hero).x == doctest::Approx(4.0));
    CHECK(f.at(f.hero).y == 3.0);
    CHECK(hp->push_time == 0.0);
    f.walk(15, {input::Key::Left}); // it walks again, 4 tiles a second
    CHECK(f.at(f.hero).x == doctest::Approx(3.0));
    for (int i = 0; i < 60; ++i) {
        game::update_health(*f.w, 1.0 / 60.0, 16);
        blinked = blinked || look->tint_a < 255;
    }
    CHECK(blinked);
    CHECK(hp->safe == 0.0);
    CHECK(look->tint_a == 255); // shown as it was

    // Again: defeated, hidden, still; then revived and shown.
    const std::vector<game::Hurt> last = game::apply_hits(*f.w, by_slime);
    REQUIRE(last.size() == 1);
    CHECK(last[0].defeated);
    CHECK(hp->health == 0.0);
    CHECK(hp->defeated);
    // It dissolves away over 0.4 s, then is hidden, whole again.
    game::update_health(*f.w, 0.2, 16);
    CHECK(look->visible);
    CHECK(look->dissolve == doctest::Approx(0.5));
    game::update_health(*f.w, 0.2, 16);
    CHECK_FALSE(look->visible);
    CHECK(look->dissolve == 0.0);
    CHECK(hp->push_time == 0.0); // no knockback for the defeated
    const double x = f.at(f.hero).x;
    f.walk(10, {input::Key::Right});
    CHECK(f.at(f.hero).x == x);
    // A defeated one hurts nothing.
    f.w->get<game::Health>(slime)->hurt_by = "";
    const std::array<scene2d::HitContact, 1> by_hero{scene2d::HitContact{f.hero, slime}};
    CHECK(game::apply_hits(*f.w, by_hero).empty());
    hp->revive();
    game::update_health(*f.w, 1.0 / 60.0, 16);
    CHECK(look->visible);
    CHECK(hp->health == 3.0);
    // Something with no Health (spikes) takes 1, from one hurt by anything.
    const std::array<scene2d::HitContact, 1> by_spikes{scene2d::HitContact{rock, slime}};
    REQUIRE(game::apply_hits(*f.w, by_spikes).size() == 1);
    game::Health& slime_hp = *f.w->get<game::Health>(slime);
    CHECK(slime_hp.health == 2.0);
    // Healing stops at the most; logic's hurt takes health as hits do.
    slime_hp.heal(5.0);
    CHECK(slime_hp.health == 3.0);
    slime_hp.hurt(std::numeric_limits<double>::quiet_NaN());
    slime_hp.hurt(-1.0);
    CHECK(slime_hp.health == 3.0);
    slime_hp.hurt(3.0);
    CHECK(slime_hp.defeated);
    CHECK(game::parse_tag_list(" Enemy,flying  enemy;x-y ") ==
          std::vector<std::string>{"enemy", "flying", "x", "y"});
}

TEST_CASE("Knockback stops at walls") {
    Fixture f;
    build_room(f);
    f.at(f.hero) = scene2d::Transform2D{10.5, 6.75};
    f.w->add<game::Health>(f.hero)->knockback = 3.0;
    const world::Entity spikes = f.place("spikes", 9.5, 6.75);
    const std::array<scene2d::HitContact, 1> hit{scene2d::HitContact{spikes, f.hero}};
    REQUIRE(game::apply_hits(*f.w, hit).size() == 1);
    for (int i = 0; i < 30; ++i) {
        game::update_health(*f.w, 1.0 / 60.0, 16);
    }
    // The wall starts at x = 11; the hitbox reaches half its width right of the feet.
    const double right =
        f.at(f.hero).x + (f.w->get<game::TopDownMover>(f.hero)->hitbox_width / 2.0);
    CHECK(right == doctest::Approx(11.0));
}

TEST_CASE("Line of sight is blocked by solid tiles") {
    Fixture f;
    build_room(f);
    CHECK(scene2d::line_of_sight(*f.w, 2.5, 6.0, 9.5, 6.0, 16));
    CHECK_FALSE(scene2d::line_of_sight(*f.w, 2.5, 3.0, 7.5, 3.0, 16));  // the middle wall
    CHECK_FALSE(scene2d::line_of_sight(*f.w, 2.5, 3.0, 2.5, -3.0, 16)); // the outer wall
    CHECK(scene2d::line_of_sight(*f.w, 2.5, 3.0, 2.5, 3.0, 16));
    CHECK_FALSE(
        scene2d::line_of_sight(*f.w, 2.5, std::numeric_limits<double>::quiet_NaN(), 3.0, 3.0, 16));
}

namespace {

/// run(), also gathering who saw or lost the player.
void watch(Fixture& f, int steps, std::vector<std::pair<world::Entity, bool>>& sightings) {
    for (int i = 0; i < steps; ++i) {
        f.state.begin_step();
        static_cast<void>(game::steer_walkers(*f.w, 1.0 / 60.0, 16, false, &sightings));
        game::move_characters(*f.w, f.state, 1.0 / 60.0, 16);
    }
}

} // namespace

TEST_CASE("Walkers that see the player chase or flee, and give up when far") {
    Fixture f;
    build_room(f);
    const world::Entity enemy = f.place("enemy", 2.5, 3.5);
    f.w->add<game::TopDownMover>(enemy)->speed = 3.0;
    game::Walker* walker = f.w->add<game::Walker>(enemy);
    REQUIRE(walker != nullptr);
    walker->mode = game::WalkMode::Stand;
    walker->on_sight = game::Sighted::Chase;
    walker->sight = 5.0;
    std::vector<std::pair<world::Entity, bool>> sightings;
    using Seen = std::pair<world::Entity, bool>;

    // Behind the middle wall, five tiles away: not seen.
    f.at(f.hero) = scene2d::Transform2D{7.5, 3.5};
    watch(f, 30, sightings);
    CHECK(sightings.empty());
    CHECK(f.at(enemy).x == 2.5);

    // In the open below, four tiles away: seen, and chased to.
    f.at(f.hero) = scene2d::Transform2D{6.5, 6.5};
    f.at(enemy) = scene2d::Transform2D{2.5, 6.5};
    watch(f, 1, sightings);
    REQUIRE(sightings.size() == 1);
    CHECK(sightings[0] == Seen{enemy, true});
    watch(f, 90, sightings);
    CHECK(std::hypot(f.at(enemy).x - 6.5, f.at(enemy).y - 6.5) < 0.2);

    // Far away (more than 7.5 tiles): lost; it goes back to standing.
    f.at(f.hero) = scene2d::Transform2D{10.5, 1.5};
    f.at(enemy) = scene2d::Transform2D{1.5, 6.5};
    watch(f, 1, sightings);
    REQUIRE(sightings.size() == 2);
    CHECK(sightings[1] == Seen{enemy, false});
    watch(f, 30, sightings);
    CHECK(f.at(enemy).x == 1.5);

    // Fleeing: it runs away from the player.
    walker->on_sight = game::Sighted::Flee;
    f.at(f.hero) = scene2d::Transform2D{4.5, 6.5};
    f.at(enemy) = scene2d::Transform2D{6.5, 6.5};
    watch(f, 60, sightings);
    CHECK(sightings.back() == Seen{enemy, true});
    CHECK(f.at(enemy).x > 8.0);

    // A defeated player is not seen; one already chasing gives up.
    walker->on_sight = game::Sighted::Chase;
    f.at(f.hero) = scene2d::Transform2D{8.5, 6.5};
    watch(f, 1, sightings);
    CHECK(walker->alerted);
    f.w->add<game::Health>(f.hero)->defeated = true;
    watch(f, 1, sightings);
    CHECK_FALSE(walker->alerted);
    CHECK(sightings.back() == Seen{enemy, false});
}

TEST_CASE("Collision layers by tag: bodies that block some, areas that notice some") {
    Fixture f;
    f.w->add<game::Tags>(f.hero)->tags = "player";
    const world::Entity slime = f.place("slime", 0.0, 2.0);
    f.w->add<game::TopDownMover>(slime);
    f.w->add<game::Tags>(slime)->tags = "enemy";
    // A fence a tile right of both, stopping the player only.
    for (const auto& [name, y] : {std::pair{"fence_a", 0.0}, std::pair{"fence_b", 2.0}}) {
        const world::Entity fence = f.place(name, 1.5, y);
        game::Body* body = f.w->add<game::Body>(fence);
        body->width = 1.0;
        body->height = 1.0;
        body->blocks = "Player";
    }
    CHECK(game::body_blocks(*f.w->get<game::Body>(f.w->find_named("fence_a").at(0)), {"player"}));
    CHECK_FALSE(
        game::body_blocks(*f.w->get<game::Body>(f.w->find_named("fence_a").at(0)), {"enemy"}));
    CHECK(game::body_blocks(game::Body{}, {}));
    // Both walk right for a second: the hero stops at the fence, the slime goes through.
    f.walk(60, {input::Key::Right});
    const double hero_right =
        f.at(f.hero).x + (f.w->get<game::TopDownMover>(f.hero)->hitbox_width / 2.0);
    CHECK(hero_right == doctest::Approx(1.0));
    CHECK(f.at(slime).x == doctest::Approx(4.0));

    // A way found by a slime goes straight through the fence; the hero's goes round it.
    const game::TopDownMover& mover = *f.w->get<game::TopDownMover>(slime);
    const auto through = game::find_path(*f.w, mover, slime, 0.5, 2.0, 2.5, 2.0, 16);
    REQUIRE(through.has_value());
    CHECK(through.value_or(std::vector<std::array<double, 2>>{}).size() == 1);
    const auto round = game::find_path(*f.w, mover, f.hero, 0.5, 2.0, 2.5, 2.0, 16);
    REQUIRE(round.has_value());
    CHECK(round.value_or(std::vector<std::array<double, 2>>{}).size() > 1);

    // A trap only enemies set off, walking by themselves or not; and an area for anyone.
    const world::Entity trap = f.place("trap", 8.0, 6.0);
    game::Trigger* area = f.w->add<game::Trigger>(trap);
    area->width = 4.0;
    area->height = 4.0;
    area->only = "enemy";
    f.at(f.hero) = scene2d::Transform2D{8.0, 5.0};
    f.at(slime) = scene2d::Transform2D{8.5, 5.0};
    f.w->add<game::Walker>(slime)->mode = game::WalkMode::Stand;
    const std::vector<game::TriggerEvent> events = game::update_triggers(*f.w);
    REQUIRE(events.size() == 1);
    CHECK(events[0].who == slime);
    // With no tags given, Walkers are not noticed, and the player is.
    f.w->get<game::Trigger>(trap)->only.clear();
    const std::vector<game::TriggerEvent> plain = game::update_triggers(*f.w);
    REQUIRE(plain.size() == 2); // the slime leaves, the hero enters
    CHECK_FALSE(plain[0].entered);
    CHECK(plain[0].who == slime);
    CHECK(plain[1].entered);
    CHECK(plain[1].who == f.hero);
}
