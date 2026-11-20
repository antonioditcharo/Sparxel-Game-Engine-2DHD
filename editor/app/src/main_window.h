#pragma once

// The editor's main window (plan/06 §2): the scene in the middle, the project's assets and
// the scene's entities on the left, the inspector on the right, and the commands that
// matter most a click away: save, undo and redo, add and delete, Play and Export.
//
// See docs/dev/editor.md.

#include <QMainWindow>
#include <QStringList>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "editor/core/asset_tools.h"
#include "editor/core/characters.h"
#include "editor/core/doctor.h"
#include "editor/core/document.h"
#include "editor/core/new_project.h"
#include "editor/core/versions.h"
#include "search.h"

class QAction;
class QTimer;
class QStackedWidget;
class QSettings;
class QTreeWidget;
class QTreeView;
class QDialog;
class QLineEdit;
class QFileSystemModel;

namespace editor {

class GameView;

class HelpWindow;
class TutorialPanel;

class LogicPanel;
class ProjectPanel;

class Inspector;
class SceneView;
class SpriteStudio;
class StartPage;
class SfxGenerator;
class DoctorPanel;
class StoryWindow;
class ScriptWindow;
class TableWindow;
class TilePalette;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    /// `player` is the sparxel-player executable that Play runs and Export ships.
    explicit MainWindow(std::filesystem::path player, QWidget* parent = nullptr);
    ~MainWindow() override;
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;
    MainWindow(MainWindow&&) = delete;
    MainWindow& operator=(MainWindow&&) = delete;

    /// Opens a project folder. Returns false, showing why, if it cannot be opened. The
    /// project goes to the top of the recent projects.
    bool open_project(const std::filesystem::path& folder);
    /// Closes the project (asking to save its changes) and shows the start page. False if
    /// it stays open.
    bool close_project();
    /// Where the recent projects are kept between sessions (the user's settings); without
    /// it, they are kept only while the editor runs.
    void set_settings(QSettings* settings);
    /// The sample project the start page offers to try; empty offers none.
    void set_sample(const std::filesystem::path& folder);
    /// The projects opened lately, by folder, the latest first (at most 10).
    [[nodiscard]] const QStringList& recent_projects() const noexcept { return recent_; }
    /// Leaves a project off the recent projects.
    void forget_recent(const QString& folder);
    [[nodiscard]] StartPage* start_page() const noexcept { return start_; }
    [[nodiscard]] DoctorPanel* doctor() const noexcept { return doctor_; }
    /// Checks the project with Project Doctor, showing what it found.
    void check_project();
    /// Goes where a finding can be fixed: its scene and entity, its logic, or its asset.
    void go_to(const Finding& finding);
    /// Whether the start page is showing (no project open).
    [[nodiscard]] bool starting() const noexcept;
    /// Makes a new project named `name` in `folder` (new or empty), a small starter game,
    /// and opens it. Returns false, with the reason shown, if it cannot.
    bool new_project(const std::filesystem::path& folder, const QString& name,
                     ProjectTemplate kind = ProjectTemplate::TopDown2D, std::uint32_t width = 0,
                     std::uint32_t height = 0);
    /// Edits another scene of the project (its path in assets/), saving the open one
    /// first if it changed. Returns false, showing why, if it cannot.
    bool open_scene(const QString& path);
    /// Makes an empty scene named `name` in assets/maps and opens it, saving first.
    bool new_scene(const QString& name);
    /// Makes the open scene the one the game starts in.
    bool start_here();
    /// The window's size and where its panels are (docked, floating, hidden, their sizes),
    /// written to `settings` and read back: the editor keeps them between sessions.
    void save_layout(QSettings& settings) const;
    /// False, changing nothing, if `settings` holds no layout or one this editor cannot use.
    bool restore_layout(QSettings& settings);
    /// Every panel back where it was when the editor first opened.
    void reset_layout();
    /// Quiet: problems go to the status bar only, never to a dialog that waits for
    /// someone (for checks that run with no one there).
    void set_quiet(bool quiet) noexcept { quiet_ = quiet; }
    /// Opens a picture or sprite of the project (its path in assets/: a .png or a .dsprite)
    /// in Sprite Studio, or brings its window forward if it is open. A PNG with a .dsprite
    /// of the same name opens that; one without is cut into frames of `frame_width` x
    /// `frame_height` (0 for the whole picture; -1 asks, offering the frame size the scene's
    /// sprites use with it), and saving makes its .dsprite. Null, showing why, if it cannot.
    SpriteStudio* open_sprite(const QString& asset_path, int frame_width = -1,
                              int frame_height = -1);
    /// Makes an empty sprite of `width` x `height` pixels as assets/art/<name>.dsprite (with
    /// its sheet, art/<name>.png) and opens it in Sprite Studio.
    SpriteStudio* new_sprite(const QString& name, int width, int height);
    /// Keeps the unsaved work in the project's recovery file (Document::write_recovery()).
    /// Runs every minute, and whenever the editor stops being the window in front.
    bool autosave();
    /// Puts back the unsaved work a crash left, as one undo step. The editor offers it when a
    /// project with such work opens.
    bool restore_recovery();
    /// What Search everything (Ctrl+K) can find now: the enabled commands of the menus, the
    /// project's scenes, pictures, sprites and logic files, and the scene's entities, each
    /// with what choosing it does (run, open, select).
    [[nodiscard]] std::vector<SearchItem> search_items();
    /// Opens Search everything, empty, its results all there.
    void show_search();
    [[nodiscard]] SearchBox* search_box() const noexcept { return search_; }
    /// Opens a conversation of the project (its path in assets/, a .dtalk) in the Story
    /// editor, or brings its window forward. Null, showing why, if it cannot.
    StoryWindow* open_story(const QString& asset_path);
    /// Opens the Luau script at `asset_path` (relative to assets/) in the Script editor, or
    /// brings its window forward. Null, saying why, if it cannot be read.
    ScriptWindow* open_script(const QString& asset_path);
    /// File > New script: makes scripts/<name>.luau, with a first event to start from, and
    /// opens it. Null, saying why, if the name is not usable or taken.
    ScriptWindow* new_script(const QString& name);
    /// Makes auto-tiles from the picture at `asset_path` in assets/ (Document::
    /// make_autotiles()); returns the tileset made, or empty, saying why in the status bar.
    QString make_autotiles(const QString& asset_path);
    /// Makes a normal map or a glow map of the picture at `asset_path` beside it
    /// (Document::make_normal_map(), make_glow_map()); returns the map made, or empty,
    /// saying why in the status bar.
    QString make_normal_map(const QString& asset_path,
                            const engine::assets::NormalMapSettings& settings);
    QString make_glow_map(const QString& asset_path, float threshold);
    /// The Assets list's *Make a normal map…*: asks how, then makes it.
    void ask_normal_map(const QString& asset_path);
    /// Imports an Aseprite file of the project (Document::import_aseprite()): its sprite
    /// sheet, made again whenever the file changes. Returns the picture made, or empty.
    QString import_aseprite(const QString& asset_path);
    /// Collects the texts to translate into data/translations
    /// (Document::collect_translations()) and opens it; null if it cannot.
    TableWindow* collect_translations();
    /// Opens the Sparxel guide (Help, F1), at `page` ("shop.md") if given.
    HelpWindow* show_guide(const QString& page);
    /// Saves, then keeps the whole project as a snapshot named `name`, with a picture of the
    /// scene (editor/core/snapshots.h). False, saying why, if it cannot.
    bool take_snapshot(const QString& name);
    /// Saves, then puts the project back as snapshot `id` had it and opens it again.
    bool restore_snapshot(const QString& id);
    /// File > Snapshots: the project's snapshots with their pictures, to go back to or delete.
    void show_snapshots();

    // ----- Versions with Git (editor/core/versions.h) -----
    /// Whether the git program was found.
    [[nodiscard]] static bool git_found();
    /// Whether the open project's versions are kept with Git.
    [[nodiscard]] bool keeps_versions() const;
    /// Starts keeping the project's versions with Git, and a .gitignore keeping the
    /// editor's working files out. False, saying why in the status bar, if it cannot.
    bool start_versions();
    /// Saves the project, then a version of it described by `message`. False, saying why,
    /// if nothing changed or it could not.
    bool save_version(const QString& message);
    /// What changed since the last version; the versions, the newest first (the last 100).
    [[nodiscard]] std::vector<VersionChange> version_changes() const;
    [[nodiscard]] std::vector<Version> versions() const;
    /// File > Versions: what changed, saving a version, and the history.
    void show_versions();
    /// Lists in the Assets panel only names with every word of `text`, in order.
    void search_assets(const QString& text);
    /// Shows (and returns) what uses the asset at `asset`, relative to assets/.
    std::vector<AssetUse> show_usages(const QString& asset);
    /// Saves, then renames the asset at `asset` (with its companions) to `name`, updating
    /// what names it, and opens the project again. False, saying why, if it cannot.
    bool rename_asset(const QString& asset, const QString& name);
    /// Saves, then writes the assets `chosen` (files or folders, relative to assets/) with
    /// everything they use as an asset pack in the folder `pack` (editor/core/asset_packs.h).
    /// False, saying why, if it cannot.
    bool export_asset_pack(const QStringList& chosen, const QString& pack, const QString& name);
    /// Saves, then brings the asset pack in the folder `pack` into the project, and opens the
    /// project again. False, saying why, if it cannot.
    bool import_asset_pack(const QString& pack);
    /// Puts entity under `parent` (or at the top, for an invalid one), as the outliner's
    /// right-click menu does. False, saying why, if it cannot.
    bool put_under(engine::Guid entity, engine::Guid parent);
    /// Game > Play in the editor (F6): saves, then plays the game in place of the scene.
    /// `sound` off for tests. False, saying why, if it cannot start.
    bool play_here(bool sound = true);
    /// Game > Stop playing (Shift+F6), or the game quitting: back to the scene.
    void stop_playing();
    [[nodiscard]] GameView* game_view() const noexcept { return game_; }
    /// Edit > Copy: the selected entities, with everything under them, to the clipboard.
    bool copy_selection();
    /// Edit > Paste: the clipboard's entities, the first where the camera looks; selected.
    std::vector<engine::Guid> paste();
    /// Edit > Duplicate (Ctrl+D): copies beside the selected ones; selected.
    std::vector<engine::Guid> duplicate_selection();
    /// F: the camera to the middle of what is selected.
    void frame_selection();
    /// Makes a conversation as assets/talk/<name>.dtalk, with one node, and opens it.
    StoryWindow* new_story(const QString& name);
    /// Opens a data table of the project (its path in assets/, a .dtable), or brings its
    /// window forward. Null, showing why, if it cannot.
    TableWindow* open_table(const QString& asset_path);
    /// Makes a table as assets/data/<name>.dtable and opens it.
    TableWindow* new_table(const QString& name);
    /// Makes a prefab of the selected entity, named `name` (Edit > Make a prefab).
    bool make_prefab(const QString& name);
    /// Makes every copy of the selected entity's prefab in this scene match it again.
    std::size_t update_prefab_copies();
    /// Opens the sound effect generator, saving into the project's assets/sfx.
    SfxGenerator* show_sfx_generator();
    /// Reads the assets folder again, and draws again the pictures that changed (hot
    /// reload): after Sprite Studio saves, and when the editor comes back to the front.
    void reload_assets();
    /// Selects an entity everywhere: the list, the scene and the inspector.
    void select(engine::Guid entity);
    /// Selects several entities together: all in the list and outlined in the scene, the
    /// first in the inspector. Delete removes them all and dragging moves them all.
    void select_many(std::vector<engine::Guid> entities);
    /// Adds `entity` to the selection, or takes it out (Ctrl- or Shift-click).
    void toggle_selected(engine::Guid entity);
    /// Everything selected, the inspector's first.
    [[nodiscard]] const std::vector<engine::Guid>& selected() const noexcept { return selected_; }

    [[nodiscard]] Document* document() const noexcept { return document_.get(); }
    [[nodiscard]] engine::Guid selection() const noexcept { return selection_; }
    [[nodiscard]] SceneView* scene_view() const noexcept { return view_; }
    [[nodiscard]] QTreeWidget* outliner() const noexcept { return outliner_; }
    [[nodiscard]] Inspector* inspector() const noexcept { return inspector_; }
    [[nodiscard]] TilePalette* tile_palette() const noexcept { return palette_; }
    [[nodiscard]] QTreeWidget* logic_view() const noexcept;
    [[nodiscard]] LogicPanel* logic_panel() const noexcept { return logic_; }
    [[nodiscard]] ProjectPanel* project_panel() const noexcept { return project_; }

    // The commands, for menus, tool buttons and tests.
    bool save();
    void undo();
    void redo();
    engine::Guid new_entity();
    /// Draws a character of `look` (editor/core/characters.h) as art/<name>.png and places
    /// it where the camera looks, standing, ready to walk; selected. An invalid GUID, saying
    /// why, for a name that is not usable or taken.
    engine::Guid create_character(const QString& name, const CharacterLook& look);
    /// File > New character: the generator, its choices and colours, with a preview.
    void show_character_generator();
    void delete_selection();
    /// Saves, then starts the player on the project. Returns false, showing why, if it
    /// could not start.
    bool play();
    /// How many times Play started the game this session (for the tutorials).
    [[nodiscard]] int plays() const noexcept { return plays_; }
    [[nodiscard]] TutorialPanel* tutorials() const noexcept { return tutorials_; }
    /// Exports the game to `folder` (asked for when empty). Returns false, showing why.
    bool export_to(const std::filesystem::path& folder);

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    /// Runs git with `args` in the project's folder; its output in `out`. False, with what
    /// git said in `error`, if it failed or is not installed.
    bool git(const QStringList& args, QString* out, QString* error) const;
    /// Shows the document's scene everywhere, after another was opened.
    void show_scene();
    /// After undo or redo: keeps selected what is still there.
    void reselect();
    bool quiet_ = false;
    QByteArray first_layout_; ///< The panels as built, for reset_layout().
    std::vector<engine::Guid> selected_;

private:
    void build_actions();
    /// After an edit: `structure` rebuilds the lists, not only the scene.
    void changed(bool structure);
    void rebuild_outliner();
    void update_title_and_actions();
    void report(const QString& message);

    /// Closes the sprite windows, asking to save their changes. False if one stays open.
    bool close_sprites();

    std::filesystem::path player_;
    /// The open Sprite Studio windows, by the .dsprite each edits.
    std::map<std::string, SpriteStudio*> studios_;
    /// The open Story windows, by the conversation each edits.
    std::map<std::string, StoryWindow*> stories_;
    std::map<std::string, ScriptWindow*> scripts_;
    /// The open table windows, by the table each edits.
    std::map<std::string, TableWindow*> tables_;
    QTimer* autosave_timer_ = nullptr;
    QStackedWidget* stack_ = nullptr; ///< The start page, or the scene.
    SearchBox* search_ = nullptr;
    HelpWindow* guide_ = nullptr;
    TutorialPanel* tutorials_ = nullptr;
    int plays_ = 0;
    DoctorPanel* doctor_ = nullptr;
    StartPage* start_ = nullptr;
    QSettings* settings_ = nullptr;
    QStringList recent_;
    std::unique_ptr<Document> document_;
    engine::Guid selection_;
    SceneView* view_ = nullptr;
    QTreeWidget* outliner_ = nullptr;
    QTreeView* asset_tree_ = nullptr;
    GameView* game_ = nullptr;
    QLineEdit* asset_search_ = nullptr;
    QDialog* usages_ = nullptr;
    QString palette_key_; ///< The tileset the palette shows, to notice when it changes.
    QFileSystemModel* asset_model_ = nullptr;
    Inspector* inspector_ = nullptr;
    TilePalette* palette_ = nullptr;
    LogicPanel* logic_ = nullptr;
    ProjectPanel* project_ = nullptr;
    QAction* save_ = nullptr;
    QAction* undo_ = nullptr;
    QAction* redo_ = nullptr;
    QAction* delete_ = nullptr;
    QAction* play_ = nullptr;
    QAction* export_ = nullptr;
    QAction* new_entity_ = nullptr;
};

} // namespace editor
