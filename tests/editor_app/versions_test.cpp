// Versions with Git from the editor: started, saved, listed (needs the git program; without
// it the test says so and passes). And asset packs, exported and brought in.

#include "engine/foundation/guid.h"

#include <doctest/doctest.h>

#include <QApplication>
#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "editor/core/new_project.h"
#include "editor/core/versions.h"
#include "main_window.h"

namespace fs = std::filesystem;
using engine::Guid;

TEST_CASE("Versions: a project kept with Git, saved, changed and saved again") {
    if (!editor::MainWindow::git_found()) {
        MESSAGE("git is not installed: versions are not tested here");
        return;
    }
    const fs::path path =
        fs::temp_directory_path() / ("sparxel-versions-" + Guid::generate().to_string());
    fs::copy(fs::path(SPARXEL_SAMPLES_DIR) / "hello", path, fs::copy_options::recursive);
    {
        editor::MainWindow window(SPARXEL_PLAYER);
        REQUIRE(window.open_project(path));
        CHECK_FALSE(window.keeps_versions());
        CHECK(window.versions().empty());

        REQUIRE(window.start_versions());
        CHECK(window.keeps_versions());
        std::ifstream ignore(path / ".gitignore");
        const std::string kept((std::istreambuf_iterator<char>(ignore)),
                               std::istreambuf_iterator<char>());
        CHECK(kept == editor::project_gitignore());
        // Every file is new; the editor's working files are not among them.
        const std::vector<editor::VersionChange> first = window.version_changes();
        CHECK_FALSE(first.empty());
        bool working = false;
        for (const editor::VersionChange& change : first) {
            CHECK(change.kind == editor::VersionChange::Kind::New);
            working = working || change.path.starts_with(".sparxel/");
        }
        CHECK_FALSE(working);

        CHECK_FALSE(window.save_version(QStringLiteral("   "))); // no words: refused
        REQUIRE(window.save_version(QStringLiteral("The village as it began")));
        std::vector<editor::Version> saved = window.versions();
        REQUIRE(saved.size() == 1);
        CHECK(saved[0].message == "The village as it began");
        CHECK(window.version_changes().empty());
        CHECK_FALSE(window.save_version(QStringLiteral("Again"))); // nothing changed

        // A new file: listed, then saved as the newest version.
        std::ofstream(path / "assets" / "notes.txt") << "a cave to the north\n";
        CHECK(window.version_changes() ==
              std::vector<editor::VersionChange>{
                  {editor::VersionChange::Kind::New, "assets/notes.txt"}});
        REQUIRE(window.save_version(QStringLiteral("Notes on the cave")));
        saved = window.versions();
        REQUIRE(saved.size() == 2);
        CHECK(saved[0].message == "Notes on the cave");

        // The dialog lists the history, and nothing to save.
        int history = -1;
        int changes = -1;
        bool startable = true;
        QTimer::singleShot(0, [&] {
            for (QWidget* top : QApplication::topLevelWidgets()) {
                if (top->objectName() == QStringLiteral("versions_dialog")) {
                    history =
                        top->findChild<QListWidget*>(QStringLiteral("version_history"))->count();
                    changes =
                        top->findChild<QListWidget*>(QStringLiteral("version_changes"))->count();
                    startable = top->findChild<QPushButton*>(QStringLiteral("start_versions"))
                                    ->isVisibleTo(top);
                    qobject_cast<QDialog*>(top)->reject();
                }
            }
        });
        window.show_versions();
        CHECK(history == 2);
        CHECK(changes == 0);
        CHECK_FALSE(startable);
    }
    std::error_code ignored;
    fs::remove_all(path, ignored);
}

TEST_CASE("Asset packs from the window: exported from one project, brought into another") {
    const fs::path base =
        fs::temp_directory_path() / ("sparxel-packs-app-" + Guid::generate().to_string());
    const fs::path village = base / "village";
    const fs::path other = base / "other";
    const fs::path pack = base / "elder.sparxelpack";
    std::string error;
    REQUIRE(editor::create_project(village, "Village", error, editor::ProjectTemplate::Rpg));
    REQUIRE(editor::create_project(other, "Other", error));
    {
        editor::MainWindow window(SPARXEL_PLAYER);
        REQUIRE(window.open_project(village));
        REQUIRE(window.export_asset_pack({QStringLiteral("logic")},
                                         QString::fromStdU16String(pack.u16string()),
                                         QStringLiteral("The elder")));
        CHECK(fs::exists(pack / "pack.json"));
        REQUIRE(window.open_project(other));
        REQUIRE(window.import_asset_pack(QString::fromStdU16String(pack.u16string())));
        // Open again with the logic and its conversation, which it checks.
        REQUIRE(window.document() != nullptr);
        bool listed = false;
        for (const editor::LogicFile& file : window.document()->logic()) {
            listed = listed || file.path == "logic/village.drules";
        }
        CHECK(listed);
        CHECK(fs::exists(other / "assets" / "talk" / "elder.dtalk"));
        // Again: nothing new, and said so.
        CHECK(window.import_asset_pack(QString::fromStdU16String(pack.u16string())));
        CHECK_FALSE(
            window.import_asset_pack(QString::fromStdU16String((base / "none").u16string())));
    }
    std::error_code ignored;
    fs::remove_all(base, ignored);
}
