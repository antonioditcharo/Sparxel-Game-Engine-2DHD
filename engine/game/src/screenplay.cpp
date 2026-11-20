// Conversations written as screenplays (conversation.h): a writer's text, turned into the
// graph of nodes and back.

#include "engine/game/conversation.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

bool is_id(std::string_view text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    });
}

/// Whether `text` can be a speaker's name before a colon: letters, digits, spaces and a
/// little punctuation, not too long.
bool is_name(std::string_view text) {
    return !text.empty() && text.size() <= 32 && std::all_of(text.begin(), text.end(), [](char c) {
        const auto u = static_cast<unsigned char>(c);
        return std::isalnum(u) != 0 || c == ' ' || c == '_' || c == '\'' || c == '-' || c == '.' ||
               u >= 0x80;
    });
}

std::vector<std::string_view> words(std::string_view text) {
    std::vector<std::string_view> out;
    std::size_t at = 0;
    while (at < text.size()) {
        while (at < text.size() && text[at] == ' ') {
            ++at;
        }
        const std::size_t end = std::min(text.find(' ', at), text.size());
        if (end > at) {
            out.push_back(text.substr(at, end - at));
        }
        at = end;
    }
    return out;
}

/// One note in brackets, without them, into a node's (or a choice's) conditions, effects
/// and portrait. False, with `error`, if it is not one.
bool read_note(std::string_view note, std::vector<TalkCondition>& conditions,
               std::vector<TalkEffect>* effects, std::string* portrait, std::string& error) {
    note = trim(note);
    const std::vector<std::string_view> w = words(note);
    const auto bad = [&] {
        error = "[" + std::string(note) +
                "] is not a note: try [if has potion], [if not flag met_elder], [if quest slime "
                "done], [give potion 2], [take potion], [set met_elder], [clear met_elder], "
                "[start quest slime: The village slime], [complete quest slime] or [portrait "
                "art/elder_glad.png].";
        return false;
    };
    if (w.empty()) {
        return bad();
    }
    if (w[0] == "if") {
        std::size_t at = 1;
        TalkCondition condition;
        if (at < w.size() && w[at] == "not") {
            condition.negate = true;
            ++at;
        }
        if (at + 1 < w.size() && w[at] == "has" && at + 2 == w.size()) {
            condition.kind = TalkCondition::Kind::HasItem;
            condition.name = std::string(w[at + 1]);
        } else if (at + 1 < w.size() && w[at] == "flag" && at + 2 == w.size()) {
            condition.kind = TalkCondition::Kind::Flag;
            condition.name = std::string(w[at + 1]);
        } else if (at + 2 < w.size() && w[at] == "quest" && at + 3 == w.size() &&
                   (w[at + 2] == "active" || w[at + 2] == "done")) {
            condition.kind = w[at + 2] == "active" ? TalkCondition::Kind::QuestActive
                                                   : TalkCondition::Kind::QuestDone;
            condition.name = std::string(w[at + 1]);
        } else {
            return bad();
        }
        conditions.push_back(std::move(condition));
        return true;
    }
    if (effects == nullptr) {
        error = "A choice can only have [if ...] notes: [" + std::string(note) + "].";
        return false;
    }
    TalkEffect effect;
    if ((w[0] == "give" || w[0] == "take") && (w.size() == 2 || w.size() == 3)) {
        effect.kind = w[0] == "give" ? TalkEffect::Kind::GiveItem : TalkEffect::Kind::TakeItem;
        effect.name = std::string(w[1]);
        effect.value = w.size() == 3 ? std::string(w[2]) : "1";
        if (!std::all_of(effect.value.begin(), effect.value.end(),
                         [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
            return bad();
        }
    } else if ((w[0] == "set" || w[0] == "clear") && w.size() == 2) {
        effect.kind = w[0] == "set" ? TalkEffect::Kind::SetFlag : TalkEffect::Kind::ClearFlag;
        effect.name = std::string(w[1]);
    } else if (w[0] == "start" && w.size() >= 3 && w[1] == "quest") {
        // start quest name: Its title
        const std::string_view rest = trim(note.substr(note.find("quest") + 5));
        const std::size_t colon = rest.find(':');
        effect.kind = TalkEffect::Kind::StartQuest;
        effect.name = std::string(trim(rest.substr(0, colon)));
        effect.value = colon == std::string_view::npos ? std::string()
                                                       : std::string(trim(rest.substr(colon + 1)));
        if (!is_id(effect.name)) {
            return bad();
        }
    } else if (w[0] == "complete" && w.size() == 3 && w[1] == "quest") {
        effect.kind = TalkEffect::Kind::CompleteQuest;
        effect.name = std::string(w[2]);
    } else if (w[0] == "portrait" && w.size() >= 2 && portrait != nullptr) {
        *portrait = std::string(trim(note.substr(note.find("portrait") + 8)));
        return true;
    } else {
        return bad();
    }
    if (effect.name.empty()) {
        return bad();
    }
    effects->push_back(std::move(effect));
    return true;
}

/// The notes in brackets at the end of `text`, read, and the text before them.
bool take_notes(std::string_view& text, std::vector<TalkCondition>& conditions,
                std::vector<TalkEffect>* effects, std::string* portrait, std::string& error) {
    text = trim(text);
    std::vector<std::string_view> found;
    while (!text.empty() && text.back() == ']') {
        const std::size_t open = text.rfind('[');
        if (open == std::string_view::npos) {
            break;
        }
        found.push_back(text.substr(open + 1, text.size() - open - 2));
        text = trim(text.substr(0, open));
    }
    std::reverse(found.begin(), found.end());
    for (const std::string_view note : found) {
        if (!read_note(note, conditions, effects, portrait, error)) {
            return false;
        }
    }
    return true;
}

std::string condition_note(const TalkCondition& c) {
    const std::string no = c.negate ? "not " : "";
    switch (c.kind) {
    case TalkCondition::Kind::HasItem:
        return "[if " + no + "has " + c.name + "]";
    case TalkCondition::Kind::Flag:
        return "[if " + no + "flag " + c.name + "]";
    case TalkCondition::Kind::QuestActive:
        return "[if " + no + "quest " + c.name + " active]";
    case TalkCondition::Kind::QuestDone:
        return "[if " + no + "quest " + c.name + " done]";
    }
    return {};
}

std::string effect_note(const TalkEffect& e) {
    switch (e.kind) {
    case TalkEffect::Kind::GiveItem:
        return "[give " + e.name + (e.value.empty() || e.value == "1" ? "" : " " + e.value) + "]";
    case TalkEffect::Kind::TakeItem:
        return "[take " + e.name + (e.value.empty() || e.value == "1" ? "" : " " + e.value) + "]";
    case TalkEffect::Kind::SetFlag:
        return "[set " + e.name + "]";
    case TalkEffect::Kind::ClearFlag:
        return "[clear " + e.name + "]";
    case TalkEffect::Kind::StartQuest:
        return "[start quest " + e.name + (e.value.empty() ? "" : ": " + e.value) + "]";
    case TalkEffect::Kind::CompleteQuest:
        return "[complete quest " + e.name + "]";
    }
    return {};
}

/// Words nobody says, written so they read back so: after ": " when they could be taken
/// for a speaker's name, a passage, a choice or a note.
std::string narration(const std::string& text) {
    const std::string_view t = text;
    const std::size_t colon = t.find(':');
    const bool named = colon != std::string_view::npos && is_name(trim(t.substr(0, colon)));
    const bool marked = t.starts_with("==") || t.starts_with("->") || t.starts_with("*") ||
                        t.starts_with("[") || t.starts_with("//") || t.starts_with(":");
    return named || marked ? ": " + text : text;
}

} // namespace

std::string conversation_to_screenplay(const Conversation& conversation) {
    std::string out;
    std::set<std::string, std::less<>> written;
    // Passages: the start first, then every node not yet written, in order.
    std::vector<const TalkNode*> heads;
    if (const TalkNode* start = conversation.find(conversation.start)) {
        heads.push_back(start);
    }
    for (const TalkNode& node : conversation.nodes) {
        heads.push_back(&node);
    }
    for (const TalkNode* head : heads) {
        if (written.contains(head->id)) {
            continue;
        }
        if (!out.empty()) {
            out += "\n";
        }
        out += "== " + head->id + "\n";
        const TalkNode* node = head;
        for (std::size_t line = 1; node != nullptr; ++line) {
            written.insert(node->id);
            out +=
                node->speaker.empty() ? narration(node->text) : node->speaker + ": " + node->text;
            out += "\n";
            std::string notes;
            for (const TalkCondition& c : node->conditions) {
                notes += (notes.empty() ? "" : " ") + condition_note(c);
            }
            for (const TalkEffect& e : node->effects) {
                notes += (notes.empty() ? "" : " ") + effect_note(e);
            }
            if (!node->portrait.empty()) {
                notes +=
                    (notes.empty() ? "" : " ") + std::string("[portrait ") + node->portrait + "]";
            }
            if (!notes.empty()) {
                out += notes + "\n";
            }
            for (const TalkChoice& choice : node->choices) {
                out += "* " + choice.text;
                for (const TalkCondition& c : choice.conditions) {
                    out += " " + condition_note(c);
                }
                out += choice.next.empty() ? std::string() : " -> " + choice.next;
                out += "\n";
            }
            const TalkNode* next = conversation.find(node->next);
            // The next line goes on in the passage if it is the one the passage's next line
            // would be called, and not written yet; otherwise it is named.
            const std::string chained = head->id + "_" + std::to_string(line + 1);
            if (node->choices.empty() && next != nullptr && next->id == chained &&
                !written.contains(next->id) && next->id != conversation.start) {
                node = next;
                continue;
            }
            if (!node->next.empty()) {
                out += "-> " + node->next + "\n";
            }
            node = nullptr;
        }
    }
    return out;
}

std::optional<Conversation> screenplay_to_conversation(std::string_view text, std::string& error,
                                                       const Conversation* keep) {
    Conversation c;
    if (keep != nullptr) {
        c.speakers = keep->speakers;
    }
    std::string passage;        // the passage being read
    std::size_t in_passage = 0; // its lines so far
    std::size_t passages = 0;
    bool chain = false; // whether the last line leads on to the next one written
    std::size_t number = 0;
    std::size_t at = 0;
    const auto fail = [&](const std::string& why) {
        error = "Line " + std::to_string(number) + ": " + why;
        return std::nullopt;
    };
    while (at <= text.size()) {
        const std::size_t end = std::min(text.find('\n', at), text.size());
        const std::string_view line = trim(text.substr(at, end - at));
        at = end + 1;
        ++number;
        if (line.empty() || line.starts_with("//")) {
            continue;
        }
        if (line.starts_with("==")) {
            const std::string_view id = trim(line.substr(2));
            if (!is_id(id)) {
                return fail(
                    "a passage's name is letters, digits and _ only, such as ask_about_cat.");
            }
            passage = std::string(id);
            in_passage = 0;
            ++passages;
            chain = false;
            continue;
        }
        TalkNode* last = c.nodes.empty() ? nullptr : &c.nodes.back();
        if (line.starts_with("->")) {
            const std::string_view target = trim(line.substr(2));
            if (last == nullptr || (!target.empty() && !is_id(target))) {
                return fail(last == nullptr ? "\"->\" goes on from a line, but there is none yet."
                                            : "\"->\" names a passage: letters, digits and _.");
            }
            last->next = std::string(target);
            chain = false;
            continue;
        }
        if (line.starts_with("*")) {
            if (last == nullptr) {
                return fail("a choice (*) answers a line, but there is none yet.");
            }
            std::string_view words_of = trim(line.substr(1));
            TalkChoice choice;
            if (const std::size_t arrow = words_of.rfind("->"); arrow != std::string_view::npos) {
                const std::string_view target = trim(words_of.substr(arrow + 2));
                if (!target.empty() && !is_id(target)) {
                    return fail("\"->\" names a passage: letters, digits and _.");
                }
                choice.next = std::string(target);
                words_of = words_of.substr(0, arrow);
            }
            if (!take_notes(words_of, choice.conditions, nullptr, nullptr, error)) {
                return fail(std::string(error));
            }
            choice.text = std::string(words_of);
            last->choices.push_back(std::move(choice));
            chain = false;
            continue;
        }
        if (line.starts_with("[")) {
            if (last == nullptr) {
                return fail("a note in brackets goes with a line, but there is none yet.");
            }
            std::string_view notes = line;
            if (!take_notes(notes, last->conditions, &last->effects, &last->portrait, error) ||
                !notes.empty()) {
                return fail(notes.empty()
                                ? std::string(error)
                                : "notes are in brackets, each on its own: [give potion]");
            }
            continue;
        }
        // A line someone says: "Name: words", or words alone (after ":" if they might be
        // taken for something else).
        TalkNode node;
        const std::size_t colon = line.find(':');
        if (line.starts_with(':')) {
            node.text = std::string(trim(line.substr(1)));
        } else if (colon != std::string_view::npos && is_name(trim(line.substr(0, colon)))) {
            node.speaker = std::string(trim(line.substr(0, colon)));
            node.text = std::string(trim(line.substr(colon + 1)));
        } else {
            node.text = std::string(line);
        }
        if (passage.empty()) {
            passage = "start";
            ++passages;
        }
        ++in_passage;
        node.id = in_passage == 1 ? passage : passage + "_" + std::to_string(in_passage);
        if (c.find(node.id) != nullptr) {
            return fail("there are two lines called " + node.id +
                        ": name the passage something else.");
        }
        if (c.nodes.size() >= most_talk_nodes) {
            return fail("a conversation has at most " + std::to_string(most_talk_nodes) +
                        " lines.");
        }
        // Laid out in rows by passage, across by line: unless the graph already had it.
        node.x = static_cast<double>(in_passage - 1) * 260.0;
        node.y = static_cast<double>(passages - 1) * 160.0;
        if (keep != nullptr) {
            if (const TalkNode* was = keep->find(node.id)) {
                node.x = was->x;
                node.y = was->y;
            }
        }
        if (chain && last != nullptr) {
            last->next = node.id;
        }
        if (!node.speaker.empty() &&
            std::none_of(c.speakers.begin(), c.speakers.end(),
                         [&](const Speaker& s) { return s.name == node.speaker; })) {
            c.speakers.push_back(Speaker{node.speaker});
        }
        c.nodes.push_back(std::move(node));
        chain = true;
    }
    if (c.nodes.empty()) {
        error = "There is nothing said: write a line, such as Elder: Welcome!";
        return std::nullopt;
    }
    c.start = c.nodes.front().id;
    return c;
}

} // namespace engine::game
