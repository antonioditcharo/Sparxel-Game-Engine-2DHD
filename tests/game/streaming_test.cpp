// Large worlds: Streamed Scenes loaded as the player comes near, what comes in and what
// stays out, and taken away again as they leave, even after a saved game.

#include "engine/game/behaviours.h"
#include "engine/game/runtime.h"
#include "engine/game/streaming.h"
#include "engine/serialize/json.h"

#include <doctest/doctest.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace game = engine::game;
namespace reflect = engine::reflect;
namespace scene2d = engine::scene2d;
namespace serialize = engine::serialize;
namespace world = engine::world;

namespace {

struct Worlds {
    Worlds() {
        game::register_types(registry);
        REQUIRE(registry.errors().empty());
        main = make();
        // The streamed scene: a tree, a house with a door under it, its own player, a
        // screen item with no Position, and a Streamed Scene of its own.
        const std::unique_ptr<world::World> forest = make();
        const world::Entity tree = forest->create("tree");
        scene2d::Transform2D* at = forest->add<scene2d::Transform2D>(tree);
        at->x = 2.0;
        at->y = 3.0;
        const world::Entity house = forest->create("house");
        forest->add<scene2d::Transform2D>(house)->x = 5.0;
        const world::Entity door = forest->create("door");
        forest->add<scene2d::Transform2D>(door)->x = 1.0;
        REQUIRE(forest->set_parent(door, house));
        const world::Entity hero = forest->create("hero");
        forest->add<scene2d::Transform2D>(hero);
        forest->add<game::TopDownMover>(hero);
        forest->create("score");
        const world::Entity deeper = forest->create("deeper");
        forest->add<scene2d::Transform2D>(deeper);
        forest->add<game::SceneStream>(deeper)->scene = "maps/deeper.dscene";
        scene = serialize::scene_to_json(*forest);
    }

    std::unique_ptr<world::World> make() {
        auto w = std::make_unique<world::World>(registry);
        REQUIRE(scene2d::register_components(*w));
        REQUIRE(game::register_behaviour_components(*w));
        return w;
    }

    /// The main world's player, at (x, y).
    [[nodiscard]] world::Entity player(double x, double y) const {
        const world::Entity e = main->create("player");
        scene2d::Transform2D* at = main->add<scene2d::Transform2D>(e);
        at->x = x;
        at->y = y;
        main->add<game::TopDownMover>(e);
        return e;
    }

    game::SceneStreamer::Reader reader() {
        return [this](std::string_view path, std::string& error) -> std::optional<serialize::Json> {
            ++reads;
            if (path == "maps/forest.dscene") {
                return scene;
            }
            error = "there is no scene " + std::string(path) + " in assets/.";
            return std::nullopt;
        };
    }

    [[nodiscard]] const scene2d::Transform2D* at(std::string_view name) const {
        const std::vector<world::Entity> found = main->find_named(name);
        return found.size() == 1 ? main->get<scene2d::Transform2D>(found[0]) : nullptr;
    }

    reflect::Registry registry;
    std::unique_ptr<world::World> main;
    serialize::Json scene;
    int reads = 0;
};

} // namespace

TEST_CASE("Large worlds: how far a point is from a stream's area") {
    game::SceneStream stream;
    stream.width = 10.0;
    stream.height = 4.0;
    CHECK(game::distance_to_area(stream, 40.0, 0.0, 45.0, 2.0) == 0.0); // inside
    CHECK(game::distance_to_area(stream, 40.0, 0.0, 35.0, 2.0) == 5.0); // left of it
    CHECK(game::distance_to_area(stream, 40.0, 0.0, 53.0, 8.0) == doctest::Approx(5.0));
    CHECK(game::distance_to_area(stream, 40.0, 0.0, 45.0, -1.0) == 1.0); // above
    stream.width = std::nan("");
    CHECK(game::distance_to_area(stream, 40.0, 0.0, 40.0, 2.0) == 0.0);
}

TEST_CASE("Large worlds: a scene streamed in near the player, and away again") {
    Worlds w;
    // The main scene has a tree of its own, and the forest laid in at (40, 0).
    w.main->add<scene2d::Transform2D>(w.main->create("tree"));
    const world::Entity stream = w.main->create("forest");
    scene2d::Transform2D* place = w.main->add<scene2d::Transform2D>(stream);
    place->x = 40.0;
    game::SceneStream* forest = w.main->add<game::SceneStream>(stream);
    forest->scene = "maps/forest.dscene";
    forest->width = 10.0;
    forest->height = 10.0;
    forest->reach = 8.0;
    const engine::Guid forest_guid = w.main->guid(stream);
    const world::Entity player = w.player(0.0, 5.0);
    static_cast<void>(player);

    game::SceneStreamer streamer;
    // Far off: nothing.
    game::SceneStreamer::Changes changes = streamer.update(*w.main, 0.0, 5.0, w.reader());
    CHECK(changes.loaded.empty());
    CHECK(game::SceneStreamer::entities(*w.main) == 0);
    CHECK(w.reads == 0);
    // Within reach (5 tiles off): in, once.
    changes = streamer.update(*w.main, 35.0, 5.0, w.reader());
    CHECK(changes.loaded == std::vector<std::string>{"forest"});
    CHECK(changes.problems.empty());
    // The tree, moved to the stream's place and numbered, as the main scene has one; the
    // house moved, the door under it with it; its player, screen item and stream left out.
    REQUIRE(w.at("tree_2") != nullptr);
    CHECK(w.at("tree_2")->x == 42.0);
    CHECK(w.at("tree_2")->y == 3.0);
    CHECK(w.at("tree") != nullptr); // the main scene's own, untouched
    REQUIRE(w.at("house") != nullptr);
    CHECK(w.at("house")->x == 45.0);
    CHECK(w.at("door") != nullptr);
    CHECK(w.main->find_named("hero").empty());
    CHECK(w.main->find_named("score").empty());
    CHECK(w.main->find_named("deeper").empty());
    CHECK(game::SceneStreamer::entities(*w.main) == 3);
    CHECK(game::SceneStreamer::brought_by(*w.main, forest_guid).size() == 3);
    changes = streamer.update(*w.main, 35.0, 5.0, w.reader());
    CHECK(changes.loaded.empty());
    CHECK(w.reads == 1);
    // Beyond reach but within the margin (11 tiles off): kept.
    changes = streamer.update(*w.main, 29.0, 5.0, w.reader());
    CHECK(changes.unloaded.empty());
    CHECK(w.at("tree_2") != nullptr);
    // Well beyond: taken away, and the main scene's tree stays.
    changes = streamer.update(*w.main, 20.0, 5.0, w.reader());
    CHECK(changes.unloaded == std::vector<std::string>{"forest"});
    CHECK(w.main->find_named("tree_2").empty());
    CHECK(w.main->find_named("house").empty());
    CHECK(w.at("tree") != nullptr);
    CHECK(game::SceneStreamer::entities(*w.main) == 0);

    // Back again, then turned off: away.
    static_cast<void>(streamer.update(*w.main, 45.0, 5.0, w.reader()));
    CHECK(game::SceneStreamer::entities(*w.main) == 3);
    w.main->get<game::SceneStream>(w.main->find(forest_guid))->enabled = false;
    changes = streamer.update(*w.main, 45.0, 5.0, w.reader());
    CHECK(changes.unloaded.size() == 1);
    CHECK(game::SceneStreamer::entities(*w.main) == 0);

    // A saved game holds the streamed entities with where they came from: a new streamer
    // (the game loaded again) takes them away when the player is far.
    w.main->get<game::SceneStream>(w.main->find(forest_guid))->enabled = true;
    static_cast<void>(streamer.update(*w.main, 45.0, 5.0, w.reader()));
    const serialize::Json saved = serialize::scene_to_json(*w.main);
    const std::unique_ptr<world::World> loaded = w.make();
    REQUIRE_FALSE(serialize::scene_from_json(saved, *loaded).entities.empty());
    CHECK(game::SceneStreamer::entities(*loaded) == 3);
    game::SceneStreamer fresh;
    changes = fresh.update(*loaded, 45.0, 5.0, w.reader());
    CHECK(changes.loaded.empty()); // there already: not loaded twice
    CHECK(game::SceneStreamer::entities(*loaded) == 3);
    changes = fresh.update(*loaded, 0.0, 5.0, w.reader());
    CHECK(changes.unloaded.size() == 1);
    CHECK(game::SceneStreamer::entities(*loaded) == 0);
}

TEST_CASE("Large worlds: a scene that cannot be read is said once, and tried after a change") {
    Worlds w;
    const world::Entity stream = w.main->create("cave");
    w.main->add<scene2d::Transform2D>(stream);
    game::SceneStream* cave = w.main->add<game::SceneStream>(stream);
    cave->scene = "maps/cave.dscene";
    game::SceneStreamer streamer;
    game::SceneStreamer::Changes changes = streamer.update(*w.main, 1.0, 1.0, w.reader());
    REQUIRE(changes.problems.size() == 1);
    CHECK(changes.problems[0] == "cave: there is no scene maps/cave.dscene in assets/.");
    CHECK(streamer.update(*w.main, 1.0, 1.0, w.reader()).problems.empty());
    CHECK(w.reads == 1);
    // Pointed at the forest instead: loaded.
    w.main->get<game::SceneStream>(stream)->scene = "maps/forest.dscene";
    changes = streamer.update(*w.main, 1.0, 1.0, w.reader());
    CHECK(changes.loaded == std::vector<std::string>{"cave"});
    // Taking every stream away.
    game::SceneStreamer::clear(*w.main);
    CHECK(game::SceneStreamer::entities(*w.main) == 0);
}
