#include "engine/game/links.h"

#include "engine/game/logic_world.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <utility>

namespace engine::game {

namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

bool is_word(std::string_view text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    });
}

/// `text` as a value of `kind`; nothing if it is not one.
std::optional<reflect::Value> value_of(std::string_view text, reflect::ValueKind kind,
                                       const reflect::ChoiceInfo* choice) {
    switch (kind) {
    case reflect::ValueKind::Text:
        return reflect::Value{std::string(text)};
    case reflect::ValueKind::Bool:
        if (text == "yes" || text == "true" || text == "on") {
            return reflect::Value{true};
        }
        if (text == "no" || text == "false" || text == "off") {
            return reflect::Value{false};
        }
        return std::nullopt;
    case reflect::ValueKind::Int: {
        std::int64_t v = 0;
        const auto [end, failed] = std::from_chars(text.data(), text.data() + text.size(), v);
        return failed == std::errc{} && end == text.data() + text.size()
                   ? std::optional<reflect::Value>(reflect::Value{v})
                   : std::nullopt;
    }
    case reflect::ValueKind::Float: {
        try {
            std::size_t used = 0;
            const double v = std::stod(std::string(text), &used);
            return used == text.size() && std::isfinite(v)
                       ? std::optional<reflect::Value>(reflect::Value{v})
                       : std::nullopt;
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    case reflect::ValueKind::Choice:
        if (choice != nullptr) {
            if (const reflect::ChoiceEntry* entry = choice->find_entry(text)) {
                return reflect::Value{entry->value};
            }
        }
        return std::nullopt;
    }
    return std::nullopt;
}

} // namespace

std::optional<std::vector<Link>> parse_links(std::string_view text, std::string& error) {
    std::vector<Link> links;
    std::size_t at = 0;
    std::size_t number = 0;
    while (at <= text.size()) {
        const std::size_t end = std::min(text.find('\n', at), text.size());
        const std::string_view line = trim(text.substr(at, end - at));
        at = end + 1;
        ++number;
        if (line.empty() || line.starts_with("//")) {
            continue;
        }
        const auto bad = [&] {
            error = "Line " + std::to_string(number) +
                    ": a link reads \"event -> object.action\", such as entered -> gate.open, "
                    "or with a value after it: interacted -> elder.say Welcome!";
            return std::nullopt;
        };
        const std::size_t arrow = line.find("->");
        if (arrow == std::string_view::npos) {
            return bad();
        }
        Link link;
        link.event = std::string(trim(line.substr(0, arrow)));
        const std::string_view rest = trim(line.substr(arrow + 2));
        const std::size_t space = std::min(rest.find(' '), rest.size());
        const std::string_view call = rest.substr(0, space);
        const std::size_t dot = call.rfind('.');
        if (!is_word(link.event) || dot == std::string_view::npos || dot == 0) {
            return bad();
        }
        link.target = std::string(call.substr(0, dot));
        link.member = std::string(call.substr(dot + 1));
        link.value = std::string(trim(rest.substr(space)));
        if (!is_word(link.member)) {
            return bad();
        }
        links.push_back(std::move(link));
    }
    return links;
}

void add_link_rules(const world::World& world, const logic::ObjectTypes& types,
                    logic::LogicAsset& rules, std::uint64_t& next_block,
                    std::vector<serialize::Issue>& issues) {
    const reflect::TypeInfo* type = world.registry().find_type<Links>();
    if (type == nullptr) {
        return;
    }
    world.each(*type, [&](world::Entity entity, reflect::ObjectRef ref) {
        const Links* links = ref.as<Links>();
        if (links == nullptr || links->links.empty()) {
            return;
        }
        const std::string name(world.name(entity));
        const std::string where =
            "links of " + (name.empty() ? world.guid(entity).to_string() : name);
        std::string error;
        const std::optional<std::vector<Link>> parsed = parse_links(links->links, error);
        if (!parsed) {
            issues.push_back(serialize::Issue{where, error});
            return;
        }
        const std::vector<const reflect::TypeInfo*> own = world.components(entity);
        const std::string called = name.empty() ? std::string("it") : name;
        for (const Link& link : *parsed) {
            const std::string said = link.event + " -> " + link.target + "." + link.member;
            // Its own event, checked here so one wrong link does not stop the others.
            if (std::none_of(own.begin(), own.end(), [&](const reflect::TypeInfo* part) {
                    return part->find_event(link.event) != nullptr;
                })) {
                std::string message = "\"" + said + "\": ";
                message += called;
                message += " has no event called ";
                message += link.event;
                message += ".";
                issues.push_back(serialize::Issue{where, std::move(message)});
                continue;
            }
            // What the target's member takes.
            const reflect::FunctionInfo* action = nullptr;
            const reflect::PropertyInfo* property = nullptr;
            for (const reflect::TypeInfo* part :
                 types ? types(link.target) : std::vector<const reflect::TypeInfo*>{}) {
                if (action == nullptr && property == nullptr) {
                    action = part->find_function(link.member);
                    property = action == nullptr ? part->find_property(link.member) : nullptr;
                }
            }
            if (action == nullptr && property == nullptr) {
                issues.push_back(serialize::Issue{where, "\"" + said + "\": " + link.target +
                                                             " cannot " + link.member + "."});
                continue;
            }
            std::vector<logic::Expr> arguments;
            const reflect::ValueKind kind =
                action != nullptr ? (action->parameters.empty() ? reflect::ValueKind::Text
                                                                : action->parameters.front().kind)
                                  : property->kind;
            const reflect::ChoiceInfo* choice =
                action != nullptr
                    ? (action->parameters.empty() ? nullptr : action->parameters.front().choice)
                    : property->choice;
            const bool wants = property != nullptr || !action->parameters.empty();
            if (wants) {
                const std::optional<reflect::Value> value = value_of(link.value, kind, choice);
                if (!value) {
                    issues.push_back(
                        serialize::Issue{where, "\"" + said + "\" needs " +
                                                    std::string(reflect::kind_display_name(kind)) +
                                                    " after it, not \"" + link.value + "\"."});
                    continue;
                }
                arguments.push_back(logic::ir::literal(*value));
            }
            logic::Rule rule;
            rule.id = logic::BlockId{next_block++};
            rule.object = world.guid(entity).to_string();
            rule.event = link.event;
            if (property != nullptr) {
                rule.actions.push_back(
                    logic::ir::set(next_block++, link.target, link.member, arguments.front()));
            } else {
                // Actions with more than one value take only the first from a link; the rest
                // keep their defaults.
                for (std::size_t i = 1; i < action->parameters.size(); ++i) {
                    arguments.push_back(logic::ir::literal(action->parameters[i].default_value));
                }
                rule.actions.push_back(logic::ir::do_action(next_block++, link.target, link.member,
                                                            std::move(arguments)));
            }
            rules.rules.push_back(std::move(rule));
        }
    });
}

} // namespace engine::game
