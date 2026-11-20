// Versions with Git: what git prints, read.

#include <doctest/doctest.h>

#include <string>
#include <string_view>
#include <vector>

#include "editor/core/versions.h"

using editor::Version;
using editor::VersionChange;
using Kind = editor::VersionChange::Kind;

TEST_CASE("Versions: git's status read as changes") {
    using namespace std::string_view_literals;
    // Changed, new (untracked and added), deleted, a rename (its old name after it), a
    // conflict, and a name with spaces.
    const std::string_view status = " M assets/maps/start.dscene\0"
                                    "?? assets/art/new hero.png\0"
                                    "A  assets/logic/doors.drules\0"
                                    " D assets/art/old.png\0"
                                    "R  assets/art/rock.png\0assets/art/stone.png\0"
                                    "UU project.sparxel.json\0"
                                    "MM assets/talk/elder.dtalk\0"
                                    "x\0"sv;
    const std::vector<VersionChange> changes = editor::parse_git_status(status);
    CHECK(changes == std::vector<VersionChange>{
                         {Kind::Changed, "assets/maps/start.dscene"},
                         {Kind::New, "assets/art/new hero.png"},
                         {Kind::New, "assets/logic/doors.drules"},
                         {Kind::Deleted, "assets/art/old.png"},
                         {Kind::Renamed, "assets/art/rock.png"},
                         {Kind::Conflict, "project.sparxel.json"},
                         {Kind::Changed, "assets/talk/elder.dtalk"},
                     });
    CHECK(editor::parse_git_status("").empty());
    CHECK(editor::change_word(Kind::Conflict) == "In conflict");
    CHECK(editor::change_word(Kind::New) == "New");
}

TEST_CASE("Versions: git's history read, the newest first") {
    const std::string text = "a1b2c3d\x1f"
                             "Ada\x1f"
                             "2 minutes ago\x1f"
                             "The cave, and a bat\x1e\n"
                             "e4f5a6b\x1f"
                             "Sam\x1f"
                             "3 days ago\x1f"
                             "First: the village\x1e";
    const std::vector<Version> versions = editor::parse_git_log(text);
    REQUIRE(versions.size() == 2);
    CHECK(versions[0] == Version{"a1b2c3d", "Ada", "2 minutes ago", "The cave, and a bat"});
    CHECK(versions[1].message == "First: the village");
    CHECK(editor::parse_git_log("").empty());
    CHECK(editor::parse_git_log("not a record\x1e").empty());
    CHECK(editor::project_gitignore().find(".sparxel/\n") != std::string::npos);
}
