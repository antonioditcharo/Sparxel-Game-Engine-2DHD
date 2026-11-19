#pragma once

// Making a new project: a folder with a project file and a small starter game to change,
// so the first thing a creator sees is something that plays.
//
// See docs/dev/editor.md.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace editor {

/// What a new project starts as: the same small game, drawn flat or as an HD-2D diorama
/// (the walls standing up, lit by the sun, with shadows and the tilt-shift look); a
/// visual novel, a story told in pictures and words; a platformer, seen from the side; an
/// RPG; or an action game, fought in the world with a sword.
enum class ProjectTemplate : std::uint8_t {
    TopDown2D,
    Diorama,
    VisualNovel,
    Platformer,
    Rpg,
    Action,
    Adventure,
    Tactics,
};

/// Makes a new project named `name` in `folder`, which must not exist yet or be empty, and
/// returns true; or returns false, with `error` in plain language, and overwrites nothing.
/// Every asset gets its ".meta".
///
/// The two games (TopDown2D and Diorama): the project file; a tileset (grass, path,
/// stone) and a hero, drawn here, in assets/art; and a start scene,
/// assets/maps/start.dscene, with a ground of grass crossed by a path, stone walls around
/// it that stop the hero, and the hero in the middle, moved with the arrow keys.
///
/// Platformer: the same tileset and hero; a start scene seen from the side, under a sky:
/// grass over stone along the bottom, stone walls at both ends, three stone ledges rising
/// to the right, each 2 tiles above the last, all solid; and the hero on the grass,
/// walking left and right with the arrow keys and jumping 3 tiles with Space or Up
/// (Jumping, game.md).
///
/// VisualNovel: a backdrop filling the screen (a sky over hills and a meadow) and a
/// character, Mira, in assets/art; the start scene with both; and assets/logic/story.drules,
/// in which Mira greets the player when the game starts, asks a question with two answers,
/// and replies to the one picked.
///
/// Action: a meadow walled with stone, with rocks to go round; a hero with a sword
/// (assets/art/fighter.png, its sheet's "slash" a ring of hit boxes all round), Health 5,
/// hurt by enemies; three slimes that wander and chase the hero on sight, Health 2, hurt
/// by the player; a health bar; and assets/logic/meadow.drules: Attack (C) slashes, a
/// slime beaten leaves gel, all three beaten says the meadow is safe, and the hero beaten
/// wakes where it started (game.md, Health and combat).
///
/// Adventure: a point-and-click room (Point and Click, game.md): a locked door in the top
/// wall, an old key on the floor and a painting; the hero walks where the mouse clicks and
/// uses what it clicks; the name of what the mouse is over shows at the top; and
/// assets/logic/room.drules, in which the key is picked up, the painting looked at, and
/// the door opens only with the key.
///
/// Tactics: a turn-based battle on a field with rocks (Tactics, game.md): a knight, a
/// guard and an archer (who shoots two tiles away) against three slimes; the battle starts
/// with the game, and assets/logic/battle.drules says who won.
bool create_project(const std::filesystem::path& folder, std::string_view name, std::string& error,
                    ProjectTemplate kind = ProjectTemplate::TopDown2D);

} // namespace editor
