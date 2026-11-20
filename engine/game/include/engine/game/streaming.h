#pragma once

// Large worlds (plan/02 C13): a world made of scenes laid side by side, each loaded as the
// player comes near and taken away again as they leave, as Godot and Unity stream levels.
// A Streamed Scene in the scene being played names another scene and where its top-left
// corner goes; the runtime keeps the ones near the player loaded (SceneStreamer).
//
// See docs/dev/game.md, *Large worlds*.

#include "engine/foundation/guid.h"
#include "engine/foundation/reflect.h"
#include "engine/serialize/json.h"
#include "engine/world/world.h"

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::game {

/// "Streamed Scene": another scene laid into this one, its point (0, 0) at the position:
/// loaded while the player is within `reach` tiles of its area, taken away again once they
/// are further than that (and a few tiles more, so walking along the edge does not load
/// and unload it again and again).
struct SceneStream {
    std::string scene;    ///< A scene in assets/ ("maps/forest.dscene"), or its GUID.
    double width = 32.0;  ///< Its area, in tiles, right of the position.
    double height = 32.0; ///< And down from it.
    double reach = 16.0;  ///< How near the player comes before it loads, in tiles.
    bool enabled = true;
};

/// On every entity a Streamed Scene brought in: which stream (its GUID). Saved with the
/// game, so a saved game knows what to take away again; hidden from the inspector.
struct StreamedFrom {
    std::string stream;
};

/// Tiles further than `reach` the player must go before a streamed scene is taken away.
inline constexpr double stream_margin = 4.0;

void register_streaming(reflect::Registry& registry);
bool register_streaming_components(world::World& world);

/// How far (x, y) is from the area of the stream placed at (sx, sy): 0 inside it.
[[nodiscard]] double distance_to_area(const SceneStream& stream, double sx, double sy, double x,
                                      double y) noexcept;

/// Loads and takes away the world's Streamed Scenes around the player.
class SceneStreamer {
public:
    /// Reads a scene's JSON by its path or GUID; nothing, with `error`, if it cannot.
    using Reader =
        std::function<std::optional<serialize::Json>(std::string_view scene, std::string& error)>;

    struct Changes {
        std::vector<std::string> loaded;   ///< Scenes loaded this time, by their stream's name.
        std::vector<std::string> unloaded; ///< And taken away.
        std::vector<std::string> problems; ///< Scenes that could not be loaded, and why.
    };

    /// Loads every enabled stream the player at (x, y) is within reach of, and takes away
    /// those they are well beyond (or that are gone or turned off). A scene's entities
    /// with a Position come in, its top-level ones moved by the stream's position; its
    /// player (a Top-down Mover with no Walker), its screen items (no Position) and its
    /// own Streamed Scenes stay out. Names taken already are numbered (forest_2). Changes
    /// made to a streamed scene's entities are lost when it is taken away.
    Changes update(world::World& world, double x, double y, const Reader& read);
    /// Takes every streamed scene away.
    static void clear(world::World& world);
    /// The entities streamed in by the stream with this GUID, read from the world (their
    /// StreamedFrom), so it holds after a saved game is loaded.
    [[nodiscard]] static std::vector<world::Entity> brought_by(const world::World& world,
                                                               Guid stream);
    /// The entities streamed in, all streams together.
    [[nodiscard]] static std::size_t entities(const world::World& world);

private:
    /// Streams whose scene could not be read: not tried again until their scene changes.
    std::map<Guid, std::string> failed_;
};

} // namespace engine::game
