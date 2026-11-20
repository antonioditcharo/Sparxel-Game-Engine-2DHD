// Conversations written as screenplays: read, written, and read back the same.

#include "engine/game/conversation.h"

#include <doctest/doctest.h>

#include <optional>
#include <string>
#include <vector>

namespace game = engine::game;

namespace {

/// The RPG template's elder: a greeting, a question with two answers, and a reply to each,
/// one giving a potion and starting a quest.
game::Conversation elder() {
    game::Conversation talk;
    talk.speakers.push_back(game::Speaker{"Elder", "#ffd27a"});
    game::TalkNode start;
    start.id = "start";
    start.speaker = "Elder";
    start.text = "Welcome, traveller. A slime has been bothering the village.";
    start.next = "ask";
    game::TalkNode ask;
    ask.id = "ask";
    ask.speaker = "Elder";
    ask.text = "Will you deal with it?";
    ask.choices = {{"Yes", "yes", {}}, {"Not yet", "later", {}}};
    ask.x = 260.0;
    game::TalkNode yes;
    yes.id = "yes";
    yes.speaker = "Elder";
    yes.text = "Bless you! Take this potion.";
    yes.effects.push_back({game::TalkEffect::Kind::GiveItem, "potion", "1"});
    yes.effects.push_back({game::TalkEffect::Kind::StartQuest, "slime", "The village slime"});
    yes.x = 520.0;
    game::TalkNode later;
    later.id = "later";
    later.speaker = "Elder";
    later.text = "Come back when you are ready.";
    later.x = 520.0;
    later.y = 140.0;
    talk.nodes = {start, ask, yes, later};
    return talk;
}

} // namespace

TEST_CASE("A screenplay reads as a conversation: passages, lines, choices and notes") {
    const std::string text = "// The elder, at the gate.\n"
                             "== start\n"
                             "Elder: Welcome, traveller.\n"
                             "Elder: Will you help us?\n"
                             "[if not flag helped] [portrait art/elder_worried.png]\n"
                             "* Yes [if has sword] -> yes\n"
                             "* Not yet\n"
                             "\n"
                             "== yes\n"
                             "The elder smiles.\n"
                             "Mira: Thank you! [Really.]\n"
                             "[give potion 2] [set helped] [start quest wolves: The wolves]\n"
                             "-> start\n";
    std::string error;
    const std::optional<game::Conversation> read = game::screenplay_to_conversation(text, error);
    REQUIRE(read.has_value());
    const game::Conversation c = read.value_or(game::Conversation{});
    CHECK(error.empty());
    CHECK(c.start == "start");
    REQUIRE(c.nodes.size() == 4);
    const game::TalkNode* second = c.find("start_2");
    REQUIRE(second != nullptr);
    CHECK(c.nodes[0].next == "start_2"); // lines in a passage lead on
    CHECK(second->speaker == "Elder");
    CHECK(second->text == "Will you help us?");
    REQUIRE(second->conditions.size() == 1);
    CHECK(second->conditions[0].kind == game::TalkCondition::Kind::Flag);
    CHECK(second->conditions[0].negate);
    CHECK(second->portrait == "art/elder_worried.png");
    REQUIRE(second->choices.size() == 2);
    CHECK(second->choices[0].text == "Yes");
    CHECK(second->choices[0].next == "yes");
    REQUIRE(second->choices[0].conditions.size() == 1);
    CHECK(second->choices[0].conditions[0].kind == game::TalkCondition::Kind::HasItem);
    CHECK(second->choices[1].next.empty()); // ends
    const game::TalkNode* narration = c.find("yes");
    REQUIRE(narration != nullptr);
    CHECK(narration->speaker.empty());
    CHECK(narration->text == "The elder smiles.");
    const game::TalkNode* thanks = c.find("yes_2");
    REQUIRE(thanks != nullptr);
    CHECK(thanks->text == "Thank you! [Really.]"); // brackets in words are words
    CHECK(thanks->next == "start");
    REQUIRE(thanks->effects.size() == 3);
    CHECK(thanks->effects[0].value == "2");
    CHECK(thanks->effects[2].kind == game::TalkEffect::Kind::StartQuest);
    CHECK(thanks->effects[2].name == "wolves");
    CHECK(thanks->effects[2].value == "The wolves");
    // Both speakers, in the order met; laid out by passage.
    REQUIRE(c.speakers.size() == 2);
    CHECK(c.speakers[1].name == "Mira");
    CHECK(thanks->x == 260.0);
    CHECK(thanks->y == 160.0);
    CHECK(game::check_conversation(c).empty());
}

TEST_CASE("A conversation written as a screenplay reads back the same") {
    const game::Conversation talk = elder();
    const std::string text = game::conversation_to_screenplay(talk);
    CHECK(text == "== start\n"
                  "Elder: Welcome, traveller. A slime has been bothering the village.\n"
                  "-> ask\n"
                  "\n"
                  "== ask\n"
                  "Elder: Will you deal with it?\n"
                  "* Yes -> yes\n"
                  "* Not yet -> later\n"
                  "\n"
                  "== yes\n"
                  "Elder: Bless you! Take this potion.\n"
                  "[give potion] [start quest slime: The village slime]\n"
                  "\n"
                  "== later\n"
                  "Elder: Come back when you are ready.\n");
    std::string error;
    const std::optional<game::Conversation> back =
        game::screenplay_to_conversation(text, error, &talk);
    REQUIRE(back.has_value());
    CHECK(back.value_or(game::Conversation{}) == talk); // places and speakers kept too

    // Lines that lead on are written in one passage; words that look like something else
    // are marked; a line no passage reaches is a passage of its own.
    game::Conversation more = talk;
    more.nodes[0].next = "start_2";
    game::TalkNode aside;
    aside.id = "start_2";
    aside.text = "Note: the gate is shut.";
    aside.next = "ask";
    game::TalkNode lost;
    lost.id = "lost";
    lost.text = "* not a choice";
    lost.conditions.push_back({game::TalkCondition::Kind::QuestDone, "slime", true});
    more.nodes.push_back(aside);
    more.nodes.push_back(lost);
    const std::string written = game::conversation_to_screenplay(more);
    CHECK(written.starts_with("== start\nElder: Welcome, traveller. A slime has been "
                              "bothering the village.\n: Note: the gate is shut.\n-> ask\n"));
    CHECK(written.find("== lost\n: * not a choice\n[if not quest slime done]\n") !=
          std::string::npos);
    const std::optional<game::Conversation> again =
        game::screenplay_to_conversation(written, error, &more);
    REQUIRE(again.has_value());
    // The same, but for the order of the nodes (passages are written start first).
    const game::Conversation got = again.value_or(game::Conversation{});
    REQUIRE(got.nodes.size() == more.nodes.size());
    for (const game::TalkNode& node : more.nodes) {
        CAPTURE(node.id);
        const game::TalkNode* same = got.find(node.id);
        REQUIRE(same != nullptr);
        CHECK(*same == node);
    }
}

TEST_CASE("A screenplay that cannot be read says which line and why") {
    const auto problem = [](const char* text) {
        std::string error;
        CHECK_FALSE(game::screenplay_to_conversation(text, error).has_value());
        return error;
    };
    CHECK(problem("== ask about cat\nElder: Hi.") ==
          "Line 1: a passage's name is letters, digits and _ only, such as ask_about_cat.");
    CHECK(problem("* Yes -> yes") == "Line 1: a choice (*) answers a line, but there is none yet.");
    CHECK(problem("-> yes") == "Line 1: \"->\" goes on from a line, but there is none yet.");
    CHECK(problem("Elder: Hi.\n[dance]").starts_with("Line 2: [dance] is not a note"));
    CHECK(problem("Elder: Hi.\n* Yes [give gold] -> yes") ==
          "Line 2: A choice can only have [if ...] notes: [give gold].");
    CHECK(problem("== start\nA.\n== start\nB.") ==
          "Line 4: there are two lines called start: name the passage something else.");
    CHECK(problem("// only a comment\n\n") ==
          "There is nothing said: write a line, such as Elder: Welcome!");
    CHECK(problem("Elder: Hi.\n[give potion two]").starts_with("Line 2: [give potion two]"));
}
