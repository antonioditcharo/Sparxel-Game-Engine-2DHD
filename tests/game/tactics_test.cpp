// Tactics battles: reach, targets, damage, the player's turn by keys and clicks, the
// enemies' turn, and the end.

#include "engine/game/behaviours.h"
#include "engine/game/runtime.h"
#include "engine/game/tactics.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace game = engine::game;
namespace reflect = engine::reflect;
namespace scene2d = engine::scene2d;
namespace world = engine::world;
using game::Tile;

namespace {

/// A field 10 by 6 with a wall down column 4 from the top to row 3.
struct Field {
    Field() {
        game::register_types(registry);
        REQUIRE(registry.errors().empty());
        w = std::make_unique<world::World>(registry);
        REQUIRE(scene2d::register_components(*w));
        REQUIRE(game::register_behaviour_components(*w));
        const world::Entity walls = w->create("walls");
        w->add<scene2d::Transform2D>(walls);
        scene2d::Tilemap* tiles = w->add<scene2d::Tilemap>(walls);
        tiles->solid = true;
        tiles->tiles = ",,,,0,,,,,\\n,,,,0,,,,,\\n,,,,0,,,,,\\n,,,,0,,,,,\\n,,,,,,,,,\\n,,,,,,,,,";
    }
    world::Entity unit(const char* name, game::Team team, Tile at) const {
        const world::Entity e = w->create(name);
        scene2d::Transform2D* place = w->add<scene2d::Transform2D>(e);
        const auto [x, y] = game::stand_on(at);
        place->x = x;
        place->y = y;
        w->add<scene2d::Sprite>(e);
        game::TacticsUnit* unit = w->add<game::TacticsUnit>(e);
        unit->team = team;
        return e;
    }
    [[nodiscard]] Tile tile(world::Entity e) const {
        const scene2d::Transform2D* at = w->get<scene2d::Transform2D>(e);
        return game::tile_of(at->x, at->y);
    }
    reflect::Registry registry;
    std::unique_ptr<world::World> w;
};

bool has(const std::vector<Tile>& tiles, Tile t) {
    return std::find(tiles.begin(), tiles.end(), t) != tiles.end();
}

} // namespace

TEST_CASE("Tactics: tiles, reach round walls and the other side, targets in range, damage") {
    CHECK(game::tile_of(2.5, 3.0) == Tile{2, 2});
    CHECK(game::stand_on(Tile{2, 2}) == std::pair{2.5, 3.0});
    CHECK(game::tiles_apart(Tile{0, 0}, Tile{2, 3}) == 5);
    const Field f;
    const world::Entity knight = f.unit("knight", game::Team::Player, Tile{2, 1});
    const world::Entity archer = f.unit("archer", game::Team::Player, Tile{2, 2});
    const world::Entity slime = f.unit("slime", game::Team::Enemy, Tile{3, 1});
    f.w->get<game::TacticsUnit>(knight)->move = 3;
    const std::vector<Tile> reach = game::reachable_tiles(*f.w, knight, 16);
    REQUIRE_FALSE(reach.empty());
    CHECK(reach.front() == Tile{2, 1});      // its own tile first
    CHECK_FALSE(has(reach, Tile{2, 2}));     // the archer stands there...
    CHECK(has(reach, Tile{2, 3}));           // ...but the knight goes through
    CHECK_FALSE(has(reach, Tile{3, 1}));     // the slime's tile
    CHECK_FALSE(has(reach, Tile{4, 4 - 3})); // the wall
    CHECK_FALSE(has(reach, Tile{5, 1}));     // across the wall: the way round is too long
    CHECK(has(reach, Tile{0, 0}));
    CHECK_FALSE(has(reach, Tile{0, 4})); // 4 steps
    // Targets: beside, and with a longer range.
    CHECK(game::targets_from(*f.w, knight, Tile{2, 1}) == std::vector<world::Entity>{slime});
    CHECK(game::targets_from(*f.w, archer, Tile{2, 2}).empty());
    f.w->get<game::TacticsUnit>(archer)->range = 2;
    CHECK(game::targets_from(*f.w, archer, Tile{2, 2}) == std::vector<world::Entity>{slime});
    game::TacticsUnit strong;
    strong.attack = 7;
    game::TacticsUnit armoured;
    armoured.defense = 10;
    CHECK(game::tactics_damage(strong, game::TacticsUnit{}) == 6);
    CHECK(game::tactics_damage(strong, armoured) == 1); // at least 1
}

TEST_CASE("Tactics: a battle played with the keys and a click, the enemies' turn, the end") {
    const Field f;
    const world::Entity knight = f.unit("knight", game::Team::Player, Tile{1, 4});
    const world::Entity slime = f.unit("slime", game::Team::Enemy, Tile{6, 4});
    f.w->get<game::TacticsUnit>(slime)->hp = 6;
    f.w->get<game::TacticsUnit>(slime)->move = 2;
    game::TacticsObject battle;
    battle.start();
    std::vector<std::string> said;
    const auto step = [&](double seconds) {
        for (const auto& e : battle.update(*f.w, seconds, 16)) {
            said.push_back(e.name + (e.value.empty() ? "" : ":" + e.value));
        }
    };
    step(0.0);
    CHECK(said == std::vector<std::string>{"turn_started:player"});
    CHECK(battle.is_player_turn());
    CHECK(battle.turn() == 1);
    CHECK(battle.cursor() == Tile{1, 4}); // on the knight
    // Chosen with Interact; moved 4 right with the keys: next to the slime, which it attacks.
    battle.press(*f.w, "interact", 16);
    CHECK(battle.step() == game::TacticsObject::Step::Move);
    CHECK_FALSE(battle.moves().empty());
    for (int i = 0; i < 4; ++i) {
        battle.press(*f.w, "move_right", 16);
    }
    battle.press(*f.w, "interact", 16);
    CHECK(f.tile(knight) == Tile{5, 4});
    CHECK(battle.step() == game::TacticsObject::Step::Target);
    REQUIRE(battle.targets().size() == 1);
    battle.click(*f.w, Tile{6, 4}, 16);                 // the slime, clicked
    CHECK(f.w->get<game::TacticsUnit>(slime)->hp == 3); // 4 attack less 1 defence
    // The knight has acted: the enemies' turn; the slime strikes back after a moment.
    step(0.1);
    CHECK(said.back() == "turn_started:enemy");
    CHECK_FALSE(battle.is_player_turn());
    battle.press(*f.w, "interact", 16); // not the player's turn: nothing
    step(0.5);
    CHECK(f.w->get<game::TacticsUnit>(knight)->hp == 7);
    step(0.5);
    CHECK(said.back() == "turn_started:player");
    CHECK(battle.turn() == 2);
    // Turn two: the knight attacks from where it stands (Interact twice), and wins.
    battle.press(*f.w, "interact", 16);
    battle.press(*f.w, "interact", 16); // stays
    CHECK(battle.step() == game::TacticsObject::Step::Target);
    battle.press(*f.w, "move_right", 16);
    battle.press(*f.w, "interact", 16);
    step(0.0);
    CHECK(said == std::vector<std::string>{"turn_started:player", "turn_started:enemy",
                                           "turn_started:player", "unit_defeated:slime", "won"});
    CHECK_FALSE(f.w->get<scene2d::Sprite>(slime)->visible);
    CHECK(battle.phase() == game::TacticsObject::Phase::Won);
}

TEST_CASE("Tactics: enemies walk to the player's units, Cancel waits, and losing ends it") {
    const Field f;
    const world::Entity knight = f.unit("knight", game::Team::Player, Tile{0, 5});
    const world::Entity ogre = f.unit("ogre", game::Team::Enemy, Tile{8, 5});
    f.w->get<game::TacticsUnit>(knight)->hp = 3;
    f.w->get<game::TacticsUnit>(ogre)->attack = 9;
    f.w->get<game::TacticsUnit>(ogre)->move = 4;
    game::TacticsObject battle;
    battle.start();
    std::vector<std::string> said;
    const auto step = [&](double seconds) {
        for (const auto& e : battle.update(*f.w, seconds, 16)) {
            said.push_back(e.name);
        }
    };
    step(0.0);
    // The knight waits: chosen, then Cancel goes back; chosen, moved where it is, Cancel.
    battle.press(*f.w, "interact", 16);
    battle.press(*f.w, "cancel", 16);
    CHECK(battle.step() == game::TacticsObject::Step::Choose);
    battle.end_turn();
    step(0.0);
    step(0.5);
    // The ogre walked 4 towards the knight; not near enough yet.
    CHECK(f.tile(ogre) == Tile{4, 5});
    CHECK(f.w->get<game::TacticsUnit>(knight)->hp == 3);
    step(0.5);
    battle.end_turn();
    step(0.0);
    step(0.5);
    step(0.0);
    CHECK(f.tile(ogre) == Tile{1, 5});
    CHECK(f.w->get<game::TacticsUnit>(knight)->hp == 0);
    CHECK(said.back() == "lost");
    CHECK(battle.phase() == game::TacticsObject::Phase::Lost);
}
