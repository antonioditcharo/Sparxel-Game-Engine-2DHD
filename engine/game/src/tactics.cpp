#include "engine/game/tactics.h"

#include "engine/game/behaviours.h"
#include "engine/scene2d/scene2d.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <limits>
#include <map>

namespace engine::game {

namespace {

/// A living unit and where it stands.
struct Placed {
    world::Entity entity;
    TacticsUnit* unit = nullptr;
    Tile tile;
    Guid guid;
};

std::vector<Placed> units_of(const world::World& world) {
    std::vector<Placed> found;
    const reflect::TypeInfo* type = world.registry().find_type<TacticsUnit>();
    if (type == nullptr) {
        return found;
    }
    world.each(*type, [&](world::Entity entity, reflect::ObjectRef ref) {
        auto* unit = const_cast<TacticsUnit*>(ref.as<TacticsUnit>()); // NOLINT
        const scene2d::Transform2D* at = world.get<scene2d::Transform2D>(entity);
        if (unit != nullptr && at != nullptr && unit->alive()) {
            found.push_back(Placed{entity, unit, tile_of(at->x, at->y), world.guid(entity)});
        }
    });
    std::sort(found.begin(), found.end(),
              [](const Placed& a, const Placed& b) { return a.guid < b.guid; });
    return found;
}

/// Whether a unit can stand on `tile`: no solid tile, no Solid Body there.
bool open_tile(const world::World& world, Tile tile, std::uint32_t ppu) {
    const auto x = static_cast<double>(tile.x);
    const auto y = static_cast<double>(tile.y);
    const scene2d::Box middle{x + 0.25, y + 0.25, x + 0.75, y + 0.75};
    if (scene2d::solid_at(world, middle, ppu)) {
        return false;
    }
    bool free = true;
    if (const reflect::TypeInfo* type = world.registry().find_type<Body>()) {
        world.each(*type, [&](world::Entity entity, reflect::ObjectRef ref) {
            const Body* body = ref.as<Body>();
            const scene2d::Transform2D* at = world.get<scene2d::Transform2D>(entity);
            if (body != nullptr && at != nullptr && body->enabled &&
                scene2d::overlaps(middle, feet_box(at->x, at->y, body->width, body->height))) {
                free = false;
            }
        });
    }
    return free;
}

std::string team_name(Team team) {
    return team == Team::Player ? "player" : "enemy";
}

} // namespace

Tile tile_of(double x, double y) noexcept {
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return Tile{};
    }
    return Tile{static_cast<std::int64_t>(std::floor(x)),
                static_cast<std::int64_t>(std::floor(y - 1e-6))};
}

std::pair<double, double> stand_on(Tile tile) noexcept {
    return {static_cast<double>(tile.x) + 0.5, static_cast<double>(tile.y) + 1.0};
}

std::int64_t tiles_apart(Tile a, Tile b) noexcept {
    return std::abs(a.x - b.x) + std::abs(a.y - b.y);
}

std::int64_t tactics_damage(const TacticsUnit& attacker, const TacticsUnit& defender) noexcept {
    return std::max<std::int64_t>(1, attacker.attack - defender.defense);
}

std::vector<Tile> reachable_tiles(const world::World& world, world::Entity unit,
                                  std::uint32_t ppu) {
    std::vector<Tile> out;
    const TacticsUnit* self = world.get<TacticsUnit>(unit);
    const scene2d::Transform2D* at = world.get<scene2d::Transform2D>(unit);
    if (self == nullptr || at == nullptr || !self->alive()) {
        return out;
    }
    const Tile start = tile_of(at->x, at->y);
    std::map<Tile, Team> standing;
    for (const Placed& other : units_of(world)) {
        if (other.entity != unit) {
            standing[other.tile] = other.unit->team;
        }
    }
    const std::int64_t most = std::clamp<std::int64_t>(self->move, 0, 32);
    std::map<Tile, std::int64_t> steps{{start, 0}};
    std::deque<Tile> todo{start};
    while (!todo.empty()) {
        const Tile here = todo.front();
        todo.pop_front();
        const std::int64_t taken = steps[here];
        if (taken >= most) {
            continue;
        }
        constexpr std::array<std::array<std::int64_t, 2>, 4> ways{
            {{0, 1}, {1, 0}, {0, -1}, {-1, 0}}};
        for (const auto& [dx, dy] : ways) {
            const Tile next{here.x + dx, here.y + dy};
            if (steps.contains(next) || !open_tile(world, next, ppu)) {
                continue;
            }
            const auto found = standing.find(next);
            if (found != standing.end() && found->second != self->team) {
                continue; // the other side blocks the way
            }
            steps[next] = taken + 1;
            todo.push_back(next);
        }
    }
    for (const auto& [tile, taken] : steps) {
        if (!standing.contains(tile)) {
            out.push_back(tile);
        }
    }
    std::stable_sort(out.begin(), out.end(),
                     [&](const Tile& a, const Tile& b) { return steps[a] < steps[b]; });
    return out;
}

std::vector<world::Entity> targets_from(const world::World& world, world::Entity unit, Tile from) {
    std::vector<world::Entity> out;
    const TacticsUnit* self = world.get<TacticsUnit>(unit);
    if (self == nullptr) {
        return out;
    }
    std::vector<Placed> others;
    for (const Placed& other : units_of(world)) {
        if (other.unit->team != self->team && tiles_apart(from, other.tile) <= self->range) {
            others.push_back(other);
        }
    }
    std::stable_sort(others.begin(), others.end(), [&](const Placed& a, const Placed& b) {
        return tiles_apart(from, a.tile) < tiles_apart(from, b.tile);
    });
    for (const Placed& other : others) {
        out.push_back(other.entity);
    }
    return out;
}

void TacticsObject::begin_turn(world::World& world, Team team) {
    phase_ = team == Team::Player ? Phase::Player : Phase::Enemy;
    step_ = Step::Choose;
    selected_ = Guid{};
    moves_.clear();
    targets_.clear();
    if (team == Team::Player) {
        ++turn_;
    }
    bool cursor_set = false;
    for (const Placed& placed : units_of(world)) {
        if (placed.unit->team == team) {
            placed.unit->done = false;
            if (team == Team::Player && !cursor_set) {
                cursor_ = placed.tile;
                cursor_set = true;
            }
        }
    }
    enemy_wait_ = 0.5;
    events_.push_back(Event{"turn_started", team_name(team)});
}

void TacticsObject::attack(world::World& world, world::Entity attacker, world::Entity defender) {
    TacticsUnit* from = world.get<TacticsUnit>(attacker);
    TacticsUnit* to = world.get<TacticsUnit>(defender);
    if (from == nullptr || to == nullptr || !to->alive()) {
        return;
    }
    to->hp = std::max<std::int64_t>(0, to->hp - tactics_damage(*from, *to));
    if (!to->alive()) {
        if (scene2d::Sprite* sprite = world.get<scene2d::Sprite>(defender)) {
            sprite->visible = false;
        }
        events_.push_back(Event{"unit_defeated", std::string(world.name(defender))});
    }
}

void TacticsObject::finish_unit(world::World& world) {
    if (const world::Entity unit = world.find(selected_); unit.is_valid()) {
        if (TacticsUnit* self = world.get<TacticsUnit>(unit)) {
            self->done = true;
        }
    }
    selected_ = Guid{};
    moves_.clear();
    targets_.clear();
    step_ = Step::Choose;
    const std::vector<Placed> all = units_of(world);
    const bool waiting = std::any_of(all.begin(), all.end(), [](const Placed& p) {
        return p.unit->team == Team::Player && !p.unit->done;
    });
    const bool enemies = std::any_of(all.begin(), all.end(),
                                     [](const Placed& p) { return p.unit->team == Team::Enemy; });
    if (!waiting && enemies) {
        begin_turn(world, Team::Enemy); // (with none left, the battle is won instead)
    }
}

void TacticsObject::choose(world::World& world, std::uint32_t ppu) {
    const std::vector<Placed> all = units_of(world);
    const auto at_cursor =
        std::find_if(all.begin(), all.end(), [&](const Placed& p) { return p.tile == cursor_; });
    const world::Entity chosen = world.find(selected_);
    if (step_ == Step::Target) {
        if (at_cursor != all.end() &&
            std::find(targets_.begin(), targets_.end(), cursor_) != targets_.end()) {
            attack(world, chosen, at_cursor->entity);
            finish_unit(world);
        }
        return;
    }
    if (step_ == Step::Move && std::find(moves_.begin(), moves_.end(), cursor_) != moves_.end()) {
        if (scene2d::Transform2D* to = world.get<scene2d::Transform2D>(chosen)) {
            const auto [x, y] = stand_on(cursor_);
            to->x = x;
            to->y = y;
        }
        moves_.clear();
        targets_.clear();
        for (const world::Entity target : targets_from(world, chosen, cursor_)) {
            const scene2d::Transform2D* there = world.get<scene2d::Transform2D>(target);
            targets_.push_back(tile_of(there->x, there->y));
        }
        if (targets_.empty()) {
            finish_unit(world);
        } else {
            step_ = Step::Target;
        }
        return;
    }
    // Choosing (or choosing again): one of the player's units that has not acted.
    if (at_cursor != all.end() && at_cursor->unit->team == Team::Player && !at_cursor->unit->done) {
        selected_ = at_cursor->guid;
        moves_ = reachable_tiles(world, at_cursor->entity, ppu);
        targets_.clear();
        step_ = Step::Move;
    }
}

void TacticsObject::press(world::World& world, std::string_view action, std::uint32_t ppu) {
    if (phase_ != Phase::Player) {
        return;
    }
    if (action == "move_up") {
        --cursor_.y;
    } else if (action == "move_down") {
        ++cursor_.y;
    } else if (action == "move_left") {
        --cursor_.x;
    } else if (action == "move_right") {
        ++cursor_.x;
    } else if (action == "interact") {
        choose(world, ppu);
    } else if (action == "cancel") {
        if (step_ == Step::Target) {
            finish_unit(world); // it waits where it moved
        } else if (step_ == Step::Move) {
            selected_ = Guid{};
            moves_.clear();
            step_ = Step::Choose;
        }
    }
}

void TacticsObject::click(world::World& world, Tile tile, std::uint32_t ppu) {
    if (phase_ != Phase::Player) {
        return;
    }
    cursor_ = tile;
    choose(world, ppu);
}

void TacticsObject::play_enemy(world::World& world, world::Entity enemy, std::uint32_t ppu) {
    const scene2d::Transform2D* at = world.get<scene2d::Transform2D>(enemy);
    if (at == nullptr) {
        return;
    }
    std::vector<world::Entity> targets = targets_from(world, enemy, tile_of(at->x, at->y));
    if (targets.empty()) {
        // Towards the nearest of the player's units: the reachable tile nearest to one.
        std::vector<Tile> goals;
        for (const Placed& p : units_of(world)) {
            if (p.unit->team == Team::Player) {
                goals.push_back(p.tile);
            }
        }
        std::optional<Tile> best;
        std::int64_t nearest = std::numeric_limits<std::int64_t>::max();
        for (const Tile& tile : reachable_tiles(world, enemy, ppu)) {
            for (const Tile& goal : goals) {
                if (tiles_apart(tile, goal) < nearest) {
                    nearest = tiles_apart(tile, goal);
                    best = tile;
                }
            }
        }
        if (best) {
            if (auto* to = world.get<scene2d::Transform2D>(enemy)) {
                const auto [x, y] = stand_on(*best);
                to->x = x;
                to->y = y;
            }
            targets = targets_from(world, enemy, *best);
        }
    }
    if (!targets.empty()) {
        attack(world, enemy, targets.front());
    }
}

std::vector<TacticsObject::Event> TacticsObject::update(world::World& world, double seconds,
                                                        std::uint32_t ppu) {
    if (start_wanted_) {
        start_wanted_ = false;
        turn_ = 0;
        begin_turn(world, Team::Player);
    }
    if (end_wanted_) {
        end_wanted_ = false;
        if (phase_ == Phase::Player) {
            for (const Placed& p : units_of(world)) {
                if (p.unit->team == Team::Player) {
                    p.unit->done = true;
                }
            }
            selected_ = Guid{};
            finish_unit(world);
        }
    }
    if (phase_ == Phase::Enemy && std::isfinite(seconds)) {
        enemy_wait_ -= seconds;
        if (enemy_wait_ <= 0.0) {
            const std::vector<Placed> all = units_of(world);
            const auto next = std::find_if(all.begin(), all.end(), [](const Placed& p) {
                return p.unit->team == Team::Enemy && !p.unit->done;
            });
            if (next == all.end()) {
                begin_turn(world, Team::Player);
            } else {
                next->unit->done = true;
                play_enemy(world, next->entity, ppu);
                enemy_wait_ = 0.4;
            }
        }
    }
    // The end: a side with no units left.
    if (is_on()) {
        const std::vector<Placed> all = units_of(world);
        const auto count = [&](Team team) {
            return std::count_if(all.begin(), all.end(),
                                 [team](const Placed& p) { return p.unit->team == team; });
        };
        if (count(Team::Player) == 0) {
            phase_ = Phase::Lost;
            events_.push_back(Event{"lost", ""});
        } else if (count(Team::Enemy) == 0) {
            phase_ = Phase::Won;
            events_.push_back(Event{"won", ""});
        }
    }
    return std::exchange(events_, {});
}

void register_tactics(reflect::Registry& registry) {
    registry.add_choice_list<Team>("team")
        .display_name("Side")
        .entry(Team::Player, "player")
        .entry(Team::Enemy, "enemy");
    auto unit = registry.add_type<TacticsUnit>("tactics_unit")
                    .display_name("Tactics Unit")
                    .category("Combat")
                    .tooltip("A unit of a turn-based tactics battle, on the tile it stands on.");
    unit.property<&TacticsUnit::team>("team").display_name("Side").tooltip(
        "The player's, or the enemy's.");
    unit.property<&TacticsUnit::move>("move").range(0, 32).tooltip("Tiles it moves in a turn.");
    unit.property<&TacticsUnit::range>("range").range(1, 32).tooltip(
        "Tiles away it attacks: 1 is beside it.");
    unit.property<&TacticsUnit::hp>("hp").display_name("HP").range(0, 100000).tooltip(
        "What it has left; at 0 it is defeated.");
    unit.property<&TacticsUnit::max_hp>("max_hp").display_name("Max HP").range(1, 100000);
    unit.property<&TacticsUnit::attack>("attack").range(0, 100000).tooltip(
        "Taken from the defender's HP, less its defence (at least 1).");
    unit.property<&TacticsUnit::defense>("defense").display_name("Defence").range(0, 100000);
    unit.property<&TacticsUnit::done>("done").flags(reflect::property_flag::hidden);

    auto tactics = registry.add_type<TacticsObject>("tactics").display_name("Tactics").tooltip(
        "A turn-based battle on the map's tiles.");
    tactics.action<&TacticsObject::start>("start_battle")
        .text("Start a tactics battle")
        .tooltip("The player's turn first: pick a unit, move it, attack or wait.");
    tactics.action<&TacticsObject::end_turn>("end_turn")
        .text("End the player's turn")
        .tooltip("Every unit that has not acted waits.");
    tactics.condition<&TacticsObject::is_player_turn>("is_player_turn")
        .text("It is the player's turn")
        .tooltip("A tactics battle is under way, and the player plays.");
    tactics.getter<&TacticsObject::turn>("turn")
        .text("The turn number")
        .tooltip("1 for the first of the player's turns.");
    tactics.event<std::string>("turn_started", {"team"})
        .text("When a turn starts ({team})")
        .tooltip("The player's or the enemy's.");
    tactics.event<std::string>("unit_defeated", {"who"})
        .text("When a unit is defeated ({who})")
        .tooltip("Its HP reached 0.");
    tactics.event<>("won").text("When the battle is won").tooltip("No enemy is left.");
    tactics.event<>("lost")
        .text("When the battle is lost")
        .tooltip("None of the player's is left.");
}

bool register_tactics_components(world::World& world) {
    return world.register_component<TacticsUnit>();
}

} // namespace engine::game
