#include "engine/game/streaming.h"

#include "engine/game/behaviours.h"
#include "engine/scene2d/scene2d.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <map>
#include <utility>

namespace engine::game {

void register_streaming(reflect::Registry& registry) {
    auto stream = registry.add_type<SceneStream>("scene_stream")
                      .display_name("Streamed Scene")
                      .category("World")
                      .tooltip("Another scene laid in here, its top-left corner at this "
                               "position: loaded as the player comes near, taken away as they "
                               "leave, for worlds too large for one scene.");
    stream.property<&SceneStream::scene>("scene").tooltip("The scene to lay in here.");
    stream.property<&SceneStream::width>("width")
        .range(1.0, 4096.0)
        .step(1.0)
        .unit(reflect::Unit::Tiles)
        .tooltip("How wide its area is, right of the position.");
    stream.property<&SceneStream::height>("height")
        .range(1.0, 4096.0)
        .step(1.0)
        .unit(reflect::Unit::Tiles)
        .tooltip("How tall its area is, down from the position.");
    stream.property<&SceneStream::reach>("reach")
        .range(0.0, 256.0)
        .step(1.0)
        .unit(reflect::Unit::Tiles)
        .tooltip("How near the player comes before it loads.");
    stream.property<&SceneStream::enabled>("enabled").tooltip(
        "Turn off to take it away and keep it from loading.");
    auto from = registry.add_type<StreamedFrom>("streamed_from")
                    .display_name("Streamed from")
                    .category("World")
                    .tooltip("Brought in by a Streamed Scene.");
    from.property<&StreamedFrom::stream>("stream").flags(reflect::property_flag::hidden);
}

bool register_streaming_components(world::World& world) {
    return world.register_component<SceneStream>() && world.register_component<StreamedFrom>();
}

double distance_to_area(const SceneStream& stream, double sx, double sy, double x,
                        double y) noexcept {
    const double width = std::isfinite(stream.width) ? std::max(stream.width, 0.0) : 0.0;
    const double height = std::isfinite(stream.height) ? std::max(stream.height, 0.0) : 0.0;
    const double dx = std::max({sx - x, 0.0, x - (sx + width)});
    const double dy = std::max({sy - y, 0.0, y - (sy + height)});
    const double d = std::hypot(dx, dy);
    return std::isfinite(d) ? d : std::numeric_limits<double>::infinity();
}

namespace {

/// Every stream's entities in the world, by the stream's GUID text.
std::map<std::string, std::vector<world::Entity>> streamed(const world::World& world) {
    std::map<std::string, std::vector<world::Entity>> found;
    if (const reflect::TypeInfo* type = world.registry().find_type<StreamedFrom>()) {
        world.each(*type, [&](world::Entity e, reflect::ObjectRef ref) {
            if (const auto* from = ref.as<StreamedFrom>()) {
                found[from->stream].push_back(e);
            }
        });
    }
    return found;
}

/// Destroys these entities (and whatever is under them).
void take_away(world::World& world, const std::vector<world::Entity>& entities) {
    for (const world::Entity e : entities) {
        if (world.is_alive(e)) {
            static_cast<void>(world.destroy(e));
        }
    }
}

} // namespace

SceneStreamer::Changes SceneStreamer::update(world::World& world, double x, double y,
                                             const Reader& read) {
    Changes changes;
    const reflect::TypeInfo* type = world.registry().find_type<SceneStream>();
    if (type == nullptr) {
        return changes;
    }
    // Where every stream is, and whether the player is near it.
    struct Seen {
        Guid guid;
        std::string name;
        SceneStream stream;
        double x;
        double y;
    };
    std::vector<Seen> streams;
    world.each(*type, [&](world::Entity e, reflect::ObjectRef ref) {
        const SceneStream* stream = ref.as<SceneStream>();
        const auto* at = world.get<scene2d::Transform2D>(e);
        if (stream != nullptr && at != nullptr) {
            streams.push_back(
                Seen{world.guid(e), std::string(world.name(e)), *stream, at->x, at->y});
        }
    });
    const bool player_known = std::isfinite(x) && std::isfinite(y);
    // Away first: streams gone, turned off, or left well behind.
    for (const auto& [stream, entities] : streamed(world)) {
        const auto seen = std::find_if(streams.begin(), streams.end(),
                                       [&](const Seen& s) { return s.guid.to_string() == stream; });
        const bool keep =
            seen != streams.end() && seen->stream.enabled &&
            (!player_known || distance_to_area(seen->stream, seen->x, seen->y, x, y) <=
                                  std::max(seen->stream.reach, 0.0) + stream_margin);
        if (!keep) {
            take_away(world, entities);
            changes.unloaded.push_back(seen != streams.end() ? seen->name : stream);
        }
    }
    // A stream whose scene was changed is tried again.
    for (auto it = failed_.begin(); it != failed_.end();) {
        const auto seen = std::find_if(streams.begin(), streams.end(),
                                       [&](const Seen& s) { return s.guid == it->first; });
        it = seen == streams.end() || seen->stream.scene != it->second ? failed_.erase(it)
                                                                       : std::next(it);
    }
    if (!player_known) {
        return changes;
    }
    // Then in: streams the player has come within reach of.
    const std::map<std::string, std::vector<world::Entity>> present = streamed(world);
    for (const Seen& s : streams) {
        if (!s.stream.enabled || present.contains(s.guid.to_string()) || failed_.contains(s.guid) ||
            s.stream.scene.empty() ||
            distance_to_area(s.stream, s.x, s.y, x, y) > std::max(s.stream.reach, 0.0)) {
            continue;
        }
        std::string error;
        const std::optional<serialize::Json> json =
            read ? read(s.stream.scene, error) : std::nullopt;
        if (!json) {
            changes.problems.push_back(
                s.name + ": " + (error.empty() ? s.stream.scene + " cannot be read." : error));
            failed_[s.guid] = s.stream.scene; // not tried again every step
            continue;
        }
        const serialize::SceneLoad scene =
            serialize::scene_from_json(serialize::with_new_guids(*json), world);
        // What stays out: its player, its screen items and its own streams.
        std::vector<world::Entity> kept;
        for (const world::Entity e : scene.entities) {
            if (!world.is_alive(e)) {
                continue;
            }
            const Walker* walker = world.get<Walker>(e);
            const bool player =
                world.get<TopDownMover>(e) != nullptr && (walker == nullptr || !walker->enabled);
            if (player || world.get<scene2d::Transform2D>(e) == nullptr ||
                world.get<SceneStream>(e) != nullptr) {
                static_cast<void>(world.destroy(e));
                continue;
            }
            kept.push_back(e);
        }
        for (const world::Entity e : kept) {
            if (!world.is_alive(e)) {
                continue;
            }
            // Top-level entities move by the stream's position; children follow them.
            if (!world.parent(e).is_valid()) {
                if (auto* at = world.get<scene2d::Transform2D>(e)) {
                    at->x += s.x;
                    at->y += s.y;
                }
            }
            // A name of its own where it is taken.
            const std::string base(world.name(e));
            if (!base.empty() && world.find_named(base).size() > 1) {
                std::string name;
                for (int n = 2;; ++n) {
                    name = base + "_" + std::to_string(n);
                    if (world.find_named(name).empty()) {
                        break;
                    }
                }
                world.set_name(e, name);
            }
            if (auto* from = world.add<StreamedFrom>(e)) {
                from->stream = s.guid.to_string();
            }
        }
        changes.loaded.push_back(s.name);
    }
    return changes;
}

void SceneStreamer::clear(world::World& world) {
    for (const auto& [stream, entities] : streamed(world)) {
        take_away(world, entities);
    }
}

std::vector<world::Entity> SceneStreamer::brought_by(const world::World& world, Guid stream) {
    std::map<std::string, std::vector<world::Entity>> all = streamed(world);
    const auto found = all.find(stream.to_string());
    return found != all.end() ? found->second : std::vector<world::Entity>{};
}

std::size_t SceneStreamer::entities(const world::World& world) {
    std::size_t count = 0;
    for (const auto& [stream, entities] : streamed(world)) {
        count += entities.size();
    }
    return count;
}

} // namespace engine::game
