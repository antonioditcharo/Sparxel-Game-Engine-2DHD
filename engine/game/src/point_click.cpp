// Point and Click (behaviours.h): the player's character walks where the pointer clicks,
// and to what it clicks, to interact with it, as in adventure games.

#include "engine/game/behaviours.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

namespace engine::game {

namespace {

/// Points nearer than this are reached.
constexpr double arrive_within = 0.02;

} // namespace

world::Entity interactable_at(const world::World& world, double x, double y) {
    world::Entity found;
    Guid found_guid;
    const reflect::TypeInfo* type = world.registry().find_type<Interactable>();
    if (type == nullptr || !std::isfinite(x) || !std::isfinite(y)) {
        return found;
    }
    world.each(*type, [&](world::Entity entity, reflect::ObjectRef ref) {
        const Interactable* thing = ref.as<Interactable>();
        const scene2d::Transform2D* at = world.get<scene2d::Transform2D>(entity);
        if (thing == nullptr || at == nullptr || !thing->enabled) {
            return;
        }
        const scene2d::Box area = feet_box(at->x, at->y, thing->width, thing->height);
        const bool inside = x >= area.left && x < area.right && y >= area.top && y < area.bottom;
        const Guid guid = world.guid(entity);
        if (inside && (!found.is_valid() || guid < found_guid)) {
            found = entity;
            found_guid = guid;
        }
    });
    return found;
}

bool click_to_walk(world::World& world, double x, double y, std::uint32_t ppu) {
    const reflect::TypeInfo* type = world.registry().find_type<PointAndClick>();
    if (type == nullptr || !std::isfinite(x) || !std::isfinite(y)) {
        return false;
    }
    const world::Entity thing = interactable_at(world, x, y);
    bool walking = false;
    world.each(*type, [&](world::Entity entity, reflect::ObjectRef ref) {
        PointAndClick* click = ref.as<PointAndClick>();
        const TopDownMover* mover = world.get<TopDownMover>(entity);
        const scene2d::Transform2D* at = world.get<scene2d::Transform2D>(entity);
        if (click == nullptr || mover == nullptr || at == nullptr || !click->enabled ||
            !mover->enabled || entity == thing) {
            return;
        }
        click->path.clear();
        click->going_to = Guid{};
        click->near_enough = click->reach;
        double to_x = x;
        double to_y = y;
        if (thing.is_valid()) {
            // Towards what was clicked: its position, until near enough.
            const scene2d::Transform2D* there = world.get<scene2d::Transform2D>(thing);
            click->going_to = world.guid(thing);
            to_x = there->x;
            to_y = there->y;
        }
        if (auto way = find_path(world, *mover, entity, at->x, at->y, to_x, to_y, ppu, thing)) {
            click->path = std::move(*way);
            walking = true;
        } else if (thing.is_valid()) {
            // Set in a wall, or on a table: the nearest tile beside it that can be reached,
            // below it first, as a door in the top wall is walked up to.
            constexpr std::array<std::array<double, 2>, 8> around{{{0.0, 1.0},
                                                                   {-1.0, 0.0},
                                                                   {1.0, 0.0},
                                                                   {0.0, -1.0},
                                                                   {-1.0, 1.0},
                                                                   {1.0, 1.0},
                                                                   {-1.0, -1.0},
                                                                   {1.0, -1.0}}};
            std::optional<std::vector<std::array<double, 2>>> best;
            double shortest = std::numeric_limits<double>::infinity();
            for (const auto& [ox, oy] : around) {
                if (auto beside = find_path(world, *mover, entity, at->x, at->y, to_x + ox,
                                            to_y + oy, ppu, thing)) {
                    double length = 0.0;
                    std::array<double, 2> from{at->x, at->y};
                    for (const std::array<double, 2>& point : *beside) {
                        length += std::hypot(point[0] - from[0], point[1] - from[1]);
                        from = point;
                    }
                    if (length < shortest - 1e-9) {
                        shortest = length;
                        best = std::move(beside);
                    }
                }
            }
            if (best && !best->empty()) {
                // Arriving beside it is near enough, however far that tile is.
                const std::array<double, 2> end = best->back();
                click->near_enough =
                    std::max(click->reach, std::hypot(end[0] - to_x, end[1] - to_y) + 1e-6);
                click->path = std::move(*best);
            }
            walking = true; // perhaps near enough already
        }
    });
    return walking;
}

std::vector<std::pair<world::Entity, world::Entity>>
steer_point_and_click(world::World& world, double seconds, const input::InputState& input) {
    std::vector<std::pair<world::Entity, world::Entity>> arrived;
    const reflect::TypeInfo* type = world.registry().find_type<PointAndClick>();
    if (type == nullptr || !std::isfinite(seconds) || seconds < 0.0) {
        return arrived;
    }
    const bool keys = input.held("move_left") || input.held("move_right") ||
                      input.held("move_up") || input.held("move_down");
    world.each(*type, [&](world::Entity entity, reflect::ObjectRef ref) {
        PointAndClick* click = ref.as<PointAndClick>();
        TopDownMover* mover = world.get<TopDownMover>(entity);
        const scene2d::Transform2D* at = world.get<scene2d::Transform2D>(entity);
        if (click == nullptr) {
            return;
        }
        click->step_dx = 0.0;
        click->step_dy = 0.0;
        click->step_scale = 1.0;
        if (mover == nullptr || at == nullptr || !click->enabled || !mover->enabled || keys) {
            // The keys take over: the walk ends.
            click->path.clear();
            click->going_to = Guid{};
            return;
        }
        // Near enough to what it walks to: it turns to it and interacts.
        if (click->going_to.is_valid()) {
            const world::Entity thing = world.find(click->going_to);
            const scene2d::Transform2D* there =
                thing.is_valid() ? world.get<scene2d::Transform2D>(thing) : nullptr;
            const double wanted = std::max(click->reach, click->near_enough);
            const double reach = std::isfinite(wanted) ? std::max(wanted, 0.0) : 0.0;
            if (there == nullptr) {
                click->going_to = Guid{};
                click->path.clear();
            } else if (std::hypot(there->x - at->x, there->y - at->y) <= reach + 1e-9) {
                mover->facing = facing_for(mover->facing, there->x - at->x, there->y - at->y);
                arrived.emplace_back(entity, thing);
                click->going_to = Guid{};
                click->path.clear();
                return;
            }
        }
        while (!click->path.empty() &&
               std::hypot(click->path.front()[0] - at->x, click->path.front()[1] - at->y) <=
                   arrive_within) {
            click->path.erase(click->path.begin());
        }
        if (click->path.empty()) {
            click->going_to = Guid{}; // could not get near enough
            return;
        }
        const double dx = click->path.front()[0] - at->x;
        const double dy = click->path.front()[1] - at->y;
        const double far = std::hypot(dx, dy);
        const double full = std::max(mover->speed, 0.0) * seconds;
        click->step_dx = dx;
        click->step_dy = dy;
        click->step_scale = full > 0.0 ? std::min(1.0, far / full) : 0.0;
    });
    return arrived;
}

} // namespace engine::game
