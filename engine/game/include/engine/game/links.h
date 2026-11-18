#pragma once

// Links (plan/05 §2, as Fortnite's devices are wired): an object's events joined straight to
// another object's actions, written on the object itself, with no Rule to write: on a
// pressure plate, "entered -> gate.open"; on a lever, "interacted -> lamp.play_tag on". The
// game turns every link of every scene into a Rule as it starts, so links work exactly as
// Rules do (and are checked as Rules are).
//
// See docs/dev/game.md, *Links*.

#include "engine/logic/compiler.h"
#include "engine/logic/ir.h"
#include "engine/serialize/json.h"
#include "engine/world/world.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::game {

/// "Links": what happens to other objects when this one's events happen, a line each:
/// "entered -> gate.open", "interacted -> elder.say Welcome!", "defeated -> door.enabled
/// no" (an action and what it is given, or a property and what it becomes).
struct Links {
    std::string links;
};

/// One link: when `event` happens to the object, `target` does `member` (an action, or a
/// property set), given `value` (empty if it takes nothing).
struct Link {
    std::string event;
    std::string target;
    std::string member;
    std::string value;
    friend bool operator==(const Link&, const Link&) = default;
};

/// The links in `text`, a line each ("event -> object.member value"); blank lines and lines
/// starting "//" are skipped. Nothing, with `error` naming the line, if one cannot be read.
[[nodiscard]] std::optional<std::vector<Link>> parse_links(std::string_view text,
                                                           std::string& error);

/// Adds a Rule to `rules` for each link of every entity with Links in `world`: when the
/// event happens to it (by its GUID), the target's action is done with the value as its
/// first argument, or the property is set to it, the text turned into what the action or
/// property takes (a number, yes or no, a choice, words). `types` tells what objects are.
/// Lines that cannot be read, or name what is not there, are reported in `issues` (by the
/// entity's name) and left out. Block ids go on from `next_block`.
void add_link_rules(const world::World& world, const logic::ObjectTypes& types,
                    logic::LogicAsset& rules, std::uint64_t& next_block,
                    std::vector<serialize::Issue>& issues);

} // namespace engine::game
