#pragma once

// Tactics (plan/02 F3, as SRPG Studio and Fire Emblem play): turn-based battles on the map's
// tiles. Each side's units take turns: the player picks a unit, moves it within its reach
// (round walls and the other side's units), and attacks an enemy in range or waits; then
// every enemy moves towards the nearest of the player's units and attacks. The side that
// loses all its units loses. Rules start the battle and hear its turns, its losses and how
// it ends; the keys and the mouse both play it.
//
// See docs/dev/game.md, *Tactics*.

#include "engine/foundation/guid.h"
#include "engine/foundation/reflect.h"
#include "engine/world/world.h"

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace engine::game {

/// Which side a unit fights for.
enum class Team : std::uint8_t { Player, Enemy };

/// "Tactics Unit": a unit of a tactics battle, standing on a tile (its position's tile).
struct TacticsUnit {
    Team team = Team::Player;
    std::int64_t move = 4;  ///< Tiles it moves in a turn.
    std::int64_t range = 1; ///< Tiles away it attacks (1: beside it).
    std::int64_t hp = 10;
    std::int64_t max_hp = 10;
    std::int64_t attack = 4;
    std::int64_t defense = 1;
    bool done = false; ///< It has acted this turn (hidden).

    [[nodiscard]] bool alive() const noexcept { return hp > 0; }
};

/// A tile of the map: (0, 0) is the one whose top-left is the map point (0, 0).
struct Tile {
    std::int64_t x = 0;
    std::int64_t y = 0;
    friend auto operator<=>(const Tile&, const Tile&) = default;
};

/// The tile a position stands on (feet at the bottom of the tile).
[[nodiscard]] Tile tile_of(double x, double y) noexcept;
/// Where a unit on `tile` stands: the middle of its bottom edge.
[[nodiscard]] std::pair<double, double> stand_on(Tile tile) noexcept;
/// Tiles apart, along the grid.
[[nodiscard]] std::int64_t tiles_apart(Tile a, Tile b) noexcept;
/// What an attack of `attacker` takes from `defender`: its attack less the defence, at
/// least 1.
[[nodiscard]] std::int64_t tactics_damage(const TacticsUnit& attacker,
                                          const TacticsUnit& defender) noexcept;

/// The tiles `unit` can move to this turn: within its `move` steps, four ways, never into a
/// solid tile or a Solid Body, through its own side's units but not the other's, ending
/// on a tile no other living unit stands on; its own tile first, then by distance.
[[nodiscard]] std::vector<Tile> reachable_tiles(const world::World& world, world::Entity unit,
                                                std::uint32_t ppu);
/// The other side's living units within `unit`'s range of `from`, nearest first (then by
/// GUID).
[[nodiscard]] std::vector<world::Entity> targets_from(const world::World& world, world::Entity unit,
                                                      Tile from);

/// The battle, bound to logic as "tactics".
class TacticsObject {
public:
    enum class Phase : std::uint8_t { Off, Player, Enemy, Won, Lost };
    enum class Step : std::uint8_t { Choose, Move, Target };

    /// Starts a battle with the units in the world: the player's turn first, the cursor on
    /// the player's first unit. A request: the runtime does it in its next step.
    void start() { start_wanted_ = true; }
    /// Ends the player's turn early: every unit not yet done waits.
    void end_turn() { end_wanted_ = true; }
    /// Whether it is the player's turn of a battle under way.
    [[nodiscard]] bool is_player_turn() const noexcept { return phase_ == Phase::Player; }
    /// Whether a battle is under way.
    [[nodiscard]] bool is_on() const noexcept {
        return phase_ == Phase::Player || phase_ == Phase::Enemy;
    }
    [[nodiscard]] std::int64_t turn() const noexcept { return turn_; }

    /// What happened, for Rules: an event of "tactics" and its value.
    struct Event {
        std::string name;  ///< "turn_started", "unit_defeated", "won" or "lost".
        std::string value; ///< "player"/"enemy", or the defeated unit's name.
    };

    /// One press of an action during the battle: the move actions move the cursor a tile,
    /// Interact chooses what is under it, Cancel goes back (or makes a unit wait).
    void press(world::World& world, std::string_view action, std::uint32_t ppu);
    /// A click on `tile`: the cursor goes there and chooses, as Interact would.
    void click(world::World& world, Tile tile, std::uint32_t ppu);
    /// One step of `seconds`: requests taken, the enemies' turn played a unit at a time,
    /// the end found. Returns what happened, in order.
    std::vector<Event> update(world::World& world, double seconds, std::uint32_t ppu);

    // For drawing.
    [[nodiscard]] Phase phase() const noexcept { return phase_; }
    [[nodiscard]] Step step() const noexcept { return step_; }
    [[nodiscard]] Tile cursor() const noexcept { return cursor_; }
    [[nodiscard]] Guid selected() const noexcept { return selected_; }
    /// Where the chosen unit may move, while it is moved.
    [[nodiscard]] const std::vector<Tile>& moves() const noexcept { return moves_; }
    /// Whom it may attack, while it attacks.
    [[nodiscard]] const std::vector<Tile>& targets() const noexcept { return targets_; }

private:
    void choose(world::World& world, std::uint32_t ppu);
    void finish_unit(world::World& world);
    void attack(world::World& world, world::Entity attacker, world::Entity defender);
    void begin_turn(world::World& world, Team team);
    void play_enemy(world::World& world, world::Entity enemy, std::uint32_t ppu);

    Phase phase_ = Phase::Off;
    Step step_ = Step::Choose;
    Tile cursor_;
    Guid selected_;
    std::vector<Tile> moves_;
    std::vector<Tile> targets_;
    std::int64_t turn_ = 0;
    bool start_wanted_ = false;
    bool end_wanted_ = false;
    double enemy_wait_ = 0.0;
    std::vector<Event> events_;
};

/// Registers TacticsUnit ("tactics_unit"), its Team, and TacticsObject ("tactics": Start a
/// tactics battle, End the turn, It is the player's turn; and its events).
void register_tactics(reflect::Registry& registry);
[[nodiscard]] bool register_tactics_components(world::World& world);

} // namespace engine::game
