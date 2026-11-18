// Asset packs: what a pack carries, written, and brought into another project.

#include "engine/assets/assets.h"
#include "engine/foundation/guid.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "editor/core/asset_packs.h"
#include "editor/core/new_project.h"

namespace fs = std::filesystem;
using engine::Guid;

namespace {

struct Folder {
    Folder() : path(fs::temp_directory_path() / ("sparxel-pack-" + Guid::generate().to_string())) {}
    Folder(const Folder&) = delete;
    Folder& operator=(const Folder&) = delete;
    Folder(Folder&&) = delete;
    Folder& operator=(Folder&&) = delete;
    ~Folder() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
    fs::path path;
};

bool has(const std::vector<std::string>& files, const std::string& file) {
    return std::find(files.begin(), files.end(), file) != files.end();
}

std::string read(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string guid_of(const fs::path& project, const std::string& asset) {
    engine::assets::AssetDatabase db(project / "assets");
    static_cast<void>(db.refresh());
    const engine::assets::AssetRecord* record = db.find(asset);
    return record != nullptr ? record->guid.to_string() : std::string();
}

} // namespace

TEST_CASE("Asset packs: what they carry, written, and brought into another project") {
    const Folder village;
    const Folder other;
    const Folder packs;
    std::string error;
    REQUIRE(editor::create_project(village.path, "Village", error, editor::ProjectTemplate::Rpg));
    REQUIRE(editor::create_project(other.path, "Other", error));

    // A scene carries the pictures it shows (named by GUID), not the logic about it.
    const std::vector<std::string> scene =
        editor::pack_contents(village.path, {"maps/start.dscene"});
    CHECK(has(scene, "maps/start.dscene"));
    CHECK(has(scene, "art/tiles.png"));
    CHECK(has(scene, "art/elder.png"));
    CHECK(has(scene, "art/panel.png"));
    CHECK_FALSE(has(scene, "logic/village.drules"));
    // A folder carries all of it, and what it names by path: the elder's conversation.
    const std::vector<std::string> logic = editor::pack_contents(village.path, {"logic"});
    CHECK(has(logic, "logic/village.drules"));
    CHECK(has(logic, "talk/elder.dtalk"));
    CHECK(editor::pack_contents(village.path, {"nothing/here.png"}).empty());

    // Written: the files with their .meta, and pack.json.
    const fs::path pack = packs.path / "village.sparxelpack";
    const std::optional<std::vector<std::string>> written = editor::export_pack(
        village.path, {"maps/start.dscene", "logic"}, pack, "The village", error);
    REQUIRE(written.has_value());
    CHECK(fs::exists(pack / "pack.json"));
    CHECK(fs::exists(pack / "assets" / "art" / "elder.png.meta"));
    CHECK(fs::exists(pack / "assets" / "talk" / "elder.dtalk"));
    CHECK_FALSE(editor::export_pack(village.path, {"logic"}, pack, "Again", error).has_value());
    CHECK(error == "That folder already has something in it: choose an empty or a new one.");
    CHECK_FALSE(editor::export_pack(village.path, {}, packs.path / "empty", "None", error));

    // Brought into a project made from another template: refused, since its start scene
    // is a different one, and nothing is copied.
    CHECK_FALSE(editor::import_pack(other.path, pack, error).has_value());
    CHECK(error.find("maps/start.dscene is in the project already, and different") !=
          std::string::npos);
    CHECK_FALSE(fs::exists(other.path / "assets" / "logic" / "village.drules"));
    // The logic alone comes in, with the conversation it names and their GUIDs.
    const fs::path talk_pack = packs.path / "talk.sparxelpack";
    REQUIRE(editor::export_pack(village.path, {"logic"}, talk_pack, "The elder", error));
    const std::optional<editor::PackImport> brought =
        editor::import_pack(other.path, talk_pack, error);
    REQUIRE(brought.has_value());
    const editor::PackImport result = brought.value_or(editor::PackImport{});
    CHECK(result.name == "The elder");
    CHECK(has(result.added, "logic/village.drules"));
    CHECK(has(result.added, "talk/elder.dtalk"));
    CHECK(result.already.empty());
    CHECK(guid_of(other.path, "talk/elder.dtalk") == guid_of(village.path, "talk/elder.dtalk"));
    // Again: all there already, nothing added.
    const std::optional<editor::PackImport> again =
        editor::import_pack(other.path, talk_pack, error);
    REQUIRE(again.has_value());
    CHECK(again.value_or(editor::PackImport{}).added.empty());
    CHECK(again.value_or(editor::PackImport{}).already.size() == result.added.size());
}

TEST_CASE("Asset packs: brought in again, kept; different files where they go, refused") {
    const Folder village;
    const Folder other;
    const Folder packs;
    std::string error;
    REQUIRE(editor::create_project(village.path, "Village", error, editor::ProjectTemplate::Rpg));
    REQUIRE(editor::create_project(other.path, "Other", error));
    // A scene of its own, using the elder's picture and the tiles.
    std::filesystem::copy_file(village.path / "assets" / "maps" / "start.dscene",
                               village.path / "assets" / "maps" / "square.dscene");
    const fs::path pack = packs.path / "square.sparxelpack";
    REQUIRE(editor::export_pack(village.path, {"maps/square.dscene"}, pack, "Square", error));
    const std::optional<editor::PackImport> first = editor::import_pack(other.path, pack, error);
    REQUIRE(first.has_value());
    CHECK(has(first.value_or(editor::PackImport{}).added, "maps/square.dscene"));
    // The square's tiles are the project's own now.
    const std::string square = read(other.path / "assets" / "maps" / "square.dscene");
    CHECK(square.find(guid_of(other.path, "art/tiles.png")) != std::string::npos);
    CHECK(square.find(guid_of(village.path, "art/tiles.png")) == std::string::npos);
    CHECK(square.find(guid_of(village.path, "art/elder.png")) != std::string::npos);

    // A picture changed in the project: bringing the pack in again is refused.
    std::ofstream(other.path / "assets" / "art" / "elder.png", std::ios::binary) << "changed";
    CHECK_FALSE(editor::import_pack(other.path, pack, error).has_value());
    CHECK(error.starts_with("The pack was not brought in: "));
    CHECK(error.find("art/elder.png is in the project already, and different") !=
          std::string::npos);
    CHECK_FALSE(editor::import_pack(other.path, packs.path / "missing", error).has_value());
    CHECK(error == "That folder is not an asset pack: it has no pack.json.");
}
